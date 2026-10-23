# Informe IEEE ULEAM — fuentes LaTeX

Documento en español, clase `IEEEtran`, opciones `journal,onecolumn,11pt,a4paper`, basado en la plantilla aportada. El PDF verificado tiene 10 páginas incluidas las referencias.

## Editar y compilar

1. Completa `metadatos.tex`: autores, afiliaciones, correos institucionales y tutor. No se han inventado estas identidades.
2. Abre `main.tex` como documento principal en Overleaf y selecciona **pdfLaTeX**. También puedes compilar localmente:

```bash
pdflatex main
bibtex main
pdflatex main
pdflatex main
```

3. Tras editar, verifica el límite de 6–10 páginas y la resolución de referencias.

Se incluye `main.bbl` generado para facilitar la compilación inicial, además de `referencias.bib`. Las figuras en PNG son autónomas; las gráficas se incluyen en PDF vectorial. La arquitectura y el flujo también están en SVG editable. No se requieren fuentes externas ni acceso a Internet para compilar si tu distribución dispone de los paquetes LaTeX usados.

Los resultados se derivan de `../data/feeds_original.csv` mediante `../analysis/analyze_csv.py`. La tabla generada usa coma decimal y desviación estándar muestral. El apartado de validación distingue consistencia interna de exactitud independiente; no se deben presentar los porcentajes del antecedente como errores medidos en este prototipo.
