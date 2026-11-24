#!/usr/bin/env python3
"""Generate ZEBU Slicer branding assets from the ZEBU logo geometry.

The upstream OrcaSlicer image file names are kept (resources/images/OrcaSlicer*.png|svg|ico|icns),
only their content changes, so no C++ or CMake reference has to be touched.

The SVGs are written "flat" on purpose: OrcaSlicer renders its SVGs with nanosvg, which ignores
<style>/classes, <use>, paint-order and vector-effect. "Stroke under fill" (paint-order) is emulated
by drawing each shape twice: a black stroked copy, then the fill on top.

Needs: rsvg-convert, Pillow.   Usage: scripts/zebu/gen_branding.py [repo_root]
"""
import io
import os
import subprocess
import sys

from PIL import Image

ORANGE = "#F7821B"
BLACK = "#000000"
WHITE = "#FFFFFF"

HEX = [(525, 0), (750, 130), (750, 390), (525, 520), (300, 390), (300, 130)]
WING = [(-440, 68), (-245, 68), (-101, 235), (-56, 235), (-16, 297), (-16, 445),
        (-146, 310), (-186, 310), (-321, 140), (-399, 140)]

LETTERS = [
    ("polygon", "0,0 240,0 240,65 95,235 240,235 240,300 0,300 0,235 145,65 0,65", "nonzero"),
    ("path", "M270,0 H490 V65 H350 V117 H470 V183 H350 V235 H490 V300 H270 Z", "nonzero"),
    ("path", "M530,0 H650 C720,0 755,35 755,80 C755,110 740,130 718,142 C750,155 770,185 770,222 "
             "C770,270 735,300 665,300 H530 L572,150 Z M600,52 L690,78 L600,104 Z M600,196 L695,222 L600,248 Z",
     "evenodd"),
    ("path", "M810,0 H890 V210 Q890,235 915,235 H945 Q970,235 970,210 V0 H1050 V225 Q1050,300 975,300 "
             "H885 Q810,300 810,225 Z", "nonzero"),
]
LETTER_STROKE = 18  # same stroke as the logo; drawn under the fill (paint-order), so 9 px stay visible
HOLES = "M600,52 L690,78 L600,104 Z M600,196 L695,222 L600,248 Z"  # the two notches of the "B"


def wing_points(mirror):
    out = []
    for x, y in WING:
        xx = -(x - 45) if mirror else (x - 45)
        out.append((525 + 0.3 * xx, 260 + 0.62 * (y - 260)))
    return out


