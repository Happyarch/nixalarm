import subprocess
from PIL import Image
import numpy as np

SRC = '/tmp/nixalarm-logo-non-indexed.png'
OUT_SVG = '/tmp/nixalarm-icon/nixalarm-icon.svg'

im = Image.open(SRC).convert('RGBA')
arr = np.array(im)
H, W = arr.shape[:2]

# (name, rgba, hex, dilate?)  -- bottom to top draw order.
# fill colors get dilated 1px so they bleed slightly UNDER their neighbours;
# black (the linework/outline layer) is left exact and drawn last on top,
# so any potrace curve-fit seam between layers is always hidden under black
# or under the (also dilated) fill beneath it -- never a visible gap.
LAYERS = [
    ('cream',  (253, 237, 201, 255), '#fdedc9', True),
    ('orange', (253, 127,  58, 255), '#fd7f3a', True),
    ('blue',   ( 93, 151, 233, 255), '#5d97e9', True),
    ('white',  (255, 255, 255, 255), '#ffffff', True),
    ('black',  (  0,   0,   0, 255), '#000000', False),
]

def dilate(mask, iters=1):
    m = mask.copy()
    for _ in range(iters):
        m = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    return m

transform = None
groups = []

for name, rgba, hexcolor, do_dilate in LAYERS:
    mask = np.all(arr == np.array(rgba, dtype=np.uint8), axis=-1)
    if mask.sum() == 0:
        continue
    if do_dilate:
        mask = dilate(mask, 1)
    pgm_path = f'/tmp/nixalarm-icon/{name}.pgm'
    svg_path = f'/tmp/nixalarm-icon/{name}.svg'
    Image.fromarray(np.where(mask, 0, 255).astype(np.uint8), mode='L').save(pgm_path)
    subprocess.run(
        ['potrace', '-s', '-a', '1.3', '-O', '0.3', '-t', '2',
         '-o', svg_path, pgm_path],
        check=True,
    )
    content = open(svg_path).read()
    g_start = content.index('<g transform="')
    t_start = g_start + len('<g transform="')
    t_end = content.index('"', t_start)
    this_transform = content[t_start:t_end]
    if transform is None:
        transform = this_transform
    elif this_transform != transform:
        raise SystemExit(f'transform mismatch on layer {name}: {this_transform!r} != {transform!r}')

    body_start = content.index('>', content.index('fill="#000000" stroke="none">')) + 1
    body_end = content.index('</g>')
    paths = content[body_start:body_end].strip()
    groups.append(f'<g fill="{hexcolor}">\n{paths}\n</g>')

svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">
<g transform="{transform}" stroke="none">
{chr(10).join(groups)}
</g>
</svg>
'''
open(OUT_SVG, 'w').write(svg)
print('wrote', OUT_SVG, len(svg), 'bytes')
