# E-TWIN 2.0 — contrato de datos

ESP-NOW: structs packed de `include/etwin/Protocol.h`, little endian IEEE754 float32,
protocolo 2, HMAC-SHA256 truncado a 128 bits; sesion aleatoria del maestro, contador de arranque monotono persistente, secuencia monotona. Datos de
esclavos unicast cifrados; beacon de descubrimiento broadcast sin cifrar PERO autenticado con HMAC. El maestro
valida MAC, longitud, versión, HMAC, sesion del maestro, contador de arranque, secuencia y rango de medidas. Un ACK
confirma recepción de la trama por la aplicación, no validez física de la medida.

## ThingSpeak

Un solo canal, ocho campos habilitados, publicación cada **20 s**. La pantalla
local trabaja con ventanas de aproximadamente 1 s. No se rellenan campos ausentes
con datos anteriores. `status` se publica aun cuando no hay sensores válidos.

| Campo | Nombre | Unidad |
|---|---|---|
| field1 | Voltaje | V RMS |
| field2 | Corriente | A RMS |
| field3 | Potencia activa | W |
| field4 | Energía acumulada PZEM | kWh |
| field5 | Frecuencia de red | Hz |
| field6 | Factor de potencia | 0–1 |
| field7 | Vibración RMS resultante | m/s² |
| field8 | Vibración pico resultante | m/s² |

Vibración: MPU6886 ±8 g, muestreo nominal 500 Hz, ventana 500 muestras,
HP Butterworth orden 4 a 6 Hz + LP Butterworth orden 4 a 75 Hz y DLPF de acelerómetro 99 Hz.
El RMS es sqrt(mean(ax²+ay²+az²)) tras filtrar cada eje. Pico=max(norm(a)).
No es velocidad mm/s ni clasificación ISO. No se resta arbitrariamente un piso
de ruido. Montaje rígido; comparación válida únicamente para el mismo montaje,
velocidad y carga. Muestras atrasadas, overflow y saturación invalidan la ventana.

`status` es ASCII, pares separados por `;`, claves/valores separados por `=`.
Se codifica URL al publicarlo; REST lo entrega como texto decodificado.

```
ver=2;state=RUN;e=1;v=1;ea=200;va=100;alarm=0;ref=1;dp=4.2;vr=1.08;boot=1234;seq=45;vf=1;run=1;ot=1;rc=10;ct=0;vx=0.20;vy=0.30;vz=0.40;cf=2.1;fs=500.0;ns=500;mi=0;rj=0;rp=0
```

| Clave | Definición |
|---|---|
| ver | Versión 2 |
| state | WAIT (sin datos eléctricos), STOP (por debajo de umbral eléctrico), RUN (operando), WARN (alerta), ALARM (límite alto), PARTIAL (falta vibración), BASE (capturando referencia) |
| e, v | 1 = medición actual válida; 0 = inválida, ausente o caducada |
| ea, va | Edad de recepción de la última trama del nodo en ms al publicar; -1 = ninguna. La edad total web suma tiempo desde created_at. |
| alarm | Máscara: 1 tensión, 2 PF bajo, 4 potencia vs referencia, 8 vibración vs referencia, 16 vibración absoluta, 32 configuración incompleta |
| ref | 1 = referencia válida, 0 = aún sin referencia |
| dp | Desviación potencia activa vs referencia (%), `na` si no comparable |
| vr | Cociente RMS vibración / referencia, `na` si no comparable |
| boot, seq | Sesión del maestro y número de publicación |
| vf | Flags de la última ventana de vibración (ver Protocol.h); 0 si ninguna |
| run | 1 si PZEM supera umbral de potencia y es válido; NO mide RPM |
| ot | 1 si el maestro usa el protocolo OT autenticado; es declarativo, no atestacion |
| rc | Cantidad requerida de pares para la referencia (10) |
| ct | Cantidad capturada durante calibracion |
| vx, vy, vz | Aceleracion RMS filtrada de cada eje, m/s2 |
| cf | Factor de cresta pico/RMS |
| fs | Tasa estimada de muestreo Hz |
| ns, mi | Numero de muestras de ventana y perdidas |
| rj, rp | Contadores de autentificacion fallida/replays durante este arranque |


`e/v` se calculan con TTL local 5000 ms. El panel representa el último muestreo
publicado como instantánea de nube, y marca el feed caducado después de 65 s.
Aunque un dato de nube siga visible, su timestamp y antigüedad deben ser explícitos;
no representa una señal en tiempo real de milisegundos. QoS0 no confirma almacenamiento.

No se publica eficiencia. El factor de potencia y la desviación de consumo son
indicadores diferentes. No se interpolan huecos en gráficas ni se usa `null` como cero.