def pts(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def emblem(dark=False):
    inner = "#2B2B2B" if dark else WHITE
    hexp = pts(HEX)
    s = [f'<polygon points="{hexp}" fill="{inner}" stroke="{BLACK}" stroke-width="56" stroke-linejoin="round"/>',
         f'<polygon points="{hexp}" fill="none" stroke="{ORANGE}" stroke-width="38" stroke-linejoin="round"/>']
    for m in (False, True):
        p = pts(wing_points(m))
        s.append(f'<polygon points="{p}" fill="{BLACK}" stroke="{BLACK}" stroke-width="16" stroke-linejoin="round"/>')
        s.append(f'<polygon points="{p}" fill="{ORANGE}"/>')
    return "".join(s)


def letters(transform):
    s = [f'<g transform="{transform}">', f'<path d="{HOLES}" fill="{WHITE}"/>']
    for tag, geo, rule in LETTERS:
        attr = "points" if tag == "polygon" else "d"
        s.append(f'<{tag} {attr}="{geo}" fill="{BLACK}" fill-rule="{rule}" stroke="{BLACK}" '
                 f'stroke-width="{LETTER_STROKE}" stroke-linejoin="round"/>')
    for tag, geo, rule in LETTERS:
        attr = "points" if tag == "polygon" else "d"
        s.append(f'<{tag} {attr}="{geo}" fill="{ORANGE}" fill-rule="{rule}"/>')
    s.append("</g>")
    return "".join(s)


# Emblem bounding box incl. the 56 px hexagon stroke
EX0, EY0, EX1, EY1 = 272, -28, 778, 548


def svg(vb, w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="{vb}">'
            f"{body}</svg>\n")


def icon_svg(w, h, pad=10, dark=False):
    x0, y0, x1, y1 = EX0 - pad, EY0 - pad, EX1 + pad, EY1 + pad
    bw, bh = x1 - x0, y1 - y0
    # fit the requested aspect ratio, centred
    aspect = w / h
    if bw / bh < aspect:
        nw = bh * aspect
        x0 -= (nw - bw) / 2
        bw = nw
    else:
        nh = bw / aspect
        y0 -= (nh - bh) / 2
        bh = nh
    return svg(f"{x0:.2f} {y0:.2f} {bw:.2f} {bh:.2f}", w, h, emblem(dark))


def horizontal_svg(w, h, dark=False):
    # emblem (506 x 576) + "ZEBU" (1050 x 300) scaled 0.62, vertically centred
    s = 0.62
    tx = EX1 + 70
    ty = EY0 + (EY1 - EY0 - 300 * s) / 2
    x0, y0 = EX0 - 20, EY0 - 20
    x1 = tx + 1050 * s + 40
    y1 = EY1 + 20
    bw, bh = x1 - x0, y1 - y0
    aspect = w / h
    if bw / bh < aspect:
        nw = bh * aspect
        x0 -= (nw - bw) / 2
        bw = nw
    else:
        nh = bw / aspect
        y0 -= (nh - bh) / 2
        bh = nh
    body = emblem(dark) + letters(f"translate({tx:.2f},{ty:.2f}) scale({s})")
    return svg(f"{x0:.2f} {y0:.2f} {bw:.2f} {bh:.2f}", w, h, body)


def full_svg():
    body = emblem() + letters("translate(0,600)")
    return svg("-60 -60 1170 1030", 1170, 1030, body)


def render(svg_text, w, h):
    png = subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h)], input=svg_text.encode(),
                         capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
    img = os.path.join(root, "resources", "images")
    os.makedirs(os.path.join(root, "resources", "zebu"), exist_ok=True)

    square = icon_svg(1024, 1024)
    narrow = icon_svg(815, 1024)

    svgs = {
        "OrcaSlicer.svg": icon_svg(64, 64),
        "OrcaSlicer_gradient.svg": square,
        "OrcaSlicer_gradient_circle.svg": square,
        "OrcaSlicer_gradient_narrow.svg": narrow,
        "OrcaSlicer_gray.svg": square,
        "OrcaSlicer_about.svg": horizontal_svg(560, 125),
        "OrcaSlicer_about_dark.svg": horizontal_svg(560, 125, dark=True),
        "OrcaSlicer_horizontal_light.svg": horizontal_svg(214, 80),
        "OrcaSlicer_horizontal_dark.svg": horizontal_svg(214, 80, dark=True),
    }
    for name, text in svgs.items():
        with open(os.path.join(img, name), "w") as f:
            f.write(text)
    with open(os.path.join(root, "resources", "zebu", "zebu_logo.svg"), "w") as f:
        f.write(full_svg())

    pngs = {
        "OrcaSlicer-mac_128px.png": 128, "OrcaSlicer.png": 154, "OrcaSlicerTitle.png": 154,
        "OrcaSlicer_128px.png": 128, "OrcaSlicer_154.png": 154, "OrcaSlicer_154_title.png": 184,
        "OrcaSlicer_192px.png": 192, "OrcaSlicer_192px_transparent.png": 192,
        "OrcaSlicer_32px.png": 32, "OrcaSlicer_64.png": 64,
    }
    for name, size in pngs.items():
        render(square, size, size).save(os.path.join(img, name))
    gray = render(square, 192, 192)
    alpha = gray.getchannel("A")
    gray = gray.convert("L").convert("RGBA")
    gray.putalpha(alpha)
    gray.save(os.path.join(img, "OrcaSlicer_192px_grayscale.png"))

    big = render(square, 1024, 1024)
    big.save(os.path.join(img, "OrcaSlicer.ico"),
             sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(os.path.join(img, "OrcaSlicer-mac_256px.ico"), sizes=[(256, 256)])
    render(square, 154, 154).save(os.path.join(img, "OrcaSlicerTitle.ico"), sizes=[(154, 154)])
    big.save(os.path.join(img, "OrcaSlicer.icns"))
    render(full_svg(), 1170, 1030).save(os.path.join(root, "resources", "zebu", "zebu_logo.png"))
    print("branding assets written to", img)


if __name__ == "__main__":
    main()
