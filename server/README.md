# Servidor TCP — Práctica 2 de IoT

## Estado y alcance

Servidor de integración implementado en `feature/integracion`, con Python 3
(3.7 o posterior) y biblioteca estándar. Conserva la infraestructura `socket` /
`selectors` y aplica el contrato aprobado que se reproduce abajo sin cambios.

Implementado: identificación SENSOR/ACTUATOR, validación de líneas y distancias,
clasificación, comandos, caducidad, reenvío periódico, limpieza de sesiones y
aceptación de reconexiones. Los registros muestran conexiones, identificaciones,
mediciones, cambios de estado, caducidad, errores y desconexiones.

Se ha verificado con clientes TCP locales y pruebas controladas de escritura.
No se ha probado físicamente con los ESP32. El firmware del actuador todavía
requiere las correcciones aprobadas de integración: completar el servidor no
hace compatible por sí solo su parser ni añade su timeout local de apagado.

## Responsabilidades

| Componente | Responsabilidad |
| --- | --- |
| ESP32 SENSOR | Medir, validar el HC-SR04 y enviar una distancia o lectura inválida cada 500 ms. No clasificar ni controlar LEDs. |
| Servidor Python | Identificar clientes, validar mensajes, clasificar distancias y enviar el estado completo de los LEDs. |
| ESP32 ACTUATOR | Ejecutar comandos, temporizar el parpadeo y apagar por pérdida de comunicación. No clasificar distancias. |

El apagado local por pérdida de comunicación es una protección del actuador,
no una clasificación de distancias. El servidor no puede garantizar un apagado
físico enviando mensajes por una conexión que ya se perdió.

## Transporte y delimitación

- TCP, con una conexión persistente independiente para cada ESP32.
- Texto ASCII, sensible a mayúsculas, con un mensaje por línea.
- Delimitador: un byte LF (`0x0A`). En los ejemplos, `\n` representa ese byte real,
  no los caracteres barra invertida y letra n. No se utiliza CRLF.
- Máximo: 128 bytes de contenido por mensaje, sin contar LF.
- Un espacio ASCII entre campos; sin espacios iniciales o finales, tabulaciones,
  líneas vacías ni campos adicionales.
- No se presupone que una llamada a `recv()` contenga exactamente un mensaje.
  Cada conexión necesita su propio buffer: conservar fragmentos hasta LF y
  extraer todas las líneas completas, en orden.
- Descartar cualquier fragmento pendiente al cerrar la conexión. Nunca unirlo
  con datos de una conexión nueva.
- Si una línea supera el máximo o contiene bytes no ASCII, cerrar la conexión
  tras intentar enviar el error correspondiente. Limitar también la cola de
  salida para que un cliente lento no consuma memoria indefinidamente.

Ejemplos de recepción:

```text
recv 1: DIST 2
recv 2: 5.50\nINVALID\n
Resultado: procesar DIST 25.50 y después INVALID.
```

Un mensaje incompleto no actualiza la vigencia de la medición. Si la conexión
se pierde antes del LF, ese mensaje no se procesa.

## Identificación y clientes

El primer mensaje debe ser uno de los siguientes:

| Cliente → servidor | Respuesta del servidor |
| --- | --- |
| `HELLO SENSOR\n` | `OK\n` |
| `HELLO ACTUATOR\n` | `OK\n` |

El cliente debe esperar `OK` antes de enviar datos de aplicación. El servidor
admite simultáneamente un SENSOR y un ACTUATOR; el orden de conexión es libre.
El rol queda asociado al socket hasta su desconexión. No se permite cambiarlo
ni volver a identificarse en la misma conexión.

- Identificación ausente o incompleta durante 5 segundos: cerrar la conexión.
- Rol desconocido: `ERROR ROLE\n` y cierre.
- Rol ya ocupado: `ERROR DUPLICATE\n` y cierre de la conexión nueva; mantener
  la anterior. Una conexión TCP aparentemente abierta puede tardar en revelar
  una interrupción de red; un rechazo por duplicado debe permitir reintentar.
- Mensaje distinto de HELLO antes de identificarse: `ERROR IDENTIFICATION\n`
  y cierre.

La identificación declara el rol; no autentica al dispositivo. El alcance de
esta práctica es una red local de laboratorio, sin TLS ni credenciales de cliente.

