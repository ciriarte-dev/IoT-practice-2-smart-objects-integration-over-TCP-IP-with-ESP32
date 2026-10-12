# Servidor TCP — Práctica 2 de IoT

## Estado y alcance

Etapas 1 y 2 completadas: protocolo aprobado e infraestructura TCP básica
implementada. El contrato descrito en las secciones de identificación, mediciones,
comandos y temporizadores sigue pendiente de implementación. Todavía no se ha
verificado la interoperabilidad con los ESP32.

Ya se puede ejecutar el servidor y sus pruebas automatizadas. La implementación
continuará por etapas, con aprobación antes de avanzar.
Los cambios del servidor se limitarán a `server/`, en `feature/server`, sin commits
ni merges automáticos.

### Funcionalidades implementadas en la etapa 2

- `TCPServer` escucha por TCP/IPv4 en `0.0.0.0:5000` por defecto, con host y puerto
  configurables desde la línea de comandos.
- Usa `selectors.DefaultSelector` y sockets no bloqueantes para aceptar múltiples
  conexiones sin esperar que un cliente inactivo envíe datos.
- Cada `ClientConnection` mantiene su socket, dirección y `receive_buffer`
  independiente. Los datos se conservan como bytes, incluidos los fragmentos.
- Registra inicio, conexiones, desconexiones, errores y cierre mediante `logging`.
- Gestiona cierre remoto, errores de recepción y cierre por Ctrl+C; libera los
  sockets y el selector. Los errores de inicio producen salida con código 1,
  argumentos inválidos con código 2 y Ctrl+C con código 0.
- Limita el buffer acumulado a 65536 bytes por cliente. Si se excede, cierra solo
  esa conexión y descarta su buffer; este límite se configura en el constructor.

**En esta etapa no hay respuestas, ni siquiera a HELLO.** No se decodifican ni
separan líneas, no se asignan roles y no se aplican los temporizadores del protocolo.
El límite temporal de 64 KiB del transporte no sustituye el límite contractual
de 128 bytes por línea: este último se aplicará al implementar ProtocolHandler.
Un cliente que envíe datos continuamente acabará llenando el buffer porque aún
no existe un consumidor de mensajes. Los clientes silenciosos permanecen conectados.

El tiempo de espera del selector es de 0.5 s y no es un timeout de cliente.
La caída silenciosa de una red puede no detectarse inmediatamente; los mecanismos
de vigencia y apagado seguro siguen pendientes de las etapas posteriores.

### Archivos actuales

| Archivo | Función |
| --- | --- |
| `server/main.py` | Argumentos, configuración de logging y ejecución. |
| `server/tcp_server.py` | Clases TCPServer y ClientConnection. |
| `server/__init__.py` | Permite importar el servidor como paquete. |
| `server/tests/test_tcp_server.py` | Pruebas de transporte y de la línea de comandos. |
| `server/README.md` | Alcance, contrato, uso y resultados. |

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

## Arquitectura actual y prevista

Python 3 (3.7 o posterior), exclusivamente biblioteca estándar. TCPServer ya utiliza
un bucle de eventos con `socket` y `selectors`, sockets no bloqueantes y registro
mediante `logging`, sin un hilo por cliente. Los temporizadores de aplicación y
los envíos se incorporarán en las siguientes etapas.

