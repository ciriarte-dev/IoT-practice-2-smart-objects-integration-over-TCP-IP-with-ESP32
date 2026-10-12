# Sensor HC-SR04 — Etapa 1

Firmware Arduino para ESP32 DOIT DEVKIT V1. Solo mide y valida distancias,
y muestra el resultado por Serial. No incluye WiFi, TCP ni control de LEDs.

## Cableado

Realizar el cableado con la alimentacion desconectada.

| HC-SR04 | Conexion |
| --- | --- |
| VCC | 5 V |
| GND | GND comun con el ESP32 y el divisor |
| TRIG | D4 (GPIO 4) |
| ECHO | Entrada del divisor de tension; salida reducida a D2 (GPIO 2) |

No conectar ECHO de 5 V directamente al GPIO. Verificar que la salida del
divisor sea compatible con 3.3 V. GPIO 2 se configura como entrada, sin
pull-up interno; el divisor debe proporcionar tambien la conexion a GND.

GPIO 2 interviene en la seleccion del modo de arranque: debe estar bajo o
desconectado al entrar en el cargador serie. Si ECHO lo mantiene alto durante
el reinicio, puede impedir la carga. Ver la [documentacion de Espressif](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

## Implementacion

- `UltrasonicSensor.h/.cpp`: clase con pines configurables mediante constructor,
  inicializacion con `begin()` y lectura con `measure()`.
- `main.cpp`: usa TRIG 4 y ECHO 2, programa una lectura cada 500 ms mediante
  `millis()` y muestra la marca de tiempo de inicio de cada intento.
- Resultado: estado `Ok`, `EchoTimeout` u `OutOfRange`, distancia en cm y
  duracion del eco. Una distancia invalida es `NAN`, nunca una lectura previa
  ni una distancia cero presentada como valida.
- Pulso TRIG de 10 us, conversion `duracion_us / 58.0` y validacion del rango
  nominal inclusivo de 2 a 400 cm. Esto valida el sensor; no clasifica distancias
  para un algoritmo de control.
- `pulseIn()` tiene timeout de 30000 us. La medicion es sincronica con espera
  acotada, no completamente asincrona. Entre lecturas se cede CPU durante 1 ms.
- Cadencia nominal: dos intentos por segundo, tambien cuando hay errores.
  No se garantiza obtener dos distancias validas si no hay un eco util.
  Las marcas de tiempo pueden tener una pequena variacion de planificacion.
  La resta sin signo admite el desbordamiento de `millis()`.

Referencia: [ficha del HC-SR04](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf).

## Compilar, cargar y abrir el monitor

Desde la raiz del proyecto, en una terminal de PlatformIO:

```sh
pio run -e sensor
pio run -e sensor -t upload
pio device monitor -e sensor
```

Si `pio` no esta en el PATH, en este equipo se puede reemplazar por
`~/.platformio/penv/bin/pio`. El monitor usa 115200 baudios.
El entorno predeterminado es `sensor` y solo compila `src/sensor/`.
El filtro se configura segun la [documentacion de PlatformIO](https://docs.platformio.org/en/stable/projectconf/sections/env/options/build/build_src_filter.html).

Si hay varios dispositivos conectados, ejecutar `pio device list` y especificar
el puerto con `--upload-port PUERTO` al cargar y `--port PUERTO` en el monitor.
Cerrar cualquier otro monitor que este usando el mismo puerto. Si la carga
queda en `Connecting...`, mantener BOOT presionado hasta que comience la carga.

### Si falla la escritura de flash

El entorno `sensor` usa `upload_speed = 115200` para probar una conexion mas
estable que la carga a 460800 baudios. Es una medida de diagnostico, no una
solucion confirmada. `monitor_speed` controla el monitor, no la carga.

Si aparece `Failed to communicate with the flash chip` o
`Serial data stream stopped`, apagar y desconectar el sensor y el divisor
del ESP32. Probar la placa sola por USB, fuera de la protoboard si la usa,
con el monitor cerrado. Repetir `pio run -e sensor -t upload`.
Si persiste, probar otro cable de datos y otro puerto USB, evitando hubs.
Estas pruebas permiten investigar conexiones externas, alimentacion y enlace
serie; el mensaje no confirma por si solo que la flash este danada.
Referencia: [diagnostico de esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/troubleshooting.html).

Si la carga funciona con la placa sola, apagar antes de reconectar el circuito
y comprobar el cableado y divisor. Despues volver a probar el sensor.

## Prueba fisica

1. Revisar el divisor, los pines y GND comun; conectar el ESP32 por USB.
2. Compilar y cargar el entorno `sensor`. Abrir el monitor y pulsar EN/RESET
   para ver el mensaje inicial.
3. Colocar una superficie plana frente al sensor a 10, 20, 50 y 100 cm,
   medidos desde el frente de los transductores. Registrar unas diez lecturas
   por posicion y compararlas con una regla; no asumir precision solo porque
   aparecen dos decimales.
4. Comprobar que las marcas de inicio avanzan aproximadamente 500 ms, sin
   sumar a ese intervalo la duracion del eco. En 10 segundos deben aparecer
   aproximadamente 20 intentos.
5. Apuntar hacia un espacio libre sin un reflector cercano: debe aparecer
   timeout cuando no se recibe un pulso completo; pueden aparecer distancias
   si se detecta otra superficie.
6. Para una prueba controlada de ausencia de eco, apagar el circuito,
   desconectar ECHO del sensor de la entrada del divisor y conservar la
   resistencia del divisor entre GPIO 2 y GND. Encender: deben aparecer
   errores cada 500 ms, sin detenerse. Apagar, restaurar ECHO y encender:
   deben volver las distancias validas.
7. Acercar un objeto a menos de 2 cm puede producir `OutOfRange`, timeout o
   ecos espurios: la zona fuera de especificacion no permite garantizar una
   deteccion correcta. No confundir esta prueba con una calibracion.

Ejemplo ilustrativo de salida (no son mediciones realizadas):

```text
HC-SR04 | TRIG=4 | ECHO=2 | intervalo=500 ms
[500 ms] Distancia: 20.00 cm
[1000 ms] ERROR: sin pulso ECHO completo en 30 ms
[1500 ms] Distancia: 19.98 cm
```

## Resultados de verificacion

- Compilacion: el registro aportado por el usuario muestra un firmware
  generado, con 21472 bytes de RAM (6.6 %) y 270297 bytes de flash (20.6 %).
- Carga: el registro muestra deteccion del ESP32 y ejecucion del stub, pero
  falla al escribir la flash a 460800 baudios. Reintento a 115200 y prueba
  con la placa sola: pendientes; no se ha confirmado una carga exitosa.
- Pruebas fisicas, cadencia real y precision: pendientes en el ESP32 con sensor.
- No se han obtenido mediciones reales durante la implementacion.
