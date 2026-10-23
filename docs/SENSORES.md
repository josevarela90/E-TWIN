# E-TWIN — conexión, medición y validación de los sensores

## 1. Esclavo eléctrico: ATOMS3 Lite + PZEM-004T **v3.0**

El firmware usa el protocolo de **PZEM-004T v3.0**, mediante la biblioteca
PZEM004Tv30. Comprueba que el medidor realmente sea v3.0: la biblioteca y el
protocolo no son intercambiables con los de las versiones anteriores.

La frase de conexión del proyecto se interpreta desde las etiquetas del PZEM:

| Señal del PZEM | ATOMS3 Lite | Dirección desde el ATOM |
|---|---|---|
| TX | G2, con adaptación de nivel | RX, recibe del PZEM |
| RX | G1, con adaptación de nivel | TX, transmite al PZEM |
| GND de la interfaz TTL | GND | Referencia común de la interfaz de baja tensión |
| 5 V de la interfaz TTL | Alimentación regulada de 5 V | Alimentación de los optoacopladores |

En el conector HY2.0-4P del ATOMS3 Lite: negro = GND, rojo = 5 V,
amarillo = G2 y blanco = G1. El programa configura UART1 a 9600 baudios,
8 bits, sin paridad y un bit de parada; dirección predeterminada `0xF8`
para un único medidor.

**El ESP32-S3 trabaja con lógica de 3.3 V.** Utiliza adaptación de niveles UART
entre la interfaz del medidor y los GPIO; no asumas que el TX de cualquier
variante del PZEM es seguro para conectarlo directamente. La línea de 5 V
de alimentación no se conecta a G1/G2. El pin de 5 V del PZEM alimenta la
interfaz óptica: el circuito medidor también requiere su alimentación AC.

La conexión de red, el fusible/protección, el encapsulado y el montaje del
transformador de corriente dependen de la versión de 10 A o 100 A del PZEM.
Se realizan sin tensión y por una persona competente; este documento describe
la interfaz de datos y no sustituye el esquema eléctrico del modelo comprado.
Un solo PZEM no mide de forma completa una máquina trifásica.