## Mediciones del sensor

| SENSOR → servidor | Significado |
| --- | --- |
| `DIST 25.50\n` | Distancia válida de 25.50 cm. |
| `INVALID\n` | Ausencia de eco o cualquier medición inválida. |

El sensor enviará un mensaje cada 500 ms, incluido `INVALID` cuando falle la
lectura. No enviará sus líneas de diagnóstico Serial ni una distancia anterior
como sustituto de una lectura fallida.

La gramática del número es `[0-9]+(\.[0-9]+)?`: entero o decimal con punto.
Se acepta, por ejemplo, `20`, `20.0` y `19.99`. No se aceptan signos, coma decimal,
notación científica, `NaN`, infinitos ni sufijos como `cm`.

El valor debe ser finito y estar entre **2 y 400 cm, inclusive**, coherente con
la validación del sensor existente. No redondear antes de clasificar; se propone
usar `decimal.Decimal`, de la biblioteca estándar, para preservar los límites
decimales del protocolo. Los límites de validez del sensor son distintos de los
umbrales de control de 20 y 40 cm.

- `DIST` correcto: actualizar la medición vigente y su instante de recepción.
- `INVALID`: invalidar inmediatamente la medición anterior y ordenar apagado.
- Mensaje mal formado del SENSOR, número no admitido o fuera de rango:
  invalidar la medición anterior, ordenar apagado y responder con un error.
  Nunca mantener el último color como respuesta a una lectura inválida.
- No hay confirmación `OK` por medición; `OK` se reserva para la identificación.
- Los mensajes del ACTUATOR no pueden modificar la distancia ni su vigencia.

## Clasificación y comandos

| Entrada del sensor | Servidor → ACTUATOR | Estado completo |
| --- | --- | --- |
| Distancia válida < 20 cm | `LED RED BLINK\n` | Rojo parpadea; amarillo y verde apagados. |
| Distancia válida >= 20 y <= 40 cm | `LED YELLOW ON\n` | Solo amarillo encendido. |
| Distancia válida > 40 cm | `LED GREEN ON\n` | Solo verde encendido. |
| Lectura inválida, vencida o sensor desconectado | `LED ALL OFF\n` | Todos apagados. |

Cada comando reemplaza el estado completo, para que no queden LEDs encendidos
por un comando anterior. El actuador no necesita enviar confirmaciones de estos
comandos en esta versión del protocolo.

Para `LED RED BLINK`, el actuador alternará 500 ms encendido y 500 ms apagado,
comenzando encendido al entrar en ese estado. Recibir de nuevo el mismo comando
**no reinicia la fase del parpadeo**; sí renueva el temporizador de comunicación.
Cambiar a otro estado cancela el parpadeo.

## Vigencia, desconexión y apagado seguro

Valores iniciales propuestos para la implementación, configurables:

| Parámetro | Valor | Responsable |
| --- | --- | --- |
| Intervalo de envío de mediciones | 500 ms | Sensor |
| Plazo de identificación | 5 s | Servidor |
| Caducidad de la última distancia válida | 2 s | Servidor |
| Reenvío periódico del estado de LEDs | 500 ms | Servidor |
| Plazo sin comandos válidos antes de apagar | 3 s | Actuador |
| Intervalo de reintento de conexión | 2 s | Ambos ESP32 |

Usar un reloj monotónico para los plazos. Al alcanzar los 2 segundos sin una
distancia válida nueva, el estado debe pasar a apagado. Mensajes parciales,
errores e identificaciones no renuevan ese plazo.

El servidor enviará el estado tras procesar una nueva medición o una condición
de apagado, y lo repetirá cada 500 ms. Antes de cada envío verificará la vigencia;
el reenvío periódico no convierte una lectura antigua en nueva.

| Suceso | Comportamiento |
| --- | --- |
| Arranque sin sensor o sin lectura | Estado apagado. |
| Desconexión detectada del sensor | Eliminar su sesión y medición; enviar apagado al actuador conectado. |
| Sensor silencioso con TCP abierto | Apagar al vencer la lectura, aunque aún no se detecte la pérdida de red. |
| Desconexión del actuador | Eliminar su sesión y su cola de salida; mantener el servidor atendiendo al sensor. |
| Conexión o reconexión del actuador | Enviar `OK` seguido de `LED ALL OFF`; esperar una nueva distancia válida recibida después de esa identificación. |
| Reconexión del sensor | Exigir HELLO de nuevo; no recuperar su medición anterior. |
| Caída del servidor o pérdida de enlace con el actuador | El actuador apaga al detectar desconexión o al vencer sus 3 segundos sin comandos válidos. |
| Cierre voluntario del servidor | Intentar enviar apagado y cerrar sockets; no asumir que el mensaje fue recibido. |

