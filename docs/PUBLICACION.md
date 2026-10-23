# Publicación en GitHub

Descomprime este proyecto, crea un repositorio vacío y sube su contenido raíz. Deben verse directamente `README.md`, `platformio.ini`, `web/` y `articulo/`; no subas el ZIP como único archivo.

Con Git, desde la carpeta del proyecto:

```bash
git init
git add .
git status --short
git commit -m "Publicar E-TWIN: código, informe y evidencia reproducible"
git branch -M main
git remote add origin https://github.com/TU_USUARIO/TU_REPOSITORIO.git
git push -u origin main
```

Revisa `git status` antes del commit. `include/config.h` y `include/etwin/RadioKeys.h` deben quedar ignorados. La preparación de este repositorio retiró credenciales de Wi-Fi/MQTT y claves de radio del archivo V2 recibido; no subas el V2 original ni su carpeta `.pio`, que puede contener binarios con secretos compilados. Si esas credenciales ya fueron públicas, revócalas y genera otras.

El README ya incluye una miniatura local enlazada al video suministrado. La imagen es un acceso al video y no un video embebido ni una prueba de su contenido.

Para el SCADA, habilita **Settings → Pages → GitHub Actions**. El flujo `pages.yml` publica únicamente `web/` en la dirección que indique GitHub. Revisa permisos y restricciones de tu cuenta/repositorio. La página no convierte por sí sola el canal de ThingSpeak en público.

El canal de demostración es el ID 1309330. La interfaz permite introducir un canal distinto. Publica datos industriales solo con autorización. Nunca añadas la clave de escritura o los datos de MQTT al JavaScript. Para datos privados con visitantes autenticados se requiere una arquitectura de acceso adicional, no una contraseña escondida en HTML.

Comprueba que el PDF final contenga los autores reales. El paquete no agrega una licencia abierta sin autorización de sus titulares.

Referencia de implementación: https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages (consulta: 23/10/2026).
