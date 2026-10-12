"""Punto de entrada del servidor TCP IoT."""

import argparse
import logging
import math

if __package__:
    from .tcp_server import TCPServer
else:
    from tcp_server import TCPServer


def port_number(value: str) -> int:
    try:
        port = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("El puerto debe ser un entero") from exc
    if not 0 <= port <= 65535:
        raise argparse.ArgumentTypeError("El puerto debe estar entre 0 y 65535")
    return port


def positive_seconds(value):
    try:
        seconds = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("El tiempo debe ser un número") from exc
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("El tiempo debe ser positivo y finito")
    return seconds


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Servidor TCP IoT")
    parser.add_argument("--host", default="0.0.0.0", help="IPv4 de escucha (0.0.0.0)")
    parser.add_argument("--port", type=port_number, default=8080,
                        help="Puerto de escucha (8080); 0 elige un puerto libre")
    parser.add_argument("--identification-timeout", type=positive_seconds, default=5.0)
    parser.add_argument("--measurement-ttl", type=positive_seconds, default=2.0)
    parser.add_argument("--resend-interval", type=positive_seconds, default=0.5)
    parser.add_argument("--send-timeout", type=positive_seconds, default=1.0)
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    server = TCPServer(args.host, args.port,
                       identification_timeout=args.identification_timeout,
                       measurement_ttl=args.measurement_ttl,
                       resend_interval=args.resend_interval,
                       send_timeout=args.send_timeout)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("Interrupción Ctrl+C recibida; salida normal")
        return 0
    except OSError as exc:
        logging.error("No se pudo ejecutar el servidor: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
