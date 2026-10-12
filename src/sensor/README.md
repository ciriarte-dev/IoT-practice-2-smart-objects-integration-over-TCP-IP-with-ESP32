# Sensor HC-SR04 — Medición y cliente WiFi/TCP

Firmware Arduino para ESP32 DOIT DEVKIT V1. Mide y valida distancias, muestra
el resultado por Serial y envía las lecturas por TCP. No clasifica distancias
ni controla LEDs. Conserva la clase UltrasonicSensor original sin cambios.
El contrato es [server/README.md](../../server/README.md).

## Cableado

Realizar el cableado con la alimentacion desconectada.

| HC-SR04 | Conexion |
| --- | --- |
| VCC | 5 V |
| GND | GND comun con el ESP32 y el divisor |
| TRIG | GPIO 32 |
| ECHO | Entrada del divisor de tension; salida reducida a GPIO 35 |

No conectar ECHO de 5 V directamente al GPIO. Verificar que la salida del
divisor sea compatible con 3.3 V. GPIO 35 se configura como entrada, sin
pull-up interno; el divisor debe proporcionar tambien la conexion a GND.

Esta configuración reemplaza el cableado anterior D4/D2. Cambiar también los
cables físicos antes de usar este firmware.

## Implementacion

- `UltrasonicSensor.h/.cpp`: clase con pines configurables mediante constructor,
  inicializacion con `begin()` y lectura con `measure()`.
- `main.cpp`: usa TRIG 32 y ECHO 35, programa una lectura cada 500 ms mediante
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

## Configuración WiFi y servidor

`secrets.example.h` contiene únicamente valores vacíos/de ejemplo. Copiarlo a
`src/sensor/secrets.h` si ese archivo local no existe. En este equipo ya se creó
con valores vacíos. Editar únicamente el archivo local:

- `SENSOR_WIFI_SSID`: nombre de la red WiFi.
- `SENSOR_WIFI_PASSWORD`: contraseña de esa red.
- `SENSOR_SERVER_IP`: IPv4 real del equipo servidor en la red local.
- `SENSOR_SERVER_PORT`: mantener `8080`.

No usar `0.0.0.0` ni `127.0.0.1` como destino del ESP32. El valor `0.0.0.0`
del ejemplo es un marcador que deshabilita la red, no una dirección operativa.
Sin SSID o sin una IPv4 válida distinta de cero, solo se ejecuta la lectura local
y aparece `[CONFIG]` en Serial. No hace falta una contraseña no vacía para una
red abierta. Los registros del sensor no imprimen la contraseña.

`.gitignore` excluye `src/sensor/secrets.h` y `src/actuator/secrets.h`; la plantilla
sí se versiona. No compartir firmware compilado con credenciales reales, ya que
estas quedan incorporadas en el binario. No se requieren bibliotecas externas:
WiFi.h/WiFiClient pertenecen al framework Arduino ESP32 instalado.

## Cliente TCP y temporización

`SensorClient` implementa los estados de configuración deshabilitada, espera
WiFi, espera TCP, envío de HELLO, espera de OK y sesión lista. `SensorConfig.h`
centraliza pines, intervalos y límites.

1. Iniciar WiFi y reintentar cada 2000 ms mientras no se conecte.
2. Abrir TCP al servidor y enviar exactamente `HELLO SENSOR\n`.
3. Esperar exactamente `OK\n`; aceptar que llegue dividido en varias recepciones.
4. Enviar solo las mediciones nuevas obtenidas después de OK: `DIST 25.50\n`
   o `INVALID\n`. La salida es ASCII, con LF, sin CR ni unidades adicionales.
5. Al desconectarse, descartar buffers y cualquier trama pendiente; reconectar
   después de 2000 ms y repetir HELLO/OK. Nunca reproducir lecturas anteriores.

La medición local continúa también sin WiFi/TCP y mientras se espera OK. Las
lecturas fuera de una sesión lista se descartan para la transmisión. La distancia
se valida otra vez antes de enviarla: estado Ok, valor finito e intervalo 2–400 cm.
Todo fallo produce INVALID, sin transmitir NaN, infinitos ni valores fuera de rango.

| Operación | Límite/configuración |
| --- | --- |
| Intentos de medición | 500 ms mediante millis() |
| Espera de eco | 30 ms; clase original |
| Intento de conexión TCP a IPv4 | Timeout de 100 ms; no se usa resolución DNS |
| Espera de OK | 5000 ms desde la conexión TCP |
| Trama de salida pendiente | Una sola trama, máximo 200 ms |
| Recepción por iteración | Máximo 256 bytes |
| Contenido por línea | Máximo 128 bytes, sin contar LF |

