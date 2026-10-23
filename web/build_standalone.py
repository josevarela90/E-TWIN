#!/usr/bin/env python3
"""Build index.html from the three ThingSpeak plugin source files."""
from pathlib import Path

root = Path(__file__).resolve().parent
html = (root / "plugin.html").read_text(encoding="utf-8")
css = (root / "plugin.css").read_text(encoding="utf-8")
js = (root / "plugin.js").read_text(encoding="utf-8")
assert html.count("%%PLUGIN_CSS%%") == 1
assert html.count("%%PLUGIN_JAVASCRIPT%%") == 1
assert "</script" not in js.lower()
assert "</style" not in css.lower()
html = html.replace("%%PLUGIN_CSS%%", css).replace("%%PLUGIN_JAVASCRIPT%%", js)
(root / "index.html").write_text(html, encoding="utf-8")
print("web/index.html generated from plugin.html + plugin.css + plugin.js")
