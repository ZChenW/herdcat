#!/usr/bin/env python3
"""Adapter policy I/O with a synthetic adapter and an isolated control socket."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading

fixture = str(Path('build/test_agent_adapters').resolve())
binary = str(Path('build/bongocat').resolve())
with tempfile.TemporaryDirectory(prefix='bongo-adapter-') as directory:
    env = dict(os.environ, XDG_RUNTIME_DIR=directory)
    env.pop('BONGOCAT_HOOK_DEBUG', None)
    def invoke(payload, args=None):
        result = subprocess.run(args or [fixture, '--client'], input=payload,
                                env=env, capture_output=True, timeout=4)
        assert result.returncode == 0 and result.stderr == b'', result
        assert result.stdout == (b'{}\n' if args is None else b''), result
    # Policy survives invalid input, unknown events and no daemon.
    for payload in (b'{', b'{}', b'{"event":"finish","status":"completed"}'):
        invoke(payload)
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as server:
        server.bind(str(Path(directory) / 'bongocat.sock'))
        server.listen()
        captured = []
        def receive():
            server.settimeout(4)
            for _ in range(3):
                connection, _ = server.accept()
                with connection:
                    captured.append(connection.recv(64).decode())
                    connection.sendall(b'0 ok')
        worker = threading.Thread(target=receive)
        worker.start()
        invoke(b'{"event":"finish","status":"completed"}')
        for agent in ('claude', 'codex'):
            invoke(b'{"hook_event_name":"Stop","session_id":"test"}',
                   [binary, '--hook', agent, '--event', 'PreToolUse'])
        worker.join(5)
        assert not worker.is_alive()
        assert len(captured) == 3, captured
        assert captured[0].startswith('ev fixture done '), captured
        assert captured[1] == f'ev claude working e430d22bdbbe8583 {os.getpid()}'
        assert captured[2] == f'ev codex working 0f886b5c86d51f1a {os.getpid()}'
    process = subprocess.Popen([fixture, '--client'], env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    try:
        assert process.wait(timeout=3) == 0
        assert process.stdout.read() == b'{}\n' and process.stderr.read() == b''
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdin.close()
        process.stdout.close()
        process.stderr.close()
    for payload in (b'{', b'{}', b'{"hookEventName":"stop"}'):
        result = subprocess.run([binary, '--hook', 'grok'], input=payload,
                                env=env, capture_output=True, timeout=3)
        assert result.returncode == 0 and result.stdout == b'{}\n' and not result.stderr
    for args in (['--event', 'Stop'], ['--event', 'Stop', '--hook', 'codex'],
                 ['--hook', 'claude', '--event'],
                 ['--hook', 'claude', '--event', 'x' * 64],
                 ['--hook', 'claude', '--event', 'Stop', '--event', 'Stop']):
        result = subprocess.run([binary, *args], input=b'', env=env,
                                capture_output=True, timeout=3)
        assert result.returncode == 1 and not result.stdout, result
print('Adapter stdout policy, override, wire compatibility and timeout passed.')
