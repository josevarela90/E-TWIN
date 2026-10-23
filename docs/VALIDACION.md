> Nota de procedencia: este archivo se conserva del paquete V2 suministrado. Para las pruebas ejecutadas durante la preparación del informe, consultar `pruebas_copia_publica.txt` y `control_editorial.json`. No acredita una nueva carga física.

# E-TWIN — validación de la entrega

Fecha: 23 de octubre de 2026.

## Alcance

Se verifican código, protocolo, cálculos, formato de datos y aplicación web.
La validación de software no equivale a una calibración metrológica ni a una
prueba del conjunto físico. No se dispuso de los tres equipos conectados, de la
máquina ni de credenciales para un canal real de ThingSpeak.

## Compilación cruzada

Resultado final: **los tres entornos compilaron correctamente (SUCCESS)**.
Se generaron los ejecutables ELF y las imágenes de firmware para cada placa.

El proyecto fija en platformio.ini las versiones de la plataforma y bibliotecas.
Los tres entornos son independientes:

| Entorno | Microcontrolador | Resultado | Tamaño del programa en flash |
|---|---|---|---|
| master_core2 | ESP32 | SUCCESS | 1 103 525 bytes |
| slave_energy | ESP32-S3 | SUCCESS | 892 081 bytes |
| slave_vibration | ESP32-S3 | SUCCESS | 875 817 bytes |

Las tres imágenes caben en la partición de aplicación configurada, de
3 342 336 bytes. El tamaño compilado no mide la memoria libre durante una
conexión TLS ni sustituye una prueba prolongada sobre el hardware.

El comando reproducible es:

~~~bash
pio run -j 2
~~~

## Pruebas nativas del protocolo y análisis

**145 comprobaciones, 0 fallos en version 2.0 (C++ host).** Se ejecutó tests/test_core.cpp con g++:

- HMAC, clave incorrecta, trama corrupta y replay.
- Magic, versión, tipo, nodo, tamaño, campos reservados y longitud.
- Comparación de direcciones MAC y entradas nulas.
- Secuencias repetidas/atrasadas y rollover.
- Caducidad por nodo y rollover de milisegundos.
- Valores cero legítimos, NaN, infinitos y límites de medición.
- Invalidación por error, calentamiento, clipping, overflow y muestras perdidas.
- Estado parcial cuando falta un nodo.
- Referencias inválidas y divisiones no comparables.
- Media, varianza y estabilidad de una referencia.
- Alarma alta conservada incluso durante captura de referencia.

Las pruebas de secuencia validan el algoritmo y el código de recepción se
revisó para no refrescar la vigencia de duplicados. La autenticación radio y la
recepción real de ACK requieren comprobarse sobre los equipos.

## Pruebas del filtro de vibración

Se ejecutó tests/test_filter.cpp. Valores obtenidos para un seno de amplitud
pico **1 m/s² en un solo eje**, con los filtros software y calentamiento:

| Frecuencia de la señal sintética | RMS de salida |
|---|---|
| 1 Hz | 0.028244 m/s² |
| 5 Hz | 0.500000 m/s² |
| 20 Hz | 0.705434 m/s² |
| 60 Hz | 0.677835 m/s² |
| 100 Hz | 0.499999 m/s² |
| 200 Hz | 0.039345 m/s² |

El RMS de un seno sin atenuación de amplitud pico 1 es aproximadamente 0.7071.
La componente constante residual fue **0.000011472 m/s²** en la prueba digital.
También se comprobaron señal cero, movimiento circular en dos ejes,
continuidad entre ventanas y reinicio del filtro.

Estos números verifican **el cálculo software**. No incluyen ruido físico del
sensor, tolerancia de su reloj, respuesta del DLPF interno de 99 Hz, montaje ni
calibración. La respuesta del conjunto instalado será diferente.

## Aplicación web

Pasaron:

~~~text
node --check web/plugin.js
node web/test_data.cjs
node web/test_ui.cjs
~~~

Cobertura: cero frente a null, datos incompletos, versión del contrato,
caducidad, reloj futuro, antigüedad por nodo, huecos en gráficas, CSV, aislamiento
entre demo y datos reales, cancelación de consultas y clave solo en memoria.
La prueba de interacción automatizada utiliza un DOM simulado.

Se realizó además **inspección visual en Chrome** de la aplicación autónoma:
estado inicial vacío, activación de demo explícita, valores y gráficas,
salida de demo con limpieza de datos y formulario de conexión. No se utilizaron
credenciales reales ni se creó un plugin dentro de una cuenta de ThingSpeak.

## Comunicación, TLS y hardware pendientes

Se verificaron las APIs y requisitos con documentación oficial. La conexión
TLS real con el broker no pudo comprobarse desde el entorno de desarrollo.
No se presenta un envío MQTT como prueba de almacenamiento en ThingSpeak.

Antes del uso, completar estas comprobaciones sobre la instalación:

| Prueba física | Criterio de aceptación |
|---|---|
| Identidad | Cada monitor serie muestra la MAC STA esperada |
| UART PZEM | V/A/W/kWh/Hz/PF coherentes con carga e instrumento de referencia |
| Energía sin red medida | El fallo aparece como sin datos, no como cero ficticio |
| IMU | WHO_AM_I correcto, 500 muestras, tasa estimada de 475–525 Hz y datos sin errores |
| Montaje quieto | La gravedad no queda como una vibración permanente de 9.81 m/s² |
| ESP-NOW | El maestro recibe ambos nodos y el ATOMS3 muestra ACK reciente |
| Desconexión | El dato caduca a los 5 s desde su última recepción; la pantalla lo refleja en su siguiente actualización y el otro nodo sigue visible |
| Canal Wi-Fi | El cambio/reinicio del AP permite recuperar los dos esclavos |
| Referencia | Rechaza carga inestable y guarda una referencia repetible |
| MQTT | Última entrada del canal avanza aproximadamente cada 20 s |
| Nube caída | No se publican lecturas antiguas al recuperar la conexión |
| Plugin real | Ocho campos, estado y validez concuerdan con el Core2 |

El monitoreo no sustituye protecciones eléctricas o mecánicas y no ejecuta
parada de emergencia. El gemelo representa las mediciones disponibles; no
predice vida útil ni cuantifica eficiencia sin información adicional de salida.

## Repetir las pruebas de software

Con C++ y Node.js en PATH:

~~~bash
python tools/check_local.py
pio run -j 2
~~~

No son necesarias estas herramientas adicionales para cargar desde PlatformIO;
sirven para repetir la verificación del proyecto.
