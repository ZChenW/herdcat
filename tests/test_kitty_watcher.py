#!/usr/bin/env python3
"""Import and call the kitty watcher with a fake window."""

import contextlib
import importlib.util
import io
import os
import socket
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

SOURCE = (
    Path(__file__).resolve().parents[1]
    / "integrations"
    / "kitty"
    / "bongocat_watcher.py"
)
BANNED = (
    "fork",
    "subprocess",
    "os.system",
    "waitpid",
    "Popen",
    "execvp",
    "os.exec",
    "spawn",
)


def load_watcher():
    stdout = io.StringIO()
    stderr = io.StringIO()
    spec = importlib.util.spec_from_file_location("bongocat_watcher", SOURCE)
    module = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        spec.loader.exec_module(module)
    if stdout.getvalue() or stderr.getvalue():
        raise AssertionError("watcher printed during import")
    if hasattr(module, "boss"):
        raise AssertionError("watcher imported kitty")
    return module


WATCHER = load_watcher()


class Window:
    def __init__(self, ident):
        self.id = ident


class WatcherTest(unittest.TestCase):
    def setUp(self):
        self.previous = os.environ.get("XDG_RUNTIME_DIR")
        self.directory = tempfile.TemporaryDirectory()
        os.chmod(self.directory.name, 0o700)
        os.environ["XDG_RUNTIME_DIR"] = self.directory.name

    def tearDown(self):
        self.directory.cleanup()
        if self.previous is None:
            os.environ.pop("XDG_RUNTIME_DIR", None)
        else:
            os.environ["XDG_RUNTIME_DIR"] = self.previous

    def call(self, window, data):
        stdout = io.StringIO()
        stderr = io.StringIO()
        started = time.monotonic()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            WATCHER.on_focus_change(object(), window, data)
        elapsed = time.monotonic() - started
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        self.assertLess(elapsed, 0.05)
        return elapsed

    def listen(self):
        path = os.path.join(self.directory.name, "bongocat.sock")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        server.bind(path)
        server.listen(1)
        server.settimeout(0.2)
        return server

    def test_reports_focused_split(self):
        server = self.listen()
        try:
            self.call(Window(12), {"focused": True})
            client, _addr = server.accept()
            try:
                payload = client.recv(128)
            finally:
                client.close()
        finally:
            server.close()
        self.assertEqual(payload, ("pane %d 12" % os.getpid()).encode("ascii"))

    def test_ignores_unfocused_and_bad_ids(self):
        server = self.listen()
        server.setblocking(False)
        try:
            self.call(Window(12), {"focused": False})
            self.call(Window(12), {})
            self.call(Window(12), None)
            self.call(Window(12), {"focused": 1})
            self.call(Window(True), {"focused": True})
            self.call(Window(False), {"focused": True})
            self.call(Window(0), {"focused": True})
            self.call(Window(-3), {"focused": True})
            self.call(Window(1.5), {"focused": True})
            self.call(Window("12"), {"focused": True})
            self.call(None, {"focused": True})
            with self.assertRaises(BlockingIOError):
                server.accept()
        finally:
            server.close()

    def test_source_starts_no_program(self):
        text = SOURCE.read_text(encoding="utf-8")
        for word in BANNED:
            self.assertNotIn(word, text)

    def test_missing_socket_returns_quickly(self):
        self.call(Window(4), {"focused": True})

    def test_rejects_unsafe_runtime_dir(self):
        os.chmod(self.directory.name, 0o777)
        server = self.listen()
        server.setblocking(False)
        try:
            self.call(Window(9), {"focused": True})
            with self.assertRaises(BlockingIOError):
                server.accept()
        finally:
            server.close()

    def test_socket_path_matches_control_rules(self):
        uid = os.getuid()
        self.assertEqual(
            WATCHER.control_socket_path(),
            os.path.join(self.directory.name, "bongocat.sock"),
        )
        os.environ["XDG_RUNTIME_DIR"] = ""
        self.assertEqual(WATCHER.control_socket_path(), "/tmp/bongocat-%d.sock" % uid)
        os.environ.pop("XDG_RUNTIME_DIR")
        self.assertEqual(WATCHER.control_socket_path(), "/tmp/bongocat-%d.sock" % uid)
        os.environ["XDG_RUNTIME_DIR"] = os.path.join(self.directory.name, "missing")
        self.assertIsNone(WATCHER.control_socket_path())
        os.environ["XDG_RUNTIME_DIR"] = "x" * 120
        self.assertIsNone(WATCHER.control_socket_path())
        nested = os.path.join(self.directory.name, "a")
        os.mkdir(nested)
        os.chmod(nested, 0o775)
        os.environ["XDG_RUNTIME_DIR"] = nested
        self.assertIsNone(WATCHER.control_socket_path())
        long_name = "p" * (94 - len(self.directory.name) - 1)
        long_dir = os.path.join(self.directory.name, long_name)
        os.mkdir(long_dir)
        os.chmod(long_dir, 0o700)
        os.environ["XDG_RUNTIME_DIR"] = long_dir
        self.assertEqual(len(long_dir), 94)
        self.assertIsNone(WATCHER.control_socket_path())


if __name__ == "__main__":
    unittest.main()