El socket pertenece a WiFiClient. El envío usa `send(..., MSG_DONTWAIT)` sobre
su descriptor para evitar los reintentos bloqueantes de WiFiClient::write en
el framework instalado. Conserva el desplazamiento en escrituras parciales;
si el envío no avanza dentro del plazo, cierra la sesión y descarta la trama.
`[TX]` indica entrega completa al socket local, no confirmación de recepción
por el servidor; el contrato no incluye confirmaciones por medición.

Ante ERROR, respuesta inesperada, byte de control/no ASCII o línea excesiva,
el cliente registra el problema y vuelve a conectar. No acepta OK con espacios,
en minúsculas, con CRLF ni un OK repetido durante la sesión.

La solución tiene esperas acotadas, no es completamente asíncrona: el intento
TCP puede ocupar hasta 100 ms y pulseIn hasta 30 ms, además de la planificación
del sistema y las llamadas WiFi. La cadencia real debe medirse en hardware,
especialmente durante reconexiones; no se garantiza tiempo real estricto.

**Estado de integración:** el servidor del repositorio aún implementa únicamente
el transporte y no responde OK. Con ese servidor el sensor enviará HELLO y
reintentará al vencer los 5 s, sin enviar distancias. Completar el servidor es
el siguiente componente y requiere autorización; no se modificó en esta entrega.

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
   resistencia del divisor entre GPIO 35 y GND. Encender: deben aparecer
   errores cada 500 ms, sin detenerse. Apagar, restaurar ECHO y encender:
   deben volver las distancias validas.
7. Acercar un objeto a menos de 2 cm puede producir `OutOfRange`, timeout o
   ecos espurios: la zona fuera de especificacion no permite garantizar una
   deteccion correcta. No confundir esta prueba con una calibracion.

Ejemplo ilustrativo de salida (no son mediciones realizadas):

```text
HC-SR04 | TRIG=32 | ECHO=35 | intervalo=500 ms
[500 ms] Distancia: 20.00 cm
[1000 ms] ERROR: sin pulso ECHO completo en 30 ms
[1500 ms] Distancia: 19.98 cm
```

## Pruebas de red pendientes en hardware

Cuando el servidor implemente el contrato, configurar ambos equipos en una red
que permita su comunicación y comprobar:

1. Se registra WiFi conectado, TCP conectado, TX HELLO y OK recibido, en ese orden.
   No debe aparecer TX DIST/INVALID antes de OK, aunque sí lecturas locales.
2. Una superficie conocida produce TX DIST; ausencia de eco produce TX INVALID.
3. El servidor recibe aproximadamente dos mensajes por segundo. Medir la cadencia
   también con fallos y reintentos, sin inferirla solamente de la configuración.
4. Apagar WiFi o cerrar el servidor produce reconexión. Después del siguiente OK
   solo debe enviarse la lectura actual; cambiar el objeto durante la desconexión
   ayuda a comprobarlo.
5. Un servidor de prueba que omita OK debe provocar timeout; si divide OK en
   fragmentos, no debe habilitarse el envío hasta recibir el LF.

## Pruebas automatizadas y resultados

Desde la raíz, con Python 3 y un compilador C++ (Apple Clang en este Mac):

```sh
python3 -B src/sensor/tests/run_tests.py
```

El script compila los archivos reales SensorClient.cpp, UltrasonicSensor.cpp y
main.cpp contra dobles de Arduino, WiFiClient, reloj y socket. El ejecutable se
crea en una carpeta temporal. Las pruebas C++ quedan vacías en la compilación
ESP32 porque solo se activan con SENSOR_HOST_TEST; no alteran platformio.ini.

Resultado: **15 pruebas simuladas aprobadas, 0 fallos**. Cubren medición y timeout,
pines, intervalo local de 500 ms, espera y fragmentación de OK, formatos y límites,
INVALID, rechazo de respuestas incorrectas, reconexión TCP/WiFi, timeout de
identificación, escrituras parciales, error/atasco de envío, descarte de datos
anteriores, desbordamiento de millis() y configuración vacía.

Compilación `pio run -e sensor`: **SUCCESS**, Espressif32 7.1.0 y Arduino ESP32
2.0.17. RAM: 45024 bytes (13.7 %); flash: 746465 bytes (57.0 %). Se usó la
configuración local vacía; no se introdujeron credenciales reales.

La clase UltrasonicSensor no se modificó. Sus pruebas inyectan pulsos simulados:
no certifican precisión, voltajes ni señales eléctricas. Tampoco los dobles de
WiFi/socket prueban el enlace inalámbrico o los tiempos reales del framework.
No se cargó este firmware ni se realizaron pruebas físicas o de integración
completa con los dos ESP32. Estas verificaciones siguen pendientes.
