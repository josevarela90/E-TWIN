# Análisis de datos E-TWIN

`analyze_csv.py` conserva el original y selecciona un intervalo cerrado de 600 segundos terminado en la última observación conjunta válida. No selecciona simplemente las últimas 30 filas ni retrocede arbitrariamente para reunir 30 puntos.

Los criterios reproducen controles del contrato v2: fecha e ID interpretables; ocho magnitudes finitas; `ver=2`; `e=v=1`; edades de recepción entre 0 y 4999 ms; rangos eléctricos y de aceleración admisibles; `vf=1`; 500 muestras; cero muestras perdidas; tasa 475–525 Hz. Se rechazan duplicados dentro de la selección, sin deduplicación silenciosa. No se imputa, suaviza ni elimina por amplitud.

La fecha de cierre observada es 2026-10-23T21:50:37Z. La apertura es 21:40:37Z. Las 31 filas corresponden a IDs 1785–1815. Cuatro filas posteriores no son conjuntas válidas; se conservan en `auditoria_validez.csv`.

La desviación estándar usa `ddof=1`. La energía integrada emplea la regla trapezoidal con intervalos reales de las marcas del servidor. La discrepancia relativa usa como denominador el incremento de energía acumulada. Los controles `P ≈ V·I·FP`, RMS por ejes y pico/RMS son de coherencia interna: no son una calibración independiente.

Las marcas de tiempo pertenecen a publicaciones almacenadas en nube. `ea` y `va` son edades desde recepción local al preparar la publicación. Ninguna de esas medidas es una latencia extremo a extremo. La completitud del intervalo elegido no es la tasa de entrega de radio global. La aceleración publicada resume ventanas de 500 muestras, no una señal cruda continua de 10 minutos.

Los datos brutos contienen registros antiguos de otra estructura. Su rechazo por el contrato v2 no demuestra fallos de la versión actual. Las fotografías y capturas no se presumen sincronizadas con este intervalo.

## Reproducción

```bash
python -m pip install -r analysis/requirements.txt
python analysis/analyze_csv.py
```

Para otro CSV utiliza `--csv ruta/feeds.csv`. Revisa sus resultados antes de actualizar el texto del artículo: la tabla y las gráficas se regeneran, pero las cifras redactadas en los párrafos no se reescriben automáticamente.

Entorno de preparación: Python 3, NumPy 2.3.5, pandas 2.2.3 y matplotlib 3.10.8. Los gráficos usan estilos predeterminados y representan todos los puntos aceptados.
