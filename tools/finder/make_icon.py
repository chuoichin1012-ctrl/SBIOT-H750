"""Builds FinderTool's brand images in Sbiot.Finder/Resources from docs/SBIOT.ai (Illustrator; a PDF inside, page 2
is the full-colour logo on a transparent page). Rendered by poppler's pdftocairo with a transparent background, so
neither image has a white margin (the user, 2026-10-17). Run again only when the logo changes:

    python3 make_icon.py

- sbiot.ico: the exe and window icon, the "Sb" square alone, rendered at each icon size (16 to 256 px). An .ico may
  hold PNG images as they are (Windows Vista and later), so no image library is needed;
- sbiot-logo.png: the header's logo (the square, SBIOT and the tagline) at twice its on-screen height (about 46 px),
  so it stays sharp at 200 %."""
import os
import struct
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
AI = os.path.join(HERE, '../../../docs/SBIOT.ai')
OUT = os.path.join(HERE, 'Sbiot.Finder/Resources')
# On page 2, in points: the "Sb" square spans x 123.0-305.9; its two colour blocks y 396.8-570.4, so a square box
# centred on them is y 392.2-575.1 (as sbiot-logo-square.svg's viewBox). The whole logo ends at x 820, y 571.
SQ_X, SQ_Y, SQ = 122.995, 392.14, 182.89
SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(dpi, x, y, w, h, out):
    subprocess.run(['pdftocairo', '-f', '2', '-l', '2', '-r', '%.4f' % dpi, '-x', str(x), '-y', str(y), '-W', str(w),
                    '-H', str(h), '-png', '-transp', '-singlefile', AI, out], check=True)


images = []
with tempfile.TemporaryDirectory() as tmp:
    for s in SIZES:
        dpi = s * 72.0 / SQ
        base = os.path.join(tmp, 'i%d' % s)
        render(dpi, round(SQ_X * dpi / 72), round(SQ_Y * dpi / 72), s, s, base)
        images.append((s, open(base + '.png', 'rb').read()))
head = struct.pack('<HHH', 0, 1, len(images))
offset = 6 + 16 * len(images)
entries, blobs = b'', b''
for s, data in images:
    entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
    blobs += data
open(os.path.join(OUT, 'sbiot.ico'), 'wb').write(head + entries + blobs)
render(36, 58, 196, 356, 92, os.path.join(OUT, 'sbiot-logo'))  # 36 dpi: the logo is 349 x 88 px, 3-4 px around it
print('sbiot.ico: %s px; sbiot-logo.png' % ', '.join(str(s) for s, _ in images))