Fuentes: [pinout ATOMS3 Lite](https://docs.m5stack.com/en/core/AtomS3-Lite),
[biblioteca y requisitos del PZEM v3](https://github.com/mandulaj/PZEM-004T-v30),
[especificaciones eléctricas ESP32-S3](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf).

### Comportamiento del programa

Se solicita un bloque de medición cada segundo. Se conservan las unidades
del medidor: V, A, W, kWh, Hz y factor de potencia de 0 a 1. No se reinicia el
contador de energía al encender ni se utiliza `V × I` como potencia activa.

Ante un fallo de comunicación o valores fuera de rango se envía una trama
con `SENSOR_ERROR`. Un dato ausente no se sustituye por cero ni por la última
lectura válida. La primera llamada a `voltage()` actualiza el bloque completo;
si falla, el programa no combina getters posteriores con esa petición fallida.
Al desbordarse millis() se reconstruye el objeto de la biblioteca PZEM para
evitar que su caché interna, en la versión 1.1.2, conserve datos antiguos por
una comparación de tiempo no segura. Esto no reinicia el contador físico de energía.

El muestreo vuelve a intentarse en el siguiente segundo. La búsqueda del
maestro y el mantenimiento del enlace continúan aunque el PZEM no responda.
Consulta el monitor serie a 115200 baudios para distinguir «enlace NO» de
«PZEM SIN LECTURA VALIDA»: son fallos diferentes.

## 2. Esclavo de vibración: ATOMS3 original

Se utiliza el **ATOMS3 original, con MPU6886**, pantalla de 128 × 128 e I2C
interno SDA=G38, SCL=G39, dirección `0x68`. El identificador `WHO_AM_I` esperado
es `0x19`. El **ATOMS3R utiliza otro IMU, BMI270**, con otro mapa de registros
y pines; este firmware no se presenta como compatible con ese modelo.

Fuentes: [ATOMS3 original](https://docs.m5stack.com/en/core/AtomS3),
[ATOMS3R](https://docs.m5stack.com/en/core/AtomS3R),
[datasheet MPU6886 enlazado por M5Stack](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf).

### Cadena de medición

1. El acelerómetro trabaja a **±8 g**, con **4096 cuentas/g**. Se usa
   `g = 9.80665 m/s²` para convertir las tres componentes.
2. El MPU6886 genera **500 muestras/s**. Su filtro interno de aceleración
   tiene un corte nominal de **99 Hz**. `SMPLRT_DIV=1`, `ACCEL_CONFIG=0x10`
   y `ACCEL_CONFIG2=0x02`; se verifica la configuración por lectura de registros.
3. Se habilita el FIFO de aceleración. En este modelo, cada trama contiene
   **8 bytes: X, Y, Z y temperatura**. La temperatura no se usa en el indicador
   de vibración. Suprimir esos dos bytes desalinearía todas las muestras.
4. Se filtra **cada eje con signo**, con un pasaaltos Butterworth de segundo
   orden a **5 Hz**, seguido de un pasabajos de segundo orden a **100 Hz**.
5. Se descartan **1000 muestras iniciales**, aproximadamente dos segundos,
   mientras se estabilizan los filtros. Después se calculan ventanas sucesivas
   de **500 muestras**, aproximadamente un segundo.

La banda de **6–75 Hz es nominal**: los cortes son graduales y la respuesta
final incluye el filtro interno de 99 Hz. No se trata de una respuesta plana
hasta 100 Hz ni de un instrumento con calibración trazable.

Para las aceleraciones filtradas `ax`, `ay`, `az`, en m/s²:

```text
RMS X = sqrt(mean(ax²))
RMS Y = sqrt(mean(ay²))
RMS Z = sqrt(mean(az²))
RMS resultante = sqrt(mean(ax² + ay² + az²))
Pico resultante = max(sqrt(ax² + ay² + az²))
Factor de cresta = Pico resultante / RMS resultante
```

Cuando el RMS es prácticamente cero se informa factor de cresta cero para
evitar una división inestable. Se eliminan DC/gravedad mediante el pasaaltos;
no se calcula `abs(norm(a) − g)`, que puede perder vibraciones transversales.
El filtrado atenúa componentes fuera de la banda; **el ruido dentro de la
banda permanece**. No se resta de forma arbitraria un supuesto piso de ruido.

### Calidad temporal y fallos

Una tarea dedicada lee el FIFO. La pantalla y ESP-NOW se atienden por separado,
para que dibujar la interfaz no determine el intervalo entre muestras.
La tasa reportada se estima con el reloj local y el número de muestras; tiene
la incertidumbre del sondeo del FIFO y no equivale a medir con un patrón externo.

| Indicador | Qué significa | Tratamiento |
|---|---|---|
| `WARMUP` | Filtro inicializándose | Sin valor válido; esperar |
| `CLIPPED` | Algún eje llegó al 98% del rango digital | Ventana inválida; revisar montaje/rango |
| `FIFO_OVERFLOW` | FIFO lleno o desbordado | Se descarta la ventana y reinicia el sensor/filtro |
| `TIMING_BAD` | Tasa fuera de 475–525 Hz, datos interrumpidos o pérdida temporal | Ventana inválida |
| `SENSOR_ERROR` | Fallo I2C, identificación o configuración | Reintento cada 2 s |

El campo `missed=65535` indica pérdida de cantidad desconocida en un overflow;
no es una cifra exacta de muestras perdidas. Un error de lectura se marca con
`missed=1` como indicación de interrupción, no como recuento exhaustivo. Los
flags y la exigencia de `missed=0` determinan la validez; el contador no pretende
cuantificar exactamente todas las muestras perdidas. Después de una recuperación
se repiten los dos segundos de estabilización.

La pantalla distingue `BUSCANDO` (sin beacon reciente del maestro), `SIN ACK`
(maestro visible, sin confirmación reciente de recepción de datos) y `ACK OK`
(el maestro confirmó una trama en los últimos tres segundos). Esa confirmación
indica recepción por la aplicación; una trama de error también puede confirmarse.

## 3. Montaje y límites de interpretación

Fija el ATOMS3 de forma rígida y repetible sobre una parte segura de la carcasa
de la máquina. Un sensor sostenido a mano, apoyado suelto o pegado sobre una
espuma mide también el movimiento del montaje. Mantén protegidos los equipos
y cables frente a partes móviles, calor y conductores energizados.

Para comparar mediciones conserva el punto, orientación, fijación, velocidad
y condición de carga. Anota estas condiciones antes de guardar una referencia
en el maestro. Un incremento del RMS es una observación que exige contexto;
por sí solo no identifica desbalance, rodamiento defectuoso ni ineficiencia.

Este sistema mide **aceleración de vibración**, no velocidad en mm/s.
No aplica límites ISO 10816/20816 ni pretende diagnosticar fallos de rodamientos
de alta frecuencia. Componentes muy lentas, por debajo de 5 Hz, y componentes
altas, fuera de la banda, se atenúan. La vibración no proporciona potencia
mecánica útil: no permite calcular por sí sola la eficiencia del motor.

## 4. Pruebas de puesta en marcha

| Prueba | Resultado esperado |
|---|---|
| ATOMS3 fijo sobre una mesa quieta | Tras estabilizarse, RMS pequeño y estable, pero no necesariamente cero |
| Giro lento del dispositivo, después reposo | Transitorio durante el movimiento; al quedar quieto, la gravedad no permanece como vibración de 9.81 m/s² |
| Golpe controlado leve, sin dañar el equipo | Aumento del pico; no es una calibración de amplitud |
| Máquina detenida y luego operando | Registrar ambos estados; comparar a igual régimen/carga |
| Desconectar alimentación/interfaz de baja tensión del PZEM de manera segura | El nodo sigue presente, pero el maestro marca medición eléctrica inválida |
| Apagar el esclavo de vibración | El maestro caduca el dato; no debe seguir mostrándolo como actual |
| Carga resistiva conocida dentro del rango del medidor | Comparar con un instrumento de referencia adecuado; revisar V, A, W y PF |
| Máquina trifásica | Detener la interpretación de consumo total; hace falta instrumentar las fases correctamente |

El proyecto incluye `tests/test_filter.cpp`. Esta prueba comprueba rechazo de
DC, respuesta a senos conocidos, coherencia de RMS/pico, señal cero y reinicio
de ventanas. Se ejecuta en un computador; no prueba la electrónica, la
calibración del MPU6886, la conexión PZEM ni el enlace radio real.

```bash
c++ -std=c++11 -Wall -Wextra -Werror -I include tests/test_filter.cpp -o /tmp/etwin_filter_test
/tmp/etwin_filter_test
```
