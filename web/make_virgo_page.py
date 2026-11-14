#!/usr/bin/env python3
"""web/virgo-editor.html: VIRGO's page (install, the user bank, the guide), everything inside it.

  python3 web/make_virgo_page.py VERSION   (after ./build.sh --release VERSION and --bt, the screens are in docs/shots)"""
import base64, json, sys
from pathlib import Path
W = Path(__file__).resolve().parent
R = W.parent
ver = sys.argv[1]
b64 = lambda p: base64.b64encode(Path(p).read_bytes()).decode()
d = ver.split(".")
prod, prod_bt = f"FM-1_97{d[1]}", f"FM-1_98{d[1]}"
ota_js = (W / "fm1ota.js").read_text().replace("export function", "function").replace("export class", "class")
shots = {p.stem: b64(p) for p in [R / "docs" / "shots" / f"{n}.png" for n in
         ("sorgente", "armonia", "fusioni", "chirp", "finale", "motore", "ringdown", "filtro", "interferometro", "rumore", "radio", "evento", "moda", "modb", "cielo", "run", "deja", "turing", "ritmo", "campione", "mix", "riverbero", "shimmer", "spazio", "midi")] if p.exists()}
html = (W / "virgo.template.html").read_text()
for k, v in {"__OTA_JS__": ota_js, "__PKG_B64__": b64(R / "build" / f"virgo-{ver}.fwsc"),
             "__PKG_BT_B64__": b64(R / "build" / f"virgo-{ver}-bluetooth.fwsc"), "__PRODUCT_BT__": prod_bt,
             "__PRODUCT__": prod, "__WASM_B64__": b64(W / "virgo.wasm"), "__VERSION__": ver, "__SHOTS_JSON__": json.dumps(shots)}.items():
    html = html.replace(k, v)
assert "__WASM" not in html and "__PKG" not in html and "__OTA" not in html
(W / "virgo-editor.html").write_text(html)
print("web/virgo-editor.html", len(html), "B")
