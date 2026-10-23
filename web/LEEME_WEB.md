# E-TWIN — panel web y plugin de ThingSpeak

## Opción más sencilla: un único bloque HTML

1. En ThingSpeak, abre **Apps → Plugins → New → Custom → Create**.
2. Nombra el plugin **E-TWIN · Monitoreo energético**.
3. Abre `index.html` con un editor de texto. Copia **todo su contenido** en el campo **HTML** del plugin.
4. Deja vacíos los campos **CSS** y **JavaScript**, porque `index.html` ya los incluye.
5. En **Add this Visualization to a Channel**, selecciona el canal. Guarda y abre el plugin.
6. Pulsa **Conectar canal**, introduce el **Channel ID** y, si es privado, su **Read API Key**.

Los plugins de ThingSpeak se presentan en la **vista privada** del canal. El panel
es amplio: para trabajar cómodamente, abre la página propia del plugin en una
pestaña. Dentro de un recuadro pequeño también se adapta, pero necesitarás
desplazarte verticalmente.

## Opción con los tres editores

Pega `plugin.html` en HTML, `plugin.css` en CSS y `plugin.js` en JavaScript.
Los marcadores `%%PLUGIN_CSS%%` y `%%PLUGIN_JAVASCRIPT%%` del HTML ya están dentro
de sus etiquetas `<style>` y `<script>`: **no añadas esas etiquetas** en los otros
dos editores. Si el editor elegido incorpora por sí mismo etiquetas adicionales,
usa la opción de un único HTML descrita arriba.

## Usarlo en el computador

Abre `index.html` en un navegador moderno. El panel no necesita bibliotecas,
fuentes ni imágenes externas. Para leer datos reales sí necesita Internet y
acceso HTTPS a `api.thingspeak.com`. Si tu navegador bloquea consultas desde
archivos locales, sirve la carpeta con `python -m http.server 8080` y abre
`http://localhost:8080`; también puedes usar directamente el plugin.

Al abrir no aparecen datos ficticios. **Explorar demo**, en el pie del panel,
activa una simulación local claramente identificada. **Salir de la demo** borra
esa vista. Ninguna acción del panel escribe datos ni controla la máquina.

## Datos y antigüedad

El mapeo exacto de los ocho campos y del estado es `../docs/DATA_CONTRACT.md`.
El panel pide `feeds.json?results=100&status=true`, consulta aproximadamente cada
20 segundos y utiliza como máximo las últimas 100 publicaciones.

- Una muestra con más de **65 s** se marca como antigua y detiene la animación.
- `e` y `v` describen la validez **cuando el maestro publicó**. La antigüedad
  mostrada suma la edad del nodo al publicar más el tiempo transcurrido en nube.
- Campos nulos, ausentes o grupos marcados inválidos se muestran como `—`.
  Un cero válido sí se muestra como cero. No se reutilizan valores anteriores.
- Se exige `ver=2` y metadatos reconocidos para interpretar las mediciones.
- Las curvas se interrumpen en datos inválidos o intervalos mayores de 32 s.
- La potencia aparente se calcula como `V × I`. El factor de potencia, las
  desviaciones y la vibración **no son una medición de eficiencia**.
- El giro del dibujo expresa un estado de operación inferido. No mide RPM.
- La exportación CSV identifica explícitamente los datos de demostración.

La cuenta y el canal deben habilitar los ocho campos. El panel usa únicamente
la **Read API Key**; no introduzcas la Write API Key, la clave de la cuenta ni las
credenciales MQTT. El Channel ID puede recordarse en el navegador. La clave
solo queda en memoria y se borra al recargar/cerrar: vuelve a introducirla para
canales privados. En un iframe que bloquee almacenamiento local, el panel sigue
funcionando, pero no recordará el Channel ID.

## Mantenimiento y verificaciones

Edita los tres archivos fuente y ejecuta desde la raíz del proyecto:

```bash
python web/build_standalone.py
node --check web/plugin.js
node web/test_data.cjs
node web/test_ui.cjs
```

El último comando prueba eventos y estados con un DOM mínimo simulado; no
sustituye la verificación visual en navegador ni una prueba con un canal real.

## Fuentes oficiales

- [Plugins App](https://www.mathworks.com/help/thingspeak/plugins-app.html)
- [Read Data: feeds y status](https://www.mathworks.com/help/thingspeak/readdata.html)
- [Channel Data Control: intervalos y caché](https://www.mathworks.com/help/thingspeak/channel-control.html)
- [MQTT Basics: TLS, QoS 0 y broker](https://www.mathworks.com/help/thingspeak/mqtt-basics.html)
- [Publish to a Channel Feed](https://www.mathworks.com/help/thingspeak/publishtoachannelfeed.html)

Consultadas el 23 de octubre de 2026.

El SCADA 2.0 incluye medidas de eje y estado OT en `status` y no guarda claves MQTT en JavaScript. El panel no permite controlar la maquina.
