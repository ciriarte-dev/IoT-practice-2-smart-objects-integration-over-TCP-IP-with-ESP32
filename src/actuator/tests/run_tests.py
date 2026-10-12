"""Pruebas del código C++ real con WiFi, socket, reloj y GPIO simulados."""
from pathlib import Path
import os
import subprocess
import tempfile

tests = Path(__file__).resolve().parent
actuator = tests.parent
sources = ["ActuatorClient.cpp", "ActuatorManager.cpp", "CommandParser.cpp", "Led.cpp", "main.cpp"]
with tempfile.TemporaryDirectory(prefix="actuator-tests-") as directory:
    binary = Path(directory) / "test_actuator"
    compiler = os.environ.get("CXX", "c++")
    flags = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-DACTUATOR_HOST_TEST",
             "-I", str(tests / "fakes"), "-I", str(actuator)]
    subprocess.run([compiler, *flags, str(tests / "test_actuator.cpp"),
                    *[str(actuator / name) for name in sources], "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # Conservar también la compilabilidad del modo SELF_TEST del compañero.
    subprocess.run([compiler, *flags, "-DSELF_TEST", "-c", str(actuator / "main.cpp"),
                    "-o", str(Path(directory) / "self_test.o")], check=True)
    print("SELF_TEST: compilacion de main.cpp aprobada")