Referencias de la biblioteca estándar: [selectors](https://docs.python.org/3/library/selectors.html)
y [socket](https://docs.python.org/3/library/socket.html).

| Clase | Responsabilidad prevista |
| --- | --- |
| `TCPServer` | Implementados escucha, selector, recepción y cierre. Pendientes escritura y temporizadores de aplicación. |
| `ClientConnection` | Implementado estado de transporte por conexión; no tiene roles. |
| `ClientManager` | Sesiones, roles, buffers por conexión y limpieza al desconectar. |
| `ProtocolHandler` | Delimitación, validación y construcción de mensajes. |
| `DistanceController` | Clasificación, vigencia y estado de LEDs, independiente de sockets. |

`server/main.py` es el punto de entrada. `start()`, `poll_once()` y `close()` permiten
controlar TCPServer en pruebas; `serve_forever()` gestiona el ciclo completo y
garantiza el cierre al salir. Estos métodos se usan desde el mismo hilo.
ClientManager, ProtocolHandler y DistanceController todavía no existen.

## Configuración y ejecución

Valores predeterminados: `0.0.0.0`, puerto TCP `5000`, ambos configurables.
Los ESP32 deberán usar la IP real del equipo servidor en la red local;
`0.0.0.0` no es una dirección de destino para los clientes.
No se ha asignado todavía una IP real ni comprobado la disponibilidad del puerto.

Desde la raíz del proyecto, sin instalar dependencias externas:

```sh
python3 server/main.py --host 0.0.0.0 --port 5000
```

También se puede ejecutar `python3 server/main.py` para usar los valores por
defecto, o `python3 -m server.main`. Consultar `python3 server/main.py --help`.
Para escuchar únicamente en el propio equipo y usar otro puerto:

```sh
python3 server/main.py --host 127.0.0.1 --port 5001
```

El puerto acepta valores entre 0 y 65535; 0 solicita al sistema un puerto libre
para pruebas. El registro de inicio muestra el puerto efectivo. Si 5000 está
ocupado, el servidor informa el error y termina; elegir otro con `--port`.
Para detenerlo, pulsar Ctrl+C en la terminal donde está ejecutándose.

Prueba manual opcional con dos terminales adicionales, si se dispone de `nc`:

```sh
nc 127.0.0.1 5000
```

Dejar una conexión sin escribir y enviar texto desde la otra: ambas deben seguir
abiertas, aunque no habrá respuesta de aplicación. Cerrar una con Ctrl+C y
comprobar su desconexión en el registro. Detener finalmente el servidor con Ctrl+C.

Los argumentos de tiempos de espera del protocolo se documentarán cuando se
implementen. No hay identificación, clasificación ni registros de LEDs todavía.

## Plan de pruebas y resultados

Las pruebas actuales usan `unittest` y sockets TCP reales en loopback, sin ESP32
ni servicios externos, con puertos libres asignados por el sistema.

Desde la raíz del proyecto:

```sh
python3 -B -W error::ResourceWarning -m unittest discover -s server/tests -v
```

Resultado de la etapa 2: **13 pruebas aprobadas, 0 fallos y 0 omisiones**, en
Python 3.14.5 sobre macOS. Última ejecución: 0.204 s. Verifican:

- Valores por defecto y escucha efectiva en la dirección configurada.
- Dos clientes simultáneos y recepción mientras el otro permanece inactivo.
- Independencia de buffers y conservación de fragmentos recibidos.
- Registro de conexiones/desconexiones y continuidad de los demás clientes.
- Desconexión abrupta mediante reset TCP y aceptación de conexiones posteriores.
- Límite del buffer sin afectar a otro cliente.
- Limpieza de buffers, cierre repetido y reutilización del puerto.
- Puerto ocupado y argumentos inválidos con salida controlada.
- Ausencia de respuestas o interpretación de HELLO en esta etapa.
- Ctrl+C simulado con SIGINT a un proceso real, cierre del cliente conectado y
  salida con código 0, sin traceback.

Las pruebas de reset mediante SO_LINGER y SIGINT están marcadas para omitirse
en Windows por diferencias de plataforma. El primer intento dentro del sandbox
fue bloqueado al abrir puertos; la ejecución final con permiso para sockets
locales pasó completa. No se realizaron pruebas con ESP32 físicos ni sobre WiFi.

### Casos pendientes para las siguientes etapas

Esta matriz corresponde al contrato completo, no a funcionalidades ya probadas.
Para probar los futuros temporizadores se preferirá un reloj controlable.

| Caso | Resultado esperado |
| --- | --- |
| 10 y 19.99 cm | Rojo intermitente. |
| 20, 30 y 40 cm | Amarillo. |
| 40.01 y 50 cm | Verde. |
| INVALID, número rechazado o DIST fuera de 2–400 cm | Todos apagados. |
| Desconexión del sensor o caducidad | Apagado sin detener el servidor. |
| Desconexión del actuador | Liberar su sesión; seguir atendiendo al sensor. |
| Reconexión de ambos roles | Nueva identificación y ausencia de estados antiguos. |
| Mensaje dividido entre varias recepciones | Procesar solo después del LF. |
| Varios mensajes en una recepción | Procesar todos, en orden. |
| Mensajes inválidos, extensos, no ASCII o rol duplicado | Error y política de cierre documentados. |
| Ambos clientes conectados simultáneamente | Atención independiente y entrega de comandos. |
| Actuador reconectado con lectura anterior aún vigente | Apagado hasta una lectura nueva. |
| Envíos parciales o cliente lento | Sin corrupción de mensajes ni bloqueo global. |

El parpadeo y el apagado autónomo del actuador necesitarán además validación
de su firmware por el integrante responsable.

## Etapas siguientes

1. Protocolo documentado: completada.
2. Servidor TCP básico: completada.
3. Identificación y gestión de clientes.
4. Recepción y validación de mediciones.
5. Algoritmo de control y envío de comandos.
6. Reconexión, errores y apagado seguro.
7. Pruebas automatizadas y documentación de resultados.

Cada etapa requiere aprobación antes de comenzar. Este documento no implica
que las etapas siguientes estén implementadas.
