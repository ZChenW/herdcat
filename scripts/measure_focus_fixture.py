"""Private Niri event stream and a real agent ancestry on an owned PTY."""
import json
import os
from pathlib import Path
import pty
import signal
import socket
import threading
import time


def process_chain(depth, pipe):
    if depth:
        child = os.fork()
        if child == 0:
            process_chain(depth - 1, pipe)
        os.close(pipe)

        def terminate(signum, frame):
            try:
                os.kill(child, signal.SIGTERM)
            except ProcessLookupError:
                pass

        signal.signal(signal.SIGTERM, terminate)
        os.waitpid(child, 0)
        os._exit(0)
    signal.signal(signal.SIGTERM, lambda signum, frame: os._exit(0))
    os.write(pipe, str(os.getpid()).encode())
    os.close(pipe)
    while True:
        signal.pause()


class FocusFixture:
    def __init__(self, directory, depth, title='Working'):
        if not isinstance(depth, int) or not 0 <= depth <= 8:
            raise ValueError('ancestry depth must be from 0 to 8')
        self.path = directory / 'niri-test.sock'
        self.depth = depth
        self.title = title
        self.pid = self.pty = self.server = self.thread = None
        self.agent_pid = None
        self.done = threading.Event()
        self.errors = []

    def start(self):
        read_pipe, write_pipe = os.pipe()
        self.pid, self.pty = pty.fork()
        if self.pid == 0:
            os.close(read_pipe)
            process_chain(self.depth, write_pipe)
        os.close(write_pipe)
        try:
            self.agent_pid = int(os.read(read_pipe, 64))
            self.server = socket.socket(socket.AF_UNIX)
            self.server.bind(str(self.path))
            self.server.listen()
            self.server.settimeout(.2)
            self.thread = threading.Thread(target=self.serve, daemon=True)
            self.thread.start()
            return self
        except Exception:
            self.close()
            raise
        finally:
            os.close(read_pipe)

    def serve(self):
        try:
            while not self.done.is_set():
                try:
                    connection, _ = self.server.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            with connection:
                connection.settimeout(3)
                with connection.makefile('rb') as stream:
                    request = stream.readline(128)
                if request != b'"EventStream"\n':
                    raise RuntimeError(f'unexpected private Niri request: {request!r}')
                event = {'WindowsChanged': {'windows': [dict(
                    id=100, pid=self.pid, title=self.title, is_focused=True)]}}
                connection.sendall((json.dumps(event) + '\n').encode())
                self.done.wait()
        except Exception as error:
            self.errors.append(str(error))

    def close(self):
        self.done.set()
        if self.thread is not None:
            self.thread.join(timeout=4)
            if self.thread.is_alive():
                raise RuntimeError('private Niri fixture thread survived cleanup')
        if self.server is not None:
            self.server.close()
        if self.pid is not None:
            try:
                os.kill(self.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            until = time.monotonic() + 3
            while os.waitpid(self.pid, os.WNOHANG)[0] == 0:
                if time.monotonic() >= until:
                    os.killpg(self.pid, signal.SIGKILL)
                    os.waitpid(self.pid, 0)
                    raise RuntimeError('agent ancestry did not exit cleanly')
                time.sleep(.01)
            if self.agent_pid is not None and Path(f'/proc/{self.agent_pid}').exists():
                raise RuntimeError('private agent survived ancestry cleanup')
            os.close(self.pty)
            self.pid = None
        if self.errors:
            raise RuntimeError('; '.join(self.errors))
