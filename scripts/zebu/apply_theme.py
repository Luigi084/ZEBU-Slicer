#!/usr/bin/env python3
"""Replace OrcaSlicer's teal accent with the ZEBU orange everywhere (C++ sources, SVG icons, web pages).

Re-runnable and idempotent: run it again after merging upstream OrcaSlicer changes.
    scripts/zebu/apply_theme.py [repo_root]
"""
import os
import re
import sys

# OrcaSlicer colour -> ZEBU colour. Light theme colours and their dark-mode counterparts (StateColor.cpp map).
HEX = {
    "009688": "F7821B",  # accent (buttons, selection, progress)
    "26A69A": "F99A45",  # accent hover
    "00675B": "C46410",  # accent pressed / dark-mode accent
    "008172": "D9731A",  # dark-mode accent hover
    "00897B": "E07614",  # accent, darker variant
    "009789": "F7821B",  # accent as used in the old splash
    "BFE1DE": "FDDCBF",  # 25 % accent tint (checked items)
    "E5F0EE": "FEF0E3",  # 10 % accent tint (focused items)
    "223C3C": "4A3322",  # dark-mode 25 % tint
    "283232": "3A2E25",  # dark-mode 10 % tint
}


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


RGB = {rgb(k): rgb(v) for k, v in HEX.items()}

HEX_RE = re.compile(r"(#|0x)(" + "|".join(HEX) + r")\b", re.IGNORECASE)
# wxColour(0, 150, 136), wxColour(0x00, 0x96, 0x88), rgb(0,150,136), rgba(0, 150, 136, 0.5)
NUM = r"(0x[0-9a-fA-F]{1,2}|\d{1,3})"
RGB_RE = re.compile(r"(wxColour\s*\(|rgba?\s*\()\s*" + NUM + r"(\s*,\s*)" + NUM + r"(\s*,\s*)" + NUM)


def _num(s):
    return int(s, 16) if s.lower().startswith("0x") else int(s)


def sub_hex(m):
    new = HEX[m.group(2).upper()]
    # keep the original letter case style
    if m.group(2).islower() or (m.group(2).isdigit() and False):
        new = new.lower()
    return m.group(1) + new


def sub_rgb(m):
    key = (_num(m.group(2)), _num(m.group(4)), _num(m.group(6)))
    if key not in RGB:
        return m.group(0)
    r, g, b = RGB[key]
    hexa = m.group(2).lower().startswith("0x")
    f = (lambda v: "0x%02X" % v) if hexa else str
    return m.group(1) + f(r) + m.group(3) + f(g) + m.group(5) + f(b)


TARGETS = [
    ("src", (".cpp", ".hpp", ".h")),
    ("resources/images", (".svg",)),
    ("resources/web", (".css", ".js", ".html", ".svg")),
]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
    changed = 0
    for sub, exts in TARGETS:
        for dirpath, _, files in os.walk(os.path.join(root, sub)):
            for name in files:
                if not name.endswith(exts):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "rb") as f:
                    raw = f.read()
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError:
                    continue
                new = RGB_RE.sub(sub_rgb, HEX_RE.sub(sub_hex, text))
                if new != text:
                    with open(path, "wb") as f:
                        f.write(new.encode("utf-8"))
                    changed += 1
    print(f"ZEBU theme applied, {changed} files changed")


if __name__ == "__main__":
    main()