Al reiniciarse, el actuador debe comenzar apagado. Solo un comando completo y
válido renueva su temporizador; bytes sueltos o `ERROR` no lo hacen. Ante un comando
de actuación desconocido o mal formado debe apagar.

Son los ESP32 quienes vuelven a conectar al servidor, reintentan cada 2 segundos
y repiten la identificación. El servidor acepta nuevas conexiones y libera las
sesiones cuando detecta su cierre o un error. No acumular un historial de
mediciones ni comandos para reproducirlo después de una reconexión.

Si un cliente no consume la salida, el servidor deberá acotar el buffer y el
tiempo de envío y cerrar esa sesión al excederlos. Un envío parcial requiere
conservar su desplazamiento; no intercalar bytes de otros comandos. Estos
mecanismos se implementarán sin bloquear la atención de los demás clientes.

## Errores del protocolo

| Respuesta | Causa | Acción sobre la conexión |
| --- | --- | --- |
| `ERROR IDENTIFICATION\n` | Falta identificación inicial. | Cerrar. |
| `ERROR ROLE\n` | Rol desconocido. | Cerrar. |
| `ERROR DUPLICATE\n` | Rol ya conectado. | Cerrar la conexión nueva. |
| `ERROR FORMAT\n` | Sintaxis incorrecta o mensaje no permitido para ese rol. | Mantener, salvo desbordamiento o error de transporte. |
| `ERROR VALUE\n` | DIST tiene un campo numérico no admitido o fuera de rango. | Mantener. |
| `ERROR TOO_LONG\n` | Línea mayor de 128 bytes antes de LF. | Cerrar. |
| `ERROR ENCODING\n` | Bytes fuera de ASCII. | Cerrar. |
| `ERROR TIMEOUT\n` | Venció el plazo de identificación. | Cerrar. |

La estructura de DIST debe tener exactamente dos campos; en caso contrario se
usa FORMAT. Si tiene dos campos pero el número no cumple las reglas, se usa VALUE.
Un error originado por el SENSOR invalida su lectura vigente. Un cliente ajeno
o un duplicado rechazado no invalida la lectura del SENSOR ya identificado.

Enviar ERROR antes del cierre es un intento de diagnóstico: puede fallar si el
cliente ya se desconectó. Las excepciones de una sesión no deben cerrar el servidor.

## Ejemplo de intercambio

SENSOR y ACTUATOR mantienen conexiones diferentes; las flechas no se transmiten.

```text
ACTUATOR -> servidor: HELLO ACTUATOR\n
servidor -> ACTUATOR: OK\n
servidor -> ACTUATOR: LED ALL OFF\n

SENSOR -> servidor: HELLO SENSOR\n
servidor -> SENSOR: OK\n
SENSOR -> servidor: DIST 19.99\n
servidor -> ACTUATOR: LED RED BLINK\n
SENSOR -> servidor: DIST 20\n
servidor -> ACTUATOR: LED YELLOW ON\n
SENSOR -> servidor: DIST 40.01\n
servidor -> ACTUATOR: LED GREEN ON\n
SENSOR -> servidor: INVALID\n
servidor -> ACTUATOR: LED ALL OFF\n
```

## Arquitectura implementada

```text
ESP32 SENSOR -- HELLO / DIST / INVALID --> TCPServer
                                           |
                                    ProtocolHandler
                                           |
                                    DistanceController
                                           |
ESP32 ACTUATOR <-- OK / LED ... ------------+
```

