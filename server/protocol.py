"""Gramática y framing del contrato ASCII/LF, sin dependencias de sockets."""
from decimal import Decimal
import re


class ProtocolError(ValueError):
    def __init__(self, code, fatal=False):
        super().__init__(code)
        self.code = code
        self.fatal = fatal


class ProtocolHandler:
    MAX_LINE_BYTES = 128
    NUMBER = re.compile(r"[0-9]+(?:\.[0-9]+)?", re.ASCII)

    @classmethod
    def feed(cls, buffer, data):
        """Produce líneas o errores en orden, conservando solo el fragmento final."""
        for byte in data:
            if byte > 127:
                buffer.clear()
                yield ProtocolError("ENCODING", True)
                return
            if byte == 10:
                line = buffer.decode("ascii")
                buffer.clear()
                yield line
            elif len(buffer) >= cls.MAX_LINE_BYTES:
                buffer.clear()
                yield ProtocolError("TOO_LONG", True)
                return
            else:
                buffer.append(byte)

    @staticmethod
    def identify(line):
        if line in ("HELLO SENSOR", "HELLO ACTUATOR"):
            return line.split(" ")[1]
        if re.fullmatch(r"HELLO [A-Z]+", line):
            raise ProtocolError("ROLE", True)
        raise ProtocolError("IDENTIFICATION", True)

    @classmethod
    def measurement(cls, line):
        if line == "INVALID":
            return None
        fields = line.split(" ")
        if (len(fields) != 2 or fields[0] != "DIST" or not fields[1]
                or any(ord(c) < 32 or ord(c) == 127 for c in line)):
            raise ProtocolError("FORMAT")
        if not cls.NUMBER.fullmatch(fields[1]):
            raise ProtocolError("VALUE")
        value = Decimal(fields[1])
        if not value.is_finite() or not Decimal(2) <= value <= Decimal(400):
            raise ProtocolError("VALUE")
        return value

    @staticmethod
    def encode(line):
        return (line + "\n").encode("ascii")
