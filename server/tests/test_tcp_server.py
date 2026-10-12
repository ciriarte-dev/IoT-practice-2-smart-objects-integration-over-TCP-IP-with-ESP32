"""Pruebas locales de transporte; no requieren ESP32 ni servicios externos."""

import os
from pathlib import Path
import queue
import re
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

from server.main import build_parser
from server.tcp_server import TCPServer


ROOT = Path(__file__).resolve().parents[2]


class TCPServerTests(unittest.TestCase):
    def setUp(self):
        self.server = TCPServer("127.0.0.1", 0)
        self.addCleanup(self.server.close)
        self.server.start()

    def pump_until(self, condition):
        deadline = time.monotonic() + 3
        while not condition():
            if time.monotonic() >= deadline:
                self.fail("El servidor no alcanzó el estado esperado")
            self.server.poll_once(0.02)

    def connect(self):
        client = socket.create_connection(self.server.address, timeout=2)
        self.addCleanup(client.close)
        return client

    def test_listens_on_configured_address(self):
        self.assertEqual(self.server.address[0], "127.0.0.1")
        self.assertGreater(self.server.address[1], 0)
        self.connect()
        self.pump_until(lambda: len(self.server.connections) == 1)

    def test_two_clients_and_idle_client_does_not_block(self):
        idle = self.connect()
        active = self.connect()
        self.pump_until(lambda: len(self.server.connections) == 2)
        active.sendall(b"datos")
        self.pump_until(lambda: any(c.receive_buffer == b"datos"
                                   for c in self.server.connections))
        buffers = {c.address: bytes(c.receive_buffer) for c in self.server.connections}
        self.assertEqual(buffers[idle.getsockname()], b"")
        self.assertEqual(buffers[active.getsockname()], b"datos")

    def test_buffers_are_independent_and_keep_fragments(self):
        first, second = self.connect(), self.connect()
        self.pump_until(lambda: len(self.server.connections) == 2)
        first.sendall(b"fragmento")
        second.sendall(b"otro")
        self.pump_until(lambda: sum(len(c.receive_buffer)
                                   for c in self.server.connections)
                        == len(b"fragmentootro"))
        first.sendall(b" final\n")
        self.pump_until(lambda: sum(len(c.receive_buffer)
                                   for c in self.server.connections)
                        == len(b"fragmento final\notro"))
        buffers = {c.address: bytes(c.receive_buffer) for c in self.server.connections}
        self.assertEqual(buffers[first.getsockname()], b"fragmento final\n")
        self.assertEqual(buffers[second.getsockname()], b"otro")

    def test_disconnect_is_logged_and_other_client_survives(self):
        first, second = self.connect(), self.connect()
        with self.assertLogs("server.tcp_server", level="INFO") as logs:
            self.pump_until(lambda: len(self.server.connections) == 2)
            first.close()
            self.pump_until(lambda: len(self.server.connections) == 1)
        self.assertTrue(any("Cliente conectado" in line for line in logs.output))
        self.assertTrue(any("cierre remoto" in line for line in logs.output))
        second.sendall(b"sigue")
        self.pump_until(lambda: self.server.connections[0].receive_buffer == b"sigue")

    @unittest.skipIf(os.name == "nt", "Formato SO_LINGER para sistemas POSIX")
    def test_reset_does_not_stop_server(self):
        broken = self.connect()
        self.pump_until(lambda: len(self.server.connections) == 1)
        broken.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        broken.close()
        self.pump_until(lambda: not self.server.connections)
        self.connect()
        self.pump_until(lambda: len(self.server.connections) == 1)

    def test_buffer_limit_disconnects_only_offending_client(self):
        self.server.max_buffer_bytes = 8
        first, second = self.connect(), self.connect()
        self.pump_until(lambda: len(self.server.connections) == 2)
        first.sendall(b"123456789")
        self.pump_until(lambda: len(self.server.connections) == 1)
        second.sendall(b"ok")
        self.pump_until(lambda: self.server.connections[0].receive_buffer == b"ok")

    def test_close_clears_buffers_and_releases_port(self):
        client = self.connect()
        self.pump_until(lambda: len(self.server.connections) == 1)
        client.sendall(b"temporal")
        self.pump_until(lambda: bool(self.server.connections[0].receive_buffer))
        state = self.server.connections[0]
        address = self.server.address
        self.server.close()
        self.server.close()
        self.assertEqual(state.sock.fileno(), -1)
        self.assertEqual(state.receive_buffer, b"")
        self.assertEqual(self.server.connections, ())
        self.assertEqual(client.recv(1), b"")
        replacement = TCPServer(*address)
        self.addCleanup(replacement.close)
        replacement.start()
        self.assertEqual(replacement.address, address)

    def test_occupied_port_fails_and_can_be_cleaned_up(self):
        other = TCPServer(*self.server.address)
        self.addCleanup(other.close)
        with self.assertRaises(OSError):
            other.start()
        other.close()
        self.connect()
        self.pump_until(lambda: len(self.server.connections) == 1)

    def test_transport_does_not_reply_or_interpret_hello(self):
        client = self.connect()
        client.sendall(b"HELLO SENSOR\n")
        self.pump_until(lambda: bool(self.server.connections)
                        and self.server.connections[0].receive_buffer == b"HELLO SENSOR\n")
        client.settimeout(0.05)
        with self.assertRaises(socket.timeout):
            client.recv(1)


class CommandLineTests(unittest.TestCase):
    def test_default_host_and_port(self):
        args = build_parser().parse_args([])
        self.assertEqual((args.host, args.port), ("0.0.0.0", 5000))

    def test_invalid_port_is_reported_without_traceback(self):
        result = subprocess.run(
            [sys.executable, "-B", "server/main.py", "--port", "65536"],
            cwd=ROOT, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn("El puerto debe estar entre", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_occupied_port_exits_with_error(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            result = subprocess.run(
                [sys.executable, "-B", "server/main.py", "--host", "127.0.0.1",
                 "--port", str(listener.getsockname()[1])],
                cwd=ROOT, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("No se pudo ejecutar el servidor", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    @unittest.skipIf(os.name == "nt", "SIGINT de proceso se verifica en POSIX")
    def test_sigint_closes_clients_and_exits_cleanly(self):
        process = subprocess.Popen(
            [sys.executable, "-B", "-u", "server/main.py",
             "--host", "127.0.0.1", "--port", "0"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        lines = queue.Queue()
        def collect_output():
            for line in process.stdout:
                lines.put(line)

        reader = threading.Thread(target=collect_output, daemon=True)
        reader.start()
        output = []

        def wait_for(text):
            deadline = time.monotonic() + 5
            while True:
                try:
                    line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
                except queue.Empty:
                    self.fail("Falta registro %r: %s" % (text, output))
                output.append(line)
                if text in line:
                    return line
                if time.monotonic() >= deadline:
                    self.fail("Venció la espera de %r" % text)

        try:
            startup = wait_for("Servidor escuchando")
            port = int(re.search(r"127\.0\.0\.1:(\d+)", startup).group(1))
            with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
                wait_for("Cliente conectado")
                process.send_signal(signal.SIGINT)
                self.assertEqual(process.wait(timeout=5), 0)
                self.assertEqual(client.recv(1), b"")
            reader.join(timeout=2)
            while not lines.empty():
                output.append(lines.get_nowait())
            self.assertIn("Servidor cerrado", "".join(output))
            self.assertIn("Ctrl+C", "".join(output))
            self.assertNotIn("Traceback", "".join(output))
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            reader.join(timeout=2)
            process.stdout.close()


if __name__ == "__main__":
    unittest.main()
