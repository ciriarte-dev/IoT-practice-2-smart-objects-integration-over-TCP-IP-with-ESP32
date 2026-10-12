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
from unittest.mock import Mock
from decimal import Decimal
from server.protocol import ProtocolHandler, ProtocolError


ROOT = Path(__file__).resolve().parents[2]


class TCPServerTests(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.server = TCPServer("127.0.0.1", 0, clock=lambda: self.now)
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
        first.sendall(b"HELLO SEN")
        second.sendall(b"HELLO ACT")
        self.pump_until(lambda: sum(len(c.receive_buffer)
                                   for c in self.server.connections)
                        == len(b"HELLO SENHELLO ACT"))
        buffers = {c.address: bytes(c.receive_buffer) for c in self.server.connections}
        self.assertEqual(buffers[first.getsockname()], b"HELLO SEN")
        self.assertEqual(buffers[second.getsockname()], b"HELLO ACT")
        first.sendall(b"SOR\n")
        second.sendall(b"UATOR\n")
        self.assertEqual(self.read_lines(first), b"OK\n")
        self.assertEqual(self.read_lines(second, 2), b"OK\nLED ALL OFF\n")

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
        first, second = self.connect(), self.connect()
        self.pump_until(lambda: len(self.server.connections) == 2)
        first.sendall(b"x" * 129)
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

    def read_lines(self, client, count=1):
        client.setblocking(False)
        result = bytearray()
        deadline = time.monotonic() + 3
        while result.count(b"\n") < count:
            self.server.poll_once(0.001)
            try:
                data = client.recv(4096)
            except BlockingIOError:
                data = None
            if data == b"":
                break
            if data:
                result.extend(data)
            if time.monotonic() > deadline:
                self.fail("Respuesta incompleta: %r" % result)
        return bytes(result)

    def identified(self, role):
        client = self.connect()
        client.sendall(("HELLO " + role + "\n").encode())
        expected = b"OK\n" if role == "SENSOR" else b"OK\nLED ALL OFF\n"
        self.assertEqual(self.read_lines(client, expected.count(b"\n")), expected)
        return client

    def pair(self):
        return self.identified("SENSOR"), self.identified("ACTUATOR")

    def test_identification_sensor(self):
        self.identified("SENSOR")
        self.assertEqual(self.server.connections[0].role, "SENSOR")

    def test_identification_actuator(self):
        self.identified("ACTUATOR")
        self.assertEqual(self.server.connections[0].role, "ACTUATOR")

    def test_all_distance_boundaries(self):
        sensor, actuator = self.pair()
        for distance, command in [("10","RED BLINK"),("19.99","RED BLINK"),
                                  ("20","YELLOW ON"),("30","YELLOW ON"),
                                  ("40","YELLOW ON"),("40.01","GREEN ON"),
                                  ("50","GREEN ON"),("2","RED BLINK"),("400","GREEN ON")]:
            with self.subTest(distance=distance):
                sensor.sendall(("DIST " + distance + "\n").encode())
                self.assertEqual(self.read_lines(actuator), ("LED " + command + "\n").encode())

    def test_invalid_turns_off(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 10\n")
        self.read_lines(actuator)
        sensor.sendall(b"INVALID\n")
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")

    def test_sensor_disconnect_turns_off_and_reconnects(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        sensor.close()
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")
        sensor = self.identified("SENSOR")
        sensor.sendall(b"DIST 20\n")
        self.assertEqual(self.read_lines(actuator), b"LED YELLOW ON\n")

    def test_expiration_and_periodic_resend(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 10\n"); self.read_lines(actuator)
        self.now = .5
        self.assertEqual(self.read_lines(actuator), b"LED RED BLINK\n")
        self.now = 1.999
        self.assertEqual(self.read_lines(actuator), b"LED RED BLINK\n")
        self.now = 2
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")
        self.now = 2.5
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")

    def test_new_valid_reading_refreshes_expiration(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 10\n"); self.read_lines(actuator)
        self.now = .4
        sensor.sendall(b"DIST 30\n"); self.read_lines(actuator)
        self.assertEqual(self.server.controller.valid_until, 2.4)

    def test_actuator_reconnect_waits_for_new_measurement(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        actuator.close()
        self.pump_until(lambda: len(self.server.connections) == 1)
        actuator = self.identified("ACTUATOR")
        self.now = .5
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")
        sensor.sendall(b"DIST 30\n")
        self.assertEqual(self.read_lines(actuator), b"LED YELLOW ON\n")

    def test_incomplete_measurement_does_not_refresh(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 10\n"); self.read_lines(actuator)
        sensor.sendall(b"DIST 3")
        self.pump_until(lambda: any(c.receive_buffer == b"DIST 3" for c in self.server.connections))
        self.now = 2
        self.assertEqual(self.read_lines(actuator), b"LED ALL OFF\n")
        sensor.sendall(b"0\n")
        self.assertEqual(self.read_lines(actuator), b"LED YELLOW ON\n")

    def test_multiple_measurements_one_receive(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 10\nDIST 30\nDIST 50\n")
        self.assertEqual(self.read_lines(actuator, 3), b"LED RED BLINK\nLED YELLOW ON\nLED GREEN ON\n")

    def test_duplicate_does_not_invalidate_sensor(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        duplicate = self.connect(); duplicate.sendall(b"HELLO SENSOR\n")
        self.assertEqual(self.read_lines(duplicate), b"ERROR DUPLICATE\n")
        self.assertEqual(self.server.controller.command, "LED GREEN ON")
        self.pump_until(lambda: len(self.server.connections) == 2)

    def test_unknown_and_missing_identification(self):
        for message, error in [(b"HELLO OTHER\n",b"ROLE"),(b"DIST 20\n",b"IDENTIFICATION")]:
            client = self.connect(); client.sendall(message)
            self.assertEqual(self.read_lines(client), b"ERROR " + error + b"\n")
            self.pump_until(lambda: not self.server.connections)

    def test_identification_timeout(self):
        client = self.connect()
        self.pump_until(lambda: len(self.server.connections)==1)
        client.sendall(b"HELLO SEN")
        self.pump_until(lambda: bool(self.server.connections[0].receive_buffer))
        self.now=5
        self.assertEqual(self.read_lines(client), b"ERROR TIMEOUT\n")
        self.pump_until(lambda: not self.server.connections)

    def test_bad_sensor_messages_turn_off(self):
        sensor, actuator = self.pair()
        for message, code in [(b"DIST -1\n",b"VALUE"),(b"DIST NaN\n",b"VALUE"),
                              (b"DIST 401\n",b"VALUE"),(b"DIST 1.99\n",b"VALUE"),
                              (b"DIST 2e1\n",b"VALUE"),(b"DIST 20,5\n",b"VALUE"),
                              (b"DIST 20 extra\n",b"FORMAT"),(b"DIST 20\r\n",b"FORMAT"),
                              (b"HELLO SENSOR\n",b"FORMAT"),(b"\n",b"FORMAT")]:
            with self.subTest(message=message):
                sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
                sensor.sendall(message)
                self.assertEqual(self.read_lines(sensor),b"ERROR "+code+b"\n")
                self.assertEqual(self.read_lines(actuator),b"LED ALL OFF\n")

    def test_non_ascii_closes_and_invalidates(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        sensor.sendall(b"\xff")
        self.assertEqual(self.read_lines(sensor),b"ERROR ENCODING\n")
        self.assertIn(b"LED ALL OFF\n",self.read_lines(actuator))
        self.pump_until(lambda: len(self.server.connections)==1)

    def test_actuator_cannot_supply_distances(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        actuator.sendall(b"DIST 10\n")
        self.assertEqual(self.read_lines(actuator),b"ERROR FORMAT\n")
        self.assertEqual(self.server.controller.command,"LED GREEN ON")

    def fake_actuator(self):
        actuator = self.identified("ACTUATOR")
        state = next(c for c in self.server.connections if c.role=="ACTUATOR")
        real = state.sock
        wrapper = Mock(wraps=real)
        self.server._clients.pop(real)
        self.server._clients[wrapper]=state
        state.sock=wrapper
        return actuator, state, wrapper

    def test_partial_sends_preserve_bytes(self):
        actuator, state, wrapper = self.fake_actuator()
        self.server._queue(state,"LED RED BLINK",is_state=True,expires_at=2)
        captured=bytearray()
        def partial(data):
            captured.extend(data[:2]); return min(2,len(data))
        wrapper.send.side_effect=partial
        while state.outgoing:
            self.server._write_client(state)
        self.assertEqual(captured,b"LED RED BLINK\n")

    def test_slow_client_timeout_does_not_reset_on_heartbeat(self):
        actuator, state, wrapper = self.fake_actuator()
        wrapper.send.side_effect=BlockingIOError()
        self.now=.5; self.server._timers()
        self.server._write_client(state)
        self.now=1; self.server._timers()
        self.now=1.5; self.server._timers()
        self.assertNotIn(state,self.server.connections)
        self.identified("SENSOR")

    def test_output_queue_is_bounded(self):
        actuator, state, wrapper = self.fake_actuator()
        self.server.max_pending_bytes=32
        wrapper.send.side_effect=BlockingIOError()
        for _ in range(4):
            self.server._queue(state,"LED RED BLINK",is_state=True)
        self.assertNotIn(state,self.server.connections)

    def test_partial_stale_frame_closes_instead_of_corrupting(self):
        actuator, state, wrapper = self.fake_actuator()
        wrapper.send.return_value=2
        self.server._queue(state,"LED RED BLINK",is_state=True,expires_at=2)
        self.server._write_client(state)
        self.server.controller.invalidate(); self.server._publish()
        self.assertNotIn(state,self.server.connections)

    def test_unsent_stale_color_is_removed_on_invalid(self):
        actuator, state, wrapper = self.fake_actuator()
        self.server._queue(state,"LED RED BLINK",is_state=True,expires_at=2)
        self.server.controller.invalidate(); self.server._publish()
        self.assertEqual([frame.data for frame in state.outgoing],[b"LED ALL OFF\n"])

    def test_shutdown_attempts_off(self):
        sensor, actuator = self.pair()
        sensor.sendall(b"DIST 50\n"); self.read_lines(actuator)
        self.server.close()
        actuator.settimeout(2)
        self.assertEqual(actuator.recv(100),b"LED ALL OFF\n")


class ProtocolTests(unittest.TestCase):
    def test_maximum_line_exactly_128(self):
        buffer=bytearray()
        line=b"DIST " + b"0"*121 + b"20"
        self.assertEqual(len(line),128)
        result=list(ProtocolHandler.feed(buffer,line+b"\n"))
        self.assertEqual(ProtocolHandler.measurement(result[0]),Decimal(20))
        result=list(ProtocolHandler.feed(buffer,b"x"*129))
        self.assertIsInstance(result[0],ProtocolError)
        self.assertEqual(result[0].code,"TOO_LONG")

    def test_decimal_precision_around_thresholds(self):
        from server.controller import DistanceController
        controller=DistanceController()
        controller.update(ProtocolHandler.measurement("DIST 19.99999999999999999999"),0)
        self.assertEqual(controller.command,"LED RED BLINK")
        controller.update(ProtocolHandler.measurement("DIST 40.00000000000000000001"),0)
        self.assertEqual(controller.command,"LED GREEN ON")


class CommandLineTests(unittest.TestCase):
    def test_default_host_and_port(self):
        args = build_parser().parse_args([])
        self.assertEqual((args.host, args.port), ("0.0.0.0", 8080))

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
