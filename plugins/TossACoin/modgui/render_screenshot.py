#!/usr/bin/env python3
"""
Render screenshot-tossacoin.png via headless Chromium so it matches MOD-UI's
actual rendering of the pedal (real fonts, real knob sprite, real CSS
gradients). Ported from the sitar repo's render_screenshot.py.

Pipeline: resolve the Mustache tokens MOD-UI fills at runtime, point the knob
sprite URL at a local file, drop the audio/MIDI/CV port loops (the jacks sit
outside the frame and are cropped away), then screenshot at WIDTH x HEIGHT.

When you fork this template, update the plugin-specific constants below
(ICON/STYLE/SPRITE/OUT names, the SUBSTITUTIONS to match modgui.ttl, and
WIDTH/HEIGHT to your pedal size).

Needs Google Chrome (the same one `make manual` uses; override with CHROME=...)
and Pillow for the thumbnail:
    python3 render_screenshot.py

Preview it the way MOD-UI will style it: point MODUI_CSS at MOD-UI's css/
directory (e.g. from a MOD Desktop install) and its main.css + pedals.css are
loaded AFTER ours, so any rule of theirs that wins shows up (they force a
font on every element, and style the footswitch and light):
    MODUI_CSS=~/Software/mod-desktop-*/mod-desktop/html/css python3 render_screenshot.py
"""

import os
import re
import shutil
import tempfile

import subprocess

from PIL import Image

HERE   = os.path.dirname(os.path.abspath(__file__))
ICON   = os.path.join(HERE, "icon-tossacoin.html")
STYLE  = os.path.join(HERE, "stylesheet-tossacoin.css")
THUMB  = os.path.join(HERE, "thumbnail-tossacoin.png")
CHROME = os.environ.get("CHROME", "google-chrome")
OUT    = os.path.join(HERE, "screenshot-tossacoin.png")

WIDTH  = 640
HEIGHT = 300
# Device pixel ratio for the render. 2x keeps the same CSS layout but outputs
# at double resolution — crisper on HiDPI screens and in the printed manual.
SCALE  = 2

# Same values you put in modgui.ttl.
SUBSTITUTIONS = {
    "{{brand}}":   "Stefan",
    "{{label}}":   "Toss a Coin",
    "{{color}}":   "wood3",
    "{{knob}}":    "gold",
    "{{{cns}}}":   "",   # cache-bust class suffix; harmless to drop
    "{{{ns}}}":    "",   # cache-bust query string; not needed for local files
    "{{instancename}}": "tossacoin-render",
}


def render_template(html: str) -> str:
    # Drop HTML comments first: a doc comment may mention Mustache tokens (e.g.
    # the icon's "{{#effect.ports.audio.input}}" example), which the port-loop
    # regex below would otherwise match across, deleting real markup.
    html = re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)
    for k, v in SUBSTITUTIONS.items():
        html = html.replace(k, v)
    # Drop the audio/midi/cv port iteration blocks. The jacks are positioned
    # outside the pedal frame and cropped by overflow:hidden, so the screenshot
    # doesn't need them (and their Mustache loops can't render statically).
    for kind in ("audio", "midi", "cv"):
        for direction in ("input", "output"):
            html = re.sub(
                r"\{\{#effect\.ports\." + kind + r"\." + direction + r"\}\}.*?\{\{/effect\.ports\." + kind + r"\." + direction + r"\}\}",
                "", html, flags=re.DOTALL,
            )
    return html


# Knob film-strip frame shown for each default (initParameter() ranges).
KNOB_FRAMES = {
    "sensitivity": round((50 - 0) / 100 * 64),
    "bounce":      round((92 - 70) / 27 * 64),
    "height":      round((0.5 - 0.1) / 1.4 * 64),
    "pitch":       round((12 + 24) / 48 * 64),
    "glitch":      0,
    "level":       round((0 + 24) / 30 * 64),
}


def resolve_resources(text: str) -> str:
    """MOD-UI serves modgui/ at /resources/; point those URLs at the local files."""
    for k, v in SUBSTITUTIONS.items():
        text = text.replace(k, v)
    return text.replace("/resources/", "file://" + HERE + "/")


def main():
    with open(ICON,  "r", encoding="utf-8") as f: icon_html = f.read()
    with open(STYLE, "r", encoding="utf-8") as f: css       = f.read()

    css        = resolve_resources(css)
    pedal_html = resolve_resources(render_template(icon_html))
    # Show the engraved selectors at their defaults (MOD-UI's script does this live).
    for port, value in (("steps", 1), ("sound", 0), ("reverse", 0)):
        pedal_html = pedal_html.replace(f'data-port="{port}" data-value="{value}"',
                                        f'data-port="{port}" data-value="{value}" class="on"')
    pedal_html = pedal_html.replace('class="mod-light"', 'class="mod-light on"')
    # Knobs at their defaults: frame = (default - min) / (max - min) * 64.
    for port, frame in KNOB_FRAMES.items():
        pedal_html = pedal_html.replace(f'mod-port-symbol="{port}"',
                                        f'mod-port-symbol="{port}" style="background-position: -{frame * 54}px 0"')

    modui = "".join(f'<link rel="stylesheet" href="file://{os.environ["MODUI_CSS"]}/{f}">'
                    for f in ("main.css", "pedals.css")) if os.environ.get("MODUI_CSS") else ""

    page = f"""<!doctype html>
<html><head>
<meta charset="utf-8">
<style>
  html, body {{
    margin: 0; padding: 0;
    width: {WIDTH}px; height: {HEIGHT}px;
    overflow: hidden;
    background: transparent;
  }}
  .pedal-host {{ position: relative; width: {WIDTH}px; height: {HEIGHT}px; }}
  .tossacoin-pedal {{ box-shadow: none; }}
{css}
</style>{modui}</head>
<body><div class="pedal-host">{pedal_html}</div></body></html>"""

    with tempfile.TemporaryDirectory(prefix="tossacoin-render-") as tmp:
        page_path = os.path.join(tmp, "render.html")
        with open(page_path, "w", encoding="utf-8") as f:
            f.write(page)
        tmp_out = os.path.join(tmp, "screenshot.png")
        subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                        "--allow-file-access-from-files", "--default-background-color=00000000",
                        f"--force-device-scale-factor={SCALE}", f"--window-size={WIDTH},{HEIGHT}",
                        "--virtual-time-budget=4000", f"--screenshot={tmp_out}", "file://" + page_path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # 256-colour palette PNG: ~3x smaller, no visible loss on fabric and coins.
        Image.open(tmp_out).quantize(colors=256, method=Image.FASTOCTREE).save(OUT, optimize=True)

    shot = Image.open(OUT).convert("RGBA")
    shot.resize((128, round(128 * HEIGHT / WIDTH)), Image.LANCZOS).save(THUMB, optimize=True)
    print(f"Wrote {OUT} ({WIDTH*SCALE}x{HEIGHT*SCALE} @ {SCALE}x) and {THUMB}")


if __name__ == "__main__":
    main()
