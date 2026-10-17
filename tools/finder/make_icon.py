"""Builds the tool's brand images in Sbiot.Finder/Resources (run again only when the logo changes):
- sbiot.ico, the exe and window icon, from the SBIOT app icons (assets/app-icons/sbiot, made from
  sbiot-logo-square.svg). An .ico may hold PNG images as they are (Windows Vista and later): no image library;
- sbiot-logo.png, the header's full-colour logo, from page 2 of docs/SBIOT.ai (Illustrator, PDF inside), rendered
  and cropped by poppler's pdftoppm at twice its on-screen height (about 46 px), so it stays sharp at 200 %.

    python3 make_icon.py"""
import os
import struct
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '../../../assets/app-icons/sbiot')
PNGS = ['ios/Assets.xcassets/AppIcon.appiconset/icon-29.png', 'ios/Assets.xcassets/AppIcon.appiconset/icon-40.png'] + \
       ['android/res/mipmap-%s/ic_launcher.png' % d for d in ('mdpi', 'hdpi', 'xhdpi', 'xxhdpi', 'xxxhdpi')]
OUT = os.path.join(HERE, 'Sbiot.Finder/Resources')

images = []
for p in PNGS:
    data = open(os.path.join(SRC, p), 'rb').read()
    w, h = struct.unpack('>II', data[16:24])
    images.append((w, h, data))
head = struct.pack('<HHH', 0, 1, len(images))
offset = 6 + 16 * len(images)
entries, blobs = b'', b''
for w, h, data in images:
    entries += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
    blobs += data
open(os.path.join(OUT, 'sbiot.ico'), 'wb').write(head + entries + blobs)
# Page 2 is the logo on white; at 36 dpi its box is about (61, 198) to (410, 285): cropped with a few pixels around it.
subprocess.run(['pdftoppm', '-f', '2', '-l', '2', '-r', '36', '-x', '56', '-y', '194', '-W', '360', '-H', '96', '-png',
                '-singlefile', os.path.join(HERE, '../../../docs/SBIOT.ai'), os.path.join(OUT, 'sbiot-logo')], check=True)
print('sbiot.ico: %s px' % ', '.join(str(w) for w, _, _ in images))