| Clase | Archivo | Responsabilidad |
| --- | --- | --- |
| `TCPServer` | `tcp_server.py` | Sockets no bloqueantes, selector, registro de roles, eventos, temporizadores y cierre. |
| `ClientConnection` | `tcp_server.py` | Rol, fragmento de entrada, cola independiente y estado de cierre de cada conexión. |
| `OutgoingMessage` | `tcp_server.py` | Bytes, desplazamiento parcial, fecha de encolado y caducidad del comando. |
| `ProtocolHandler` | `protocol.py` | ASCII/LF, límite de 128 bytes, identificación, números y errores del contrato. |
| `ProtocolError` | `protocol.py` | Código de error y decisión de cerrar después de intentar responder. |
| `DistanceController` | `controller.py` | Clasificación exacta, vigencia y apagado, sin dependencia de sockets. |

La gestión de clientes sigue dentro de TCPServer; no es necesaria una clase
ClientManager adicional para estos dos roles. `decimal.Decimal` conserva los
límites decimales sin redondear antes de clasificar. No se convierte a float.

`start()`, `poll_once()` y `close()` permiten controlar el ciclo en las pruebas;
`serve_forever()` libera recursos al salir. Todos se usan desde un único hilo.
El reloj es monotónico y se puede sustituir en pruebas. El tiempo de espera del
selector se ajusta al próximo plazo, además de revisar los temporizadores antes
de escribir y después de procesar eventos.

### Envíos y clientes lentos

- Cada conexión conserva su propio fragmento de entrada, hasta 128 bytes, y
  procesa hasta 4096 bytes recibidos por evento. No acumula un historial de lecturas.
- Se registra EVENT_WRITE solo cuando hay salida pendiente. Cada evento intenta
  escribir una trama; una escritura parcial conserva su desplazamiento.
- La cola de salida admite por defecto 1024 bytes pendientes y cada trama dispone
  de 1 segundo desde que se encola. El avance parcial no reinicia ese plazo.
  Al exceder cualquiera de esos límites se cierra únicamente esa sesión.
- Una orden de apagado retira colores todavía no enviados. Si una trama de color
  ya se envió parcialmente, se cierra la conexión en vez de intercalar otra orden
  dentro de ella. El actuador debe aplicar su apagado por desconexión/timeout.
- Una trama caducada no empieza a enviarse. Un OFF pendiente se conserva sin
  reiniciar su plazo en cada reenvío; así un cliente lento tampoco puede mantener
  indefinidamente una cola de apagado sin consumirla.
- En la reconexión del actuador se envían OK y OFF, y se invalida el estado anterior
  hasta recibir otra distancia válida. Los buffers de sesiones cerradas se borran.
- Ctrl+C intenta enviar OFF sin bloquear y luego cierra todos los sockets. Ese
  intento no equivale a confirmación del apagado físico.

Los límites de cola y tiempo de envío concretan la protección de transporte ya
prevista en el contrato; no añaden ni cambian mensajes de aplicación.
Las respuestas ERROR se intentan enviar antes de un cierre por protocolo, pero
un fallo de transporte puede impedir su entrega.

## Configuración y ejecución

Desde la raíz del proyecto:

```sh
python3 server/main.py --host 0.0.0.0 --port 5000
```

También funcionan `python3 server/main.py` y `python3 -m server.main`.
Para revisar los argumentos: `python3 server/main.py --help`.
No se instalan dependencias externas. Para detenerlo, usar Ctrl+C.

| Argumento | Valor predeterminado | Uso |
| --- | --- | --- |
| `--host` | `0.0.0.0` | Escucha IPv4; `127.0.0.1` permite pruebas solo locales. |
| `--port` | `5000` | Puerto TCP; 0 pide un puerto libre para pruebas. |
| `--identification-timeout` | `5.0` | Segundos máximos antes de identificarse. |
| `--measurement-ttl` | `2.0` | Segundos de vigencia de la última lectura válida. |
| `--resend-interval` | `0.5` | Segundos entre reenvíos al actuador. |
| `--send-timeout` | `1.0` | Segundos máximos de permanencia de una trama en la cola. |

Mantener los valores acordados de 5/2/0.5 segundos en la integración con los ESP32.
Los argumentos temporales aceptan solo números positivos y finitos. El constructor
permite ajustar `max_pending_bytes`, con un mínimo de 32 bytes.

El servidor sale con código 0 ante Ctrl+C, 1 ante errores del sistema como puerto
ocupado y 2 ante argumentos inválidos. Si 5000 está ocupado, identificar el servicio
que lo utiliza o coordinar otro puerto en los tres componentes.

