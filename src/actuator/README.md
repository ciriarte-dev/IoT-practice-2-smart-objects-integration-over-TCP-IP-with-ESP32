# ESP32 actuador — tres LEDs y cliente TCP

Firmware para ESP32 DOIT DEVKIT V1, framework Arduino, entorno PlatformIO
`actuator`. Se conservaron las clases del compañero y se adaptaron al contrato
de [server/README.md](../../server/README.md), sin modificar ese contrato.
El actuador no recibe distancias ni ejecuta su clasificación.

## Clases y archivos

| Clase/archivo | Función |
| --- | --- |
| `Led.h/.cpp` | Control individual y parpadeo mediante millis(); conservados sin cambios. |
| `ActuatorManager.h/.cpp` | Exclusión entre LEDs, comando ALL_OFF y apagado ante comandos inválidos. |
| `CommandParser.h/.cpp` | Reconocimiento exacto de los cuatro comandos, sin recortar espacios ni normalizar mayúsculas. |
| `ActuatorClient.h/.cpp` | WiFi, TCP, identificación, recepción, reconexión y temporizador de seguridad. |
| `ActuatorConfig.h` | GPIO, tiempos, límite de líneas y presupuesto de recepción. |
| `main.cpp` | Inicialización apagada y actualización de red y LEDs; conserva el modo SELF_TEST. |
| `secrets.example.h` | Plantilla sin credenciales reales. |
| `tests/` | Pruebas C++ con hardware y red simulados. |

Los pines originales no se cambiaron: rojo GPIO 25, amarillo GPIO 26, verde
GPIO 27. Las salidas son activas en HIGH; el cableado previsto usa una resistencia
de 220 ohm por LED hacia GND, con masa común. El cableado físico no se verificó
en esta entrega.

## Protocolo y comportamiento

Al conectar TCP se envía exclusivamente `HELLO ACTUATOR\n` y se espera `OK\n`.
En esta documentación `\n` representa un LF real; no se utiliza CRLF.
Antes de OK ningún comando puede activar un LED. Una respuesta de identificación
inesperada apaga y cierra la sesión para reintentar.

| Mensaje del servidor, terminado en LF | Resultado |
| --- | --- |
| `LED RED BLINK` | Solo rojo: 500 ms encendido y 500 ms apagado. |
| `LED YELLOW ON` | Solo amarillo encendido. |
| `LED GREEN ON` | Solo verde encendido. |
| `LED ALL OFF` | Todos apagados; cancela el parpadeo. |

Repetir LED RED BLINK no reinicia su fase. Un comando válido repetido sí renueva
el timeout de comunicación. Una línea inválida apaga inmediatamente y no renueva
ese timeout; un comando válido posterior puede volver a activar los LEDs.
No se envían respuestas OK/ERR ni confirmaciones por comando: la única salida
de aplicación del actuador es HELLO al abrir cada sesión.

La recepción admite líneas fragmentadas y varias líneas juntas. Conserva como
máximo 128 bytes de contenido, más el terminador interno C. Al recibir el byte
129 sin LF, un byte no ASCII o un control como CR/NUL, apaga y cierra la sesión.
Una línea vacía también es inválida. Los bytes parciales no renuevan el timeout.

## Apagado y reconexión

- Todos los LEDs se inicializan apagados antes de conectar a la red.
- Perder WiFi o detectar el cierre TCP apaga, descarta el fragmento recibido y
  cancela la sesión anterior.
- A los 3000 ms sin un comando válido se apaga y cierra TCP. Tras OK se concede
  el mismo plazo inicial, siempre con LEDs apagados hasta recibir un comando.
- La comprobación de seguridad se hace antes de consumir datos pendientes.
- WiFi y TCP reintentan cada 2000 ms. Una reconexión repite HELLO/OK y nunca
  restaura el estado anterior.
- Espera de identificación: máximo 5000 ms desde la conexión TCP.
- Envío de HELLO: máximo 200 ms; soporta escrituras parciales sin repetir bytes.

