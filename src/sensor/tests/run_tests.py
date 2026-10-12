"""Compila el código real del sensor con Arduino/WiFi simulados, sin ESP32."""
from pathlib import Path
import os
import subprocess
import tempfile

tests = Path(__file__).resolve().parent
sensor = tests.parent
with tempfile.TemporaryDirectory(prefix="sensor-tests-") as directory:
    binary = Path(directory) / "test_sensor"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-DSENSOR_HOST_TEST", "-I", str(tests / "fakes"), "-I", str(sensor),
        str(tests / "test_sensor.cpp"), str(sensor / "SensorClient.cpp"),
        str(sensor / "UltrasonicSensor.cpp"), str(sensor / "main.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