Los ESP32 deben usar la IPv4 real del Mac en su red local y el puerto 5000.
`0.0.0.0` sirve para escuchar y no es una dirección de destino. Verificar que el
firewall permita conexiones entrantes a Python y que la red no aísle los clientes.
No se han verificado la red, firewall o dispositivos físicos en esta entrega.

### Prueba manual con clientes locales

Con el servidor activo, abrir dos terminales y ejecutar en cada una:

```sh
nc 127.0.0.1 5000
```

Identificarse antes de 5 segundos. En una terminal escribir `HELLO ACTUATOR` y
pulsar Enter: debe responder OK y LED ALL OFF. En la otra escribir HELLO SENSOR,
esperar OK y enviar DIST 10, DIST 20, DIST 40.01 o INVALID, cada uno con Enter.
La terminal del actuador debe mostrar el comando correspondiente. Si no llega
otra lectura válida en 2 segundos, debe volver a OFF. Los comandos se repiten
cada 500 ms. Estas terminales no controlan LEDs físicos.

## Pruebas automatizadas y resultados

Desde la raíz:

```sh
python3 -B -W error::ResourceWarning -m unittest discover -s server/tests -v
```

Resultado: **36 pruebas aprobadas, 0 fallos, 0 omisiones**, Python 3.14.5 sobre
macOS. Última ejecución: 0.272 s. La suite usa puertos libres en 127.0.0.1,
clientes TCP locales y un reloj controlado para probar los plazos sin esperas.
Los envíos parciales y la contrapresión se fuerzan mediante dobles de `send()`
sobre las sesiones de prueba; no representan una medición de congestión WiFi.
La prueba de Ctrl+C envía SIGINT a un proceso real del servidor.

| Caso | Resultado |
| --- | --- |
| Identificación de ambos roles y conexiones simultáneas | Aprobado. |
| 10 y 19.99 cm | LED RED BLINK. |
| 20, 30 y 40 cm | LED YELLOW ON. |
| 40.01 y 50 cm | LED GREEN ON. |
| Valores 2 y 400 cm y decimales muy próximos a los umbrales | Aprobado, sin redondeo previo. |
| INVALID, valores fuera de rango, NaN, formatos incorrectos | OFF y error cuando corresponde. |
| Desconexión y reconexión del sensor | OFF y nueva identificación. |
| Caducidad a los 2 s y reenvío cada 500 ms | Aprobado. |
| Reconexión del actuador con lectura anterior aún vigente | OK/OFF y espera de nueva lectura. |
| Líneas fragmentadas y varias líneas en una recepción | Aprobado. |
| Roles desconocidos/duplicados, identificación ausente o vencida | Error y cierre según el contrato. |
| Línea de exactamente 128 bytes, línea excesiva y no ASCII | Aprobado. |
| Actuador intentando enviar distancias | ERROR FORMAT, sin alterar la lectura del sensor. |
| Escrituras parciales, cola llena y timeout de salida | Aprobado. |
| Color pendiente invalidado o trama parcial obsoleta | Retirada o cierre de sesión, sin concatenar tramas corruptas. |
| Puerto ocupado, argumentos inválidos, cierre repetido y Ctrl+C | Aprobado. |

Las pruebas básicas anteriores se conservaron y adaptaron donde cambió el alcance:
HELLO ahora debe recibir respuesta y las líneas completas se procesan, en lugar
de permanecer indefinidamente en el buffer. Las pruebas de reset TCP mediante
SO_LINGER y de SIGINT se omiten en Windows por diferencias de plataforma.

El primer intento dentro del sandbox fue bloqueado al abrir puertos locales.
La ejecución final con permiso para sockets locales pasó completa.

## Pendientes de integración física

- Adaptar el firmware del actuador al contrato: comandos completos, espera de OK,
  apagado por comando inválido, timeout local de 3 s y parpadeo de 500 ms.
- Configurar la red real y cargar ambos ESP32.
- Verificar lectura física del HC-SR04, cadencia, LEDs y reconexiones en WiFi.
- Verificar apagado físico ante caída del servidor o pérdida silenciosa del enlace.

No se modificaron los ESP32 ni PlatformIO durante esta implementación del servidor.
No se realizaron commits ni merges. Las pruebas locales no certifican el estado
de los LEDs ni la compatibilidad del firmware actual del actuador.