WiFiClient mantiene la conexión. HELLO se escribe en su socket con
`send(..., MSG_DONTWAIT)` para evitar los reintentos bloqueantes de write().
La conexión a una IPv4 numérica tiene timeout de 100 ms y ocurre con los LEDs
apagados. Se procesan como máximo 256 bytes de entrada por iteración, y loop()
cede CPU durante 1 ms. No se utilizan esperas de 500 ms para hacer parpadear.
Los tiempos reales, incluidos los efectos de Serial y la planificación del ESP32,
siguen pendientes de comprobación física; no es una garantía de tiempo real estricto.

## Configuración local

Ya se creó `src/actuator/secrets.h` local con valores vacíos. Está ignorado por
Git mediante una regla existente, verificada; no fue necesario cambiar .gitignore.
En otro equipo, copiar secrets.example.h a secrets.h si este último no existe.
Completar solo el archivo local:

- `WIFI_SSID`: nombre de la red WiFi.
- `WIFI_PASSWORD`: contraseña, sin colocarla en la plantilla versionada.
- `SERVER_IP`: IPv4 real del Mac servidor en la misma red.
- `SERVER_PORT`: `5000`.

No usar 0.0.0.0 ni 127.0.0.1 como destino del ESP32. El marcador 0.0.0.0 o un
SSID vacío deshabilitan la red y mantienen el estado seguro. Las contraseñas no
se imprimen en Serial. Las credenciales reales que se configuren quedarán en el
binario compilado; no compartir ese binario como si fuera la plantilla vacía.

## Compilar y cargar

Desde la raíz del proyecto:

```sh
pio run -e actuator
pio run -e actuator -t upload
pio device monitor -e actuator
```

El monitor funciona a 115200 baudios. Si pio no está en PATH en este Mac,
reemplazarlo por `~/.platformio/penv/bin/pio`.
Si hay varios ESP32 conectados, identificar el puerto con `pio device list` y
usar `--upload-port PUERTO` al cargar y `--port PUERTO` en el monitor.
Estas instrucciones se entregan para la siguiente etapa: no se cargó firmware
ni se iniciaron pruebas físicas de integración.

`SELF_TEST` se conserva como modo de demostración local: recorre los cuatro
comandos y uno inválido cada 4 segundos, sin cliente de red. No está activado
en el entorno actuator normal y no sirve para certificar los timeouts TCP.

## Verificación realizada

Ejecutar las pruebas simuladas desde la raíz, con Python 3 y compilador C++:

```sh
python3 -B src/actuator/tests/run_tests.py
```

**24 pruebas aprobadas, 0 fallos**, compiladas con Apple Clang, C++11 y
`-Wall -Wextra -Werror`. Se compilan los archivos reales de las cuatro clases
y main.cpp usando dobles de GPIO, reloj, WiFiClient y socket. Cubren:

- Parser exacto, todos los comandos, apagado y exclusión entre LEDs.
- Pines de arranque, semiperiodos de 500 ms y repetición sin reiniciar la fase.
- Espera de OK, fragmentación, líneas múltiples y ausencia de respuestas extra.
- Desconexión WiFi/TCP, reintentos y descarte de fragmentos tras reconexión.
- Timeout de 3 s, renovación solo por comandos válidos y timeout de identificación.
- Líneas inválidas, de 128 bytes, excesivas, CRLF, NUL y bytes no ASCII.
- HELLO parcial, errores/atasco de escritura y configuración vacía.
- Desbordamiento de millis() y presupuesto de lectura por iteración.

También se verificó la compilación nativa de main.cpp con SELF_TEST.
Las pruebas solo se activan con ACTUATOR_HOST_TEST; el archivo de pruebas queda
vacío al compilar para ESP32. No se modificó platformio.ini.

Compilación final `pio run -e actuator`: **SUCCESS**, Espressif32 7.1.0,
Arduino ESP32 2.0.17. RAM: 44952 bytes (13.7 %). Flash: 746581 bytes (57.0 %).
Se compiló con configuración local vacía, sin credenciales reales.

La primera compilación detectó una colisión de DISABLED con una macro del core
ESP32. Se corrigió renombrando el estado CONFIG_MISSING y se añadió esa macro
al entorno de pruebas para detectar regresiones.

## Pendientes

Configurar la red real, cargar el firmware y verificar cableado, LEDs y tiempos
en los dos ESP32 junto al servidor. Los dobles de GPIO/socket no certifican el
estado eléctrico ni la comunicación WiFi real. La integración física no se
ejecutó y requiere autorización. No se crearon commits ni se hizo merge.
