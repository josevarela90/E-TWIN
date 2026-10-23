# Configurar ThingSpeak para E-TWIN

## 1. Crear un canal

En tu cuenta de ThingSpeak crea un canal llamado **E-TWIN** y habilita los ocho
campos. Conserva el Channel ID.

| Campo | Nombre recomendado | Unidad |
|---|---|---|
| 1 | Voltaje | V |
| 2 | Corriente | A |
| 3 | Potencia activa | W |
| 4 | Energia acumulada | kWh |
| 5 | Frecuencia | Hz |
| 6 | Factor de potencia | 0–1 |
| 7 | Vibracion RMS | m/s² |
| 8 | Vibracion pico | m/s² |

El canal puede ser privado. Activa el estado del canal si el editor ofrece esa
opción. El firmware publica la información de estado en el atributo status;
el panel lee ese atributo explícitamente mediante status=true.

## 2. Crear el dispositivo MQTT

En **Devices → MQTT**, añade un dispositivo y autoriza **Publish** para el canal
E-TWIN. Conserva el **Client ID, Username y Password** que proporciona ese
dispositivo. Si la contraseña solo se muestra al crearla, guárdala de forma
segura. Esos tres datos van en **include/config.h** del proyecto.

No sustituyas estos datos por el Write API Key, Read API Key o clave de cuenta.
Solo el maestro se conecta a MQTT; no compartas simultáneamente su Client ID
con otro cliente MQTT activo, porque se podrían expulsar mutuamente.

Parámetros implementados:

~~~text
Host: mqtt3.thingspeak.com
Puerto: 8883
TLS: obligatorio, certificado y nombre de servidor validados
Topic: channels/CHANNEL_ID/publish
QoS: 0
Retain: false
Clean session: true
Intervalo: 20 s
~~~

Los campos se publican en una misma entrada. Un sensor inválido se omite y se
marca como tal en status; no se publican ceros inventados ni cachés antiguos.
Los estados se codifican como URL para cumplir con el formato del broker.

ThingSpeak permite un mínimo de 15 s entre escrituras para licencia gratuita,
según la documentación consultada. Se usan 20 s. Una licencia puede tener
también cuotas de mensajes o condiciones de uso: verifica las aplicables a tu
cuenta, especialmente si monitorizas continuamente o utilizas varias máquinas.
Ninguna cuenta se crea o modifica automáticamente con estos archivos.

## 3. Validación TLS y reloj

El maestro obtiene UTC por NTP (pool.ntp.org y time.google.com). Mientras no
tenga una hora válida no intenta autenticar el certificado TLS. La pantalla de
comunicaciones permite distinguir esa espera de un problema MQTT.

**include/etwin/ThingSpeakCA.h** contiene certificados raíz públicos de DigiCert,
ISRG y Amazon; no contiene una clave privada. Los certificados DigiCert Global
Root G2, ISRG Root X1 y Amazon Root CA1 proceden del bundle Mozilla/certifi
consultado; DigiCert Global Root CA se obtuvo del sitio oficial DigiCert.
Se incluyeron raíces de confianza, no un certificado hoja de corta duración.

El handshake del broker no pudo comprobarse desde el entorno de desarrollo;
debe verificarse desde la red donde se instalará el Core2. Si el proveedor cambia
de cadena de confianza, actualiza el certificado raíz a partir de una fuente
oficial después de comprobar la cadena. No sustituyas la validación por
setInsecure(). Revisa también NTP, DNS y firewall de salida TCP 8883.

Fuentes:
[MQTT seguro](https://www.mathworks.com/help/icomm/ug/communicate-securely-thingspeak-mqtt.html),
[DigiCert Global Root CA](https://cacerts.digicert.com/DigiCertGlobalRootCA.crt).

## 4. Verificar una publicación

Después de cargar el maestro con la configuración:

1. Abre su pantalla de comunicaciones con C.
2. Espera Wi-Fi conectado, hora NTP y MQTT conectado.
3. Comprueba que aparece **MQTT enviado (QoS0)**.
4. Abre el canal en ThingSpeak y comprueba que su última entrada avanza.
5. Revisa que los valores y el estado corresponden a los nodos encendidos.

Un resultado exitoso de publish() indica envío, no un acuse de almacenamiento
del servidor. ThingSpeak utiliza QoS0. La prueba final es observar la entrada
correcta en el canal.

## 5. Instalar el plugin

En **Apps → Plugins → New → Custom → Create**:

1. Escribe **E-TWIN · Monitoreo energético** como nombre.
2. Copia **todo web/index.html** en el editor **HTML**.
3. Deja **CSS y JavaScript vacíos**: el HTML autónomo ya contiene ambos.
4. Selecciona el canal en **Add this Visualization to a Channel**.
5. Guarda y abre el plugin. Pulsa **Conectar canal**.
6. Introduce el Channel ID y la **Read API Key**, si el canal es privado.

La clave de lectura se mantiene solo en memoria del navegador; habrá que
introducirla de nuevo al recargar. No se solicitan credenciales MQTT ni claves
de escritura. La consulta de un canal público no necesita Read API Key.

Los plugins de ThingSpeak aparecen en la **vista privada** del canal. Para
exponer un dashboard a público externo se necesita otro mecanismo; esta entrega
no hace público el canal ni publica un sitio externo.

Si prefieres editores separados, utiliza web/plugin.html, web/plugin.css
y web/plugin.js; el método del HTML autónomo evita depender de cómo el editor
inserta los marcadores CSS/JS. web/build_standalone.py regenera el autónomo.

## 6. Panel

Muestra las ocho medidas, potencia aparente, desviaciones de referencia, nodos,
estado de la máquina, antigüedad y dos series temporales. Un gráfico SVG refleja
la relación entre sensores, máquina, maestro y nube. Su rotor es ilustrativo:
no equivale a medir RPM.

El panel consulta las últimas 100 entradas aproximadamente cada 20 s. Marca
datos antiguos después de 65 s. La frecuencia local de adquisición es mayor
que la de nube: el panel representa instantáneas publicadas, no una onda de
vibración en tiempo real. Las series interrumpen los huecos y no interpolan
valores perdidos.

**Explorar demo** funciona sin equipos; los números quedan identificados como
simulados y nunca se envían al canal. No confundir esa demo con una prueba de
recepción MQTT o de sensores.

## Documentación oficial consultada

- [MQTT Basics](https://www.mathworks.com/help/thingspeak/mqtt-basics.html)
- [Publish to a Channel Feed](https://www.mathworks.com/help/thingspeak/publishtoachannelfeed.html)
- [Channel Data Control](https://www.mathworks.com/help/thingspeak/channel-control.html)
- [Read Data](https://www.mathworks.com/help/thingspeak/readdata.html)
- [Plugins App](https://www.mathworks.com/help/thingspeak/plugins-app.html)

Consulta: 23 de octubre de 2026.
