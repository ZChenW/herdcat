#!/usr/bin/env python3
"""Regenerate split-post designs and the two README images with production C."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'docs/design/post-split'
OUTPUT.mkdir(parents=True, exist_ok=True)
sources = [ROOT / 'scripts/post_split_render.c', ROOT / 'src/config/nameplate.c',
           ROOT / 'src/core/agent_adapters.c', ROOT / 'src/core/agent_state.c',
           ROOT / 'src/utils/error.c', ROOT / 'src/graphics/text.c',
           ROOT / 'src/graphics/post_text_layout.c',
           ROOT / 'src/graphics/nameplate_layout.c',
           ROOT / 'src/graphics/animation.c', ROOT / 'src/graphics/embedded_assets.c']
sources += sorted((ROOT / 'src/graphics').glob('sign*.c'))
flags = subprocess.check_output(
    ['pkg-config', '--cflags', '--libs', 'freetype2', 'fontconfig'], text=True).split()


def cropped(image):
    background = Image.new('RGB', image.size, image.getpixel((0, 0)))
    box = ImageChops.difference(image, background).getbbox()
    return image.crop((max(0, box[0] - 24), max(0, box[1] - 24),
                       min(image.width, box[2] + 24), min(image.height, box[3] + 24)))


with tempfile.TemporaryDirectory(prefix='herdcat-post-split-render-') as temporary:
    binary = Path(temporary) / 'render'
    subprocess.run(['gcc', '-std=c2x', '-O2', '-Iinclude', '-Ilib',
                    *map(str, sources), '-o', str(binary), '-lm', *flags],
                   cwd=ROOT, check=True)
    subprocess.run([str(binary), temporary], cwd=ROOT, check=True)
    panels = []
    for path in sorted(Path(temporary).glob('*.ppm')):
        with Image.open(path) as source:
            picture = source.copy()
        if path.stem == 'readme-post':
            picture.save(ROOT / 'docs/screenshots/session-signs/post.png')
        elif path.stem.startswith('readme-'):
            panels.append((path.stem, cropped(picture)))
        else:
            picture.save(OUTPUT / path.with_suffix('.png').name)
    panels.sort()
    panel_width = max(picture.width for _, picture in panels)
    panel_height = max(picture.height for _, picture in panels)
    comparison = Image.new('RGB', (panel_width * 2 + 24, panel_height + 48), '#f1f2f6')
    draw = ImageDraw.Draw(comparison)
    font = ImageFont.truetype('DejaVuSans.ttf', 22)
    for index, (name, picture) in enumerate(panels):
        left = index * (panel_width + 24)
        comparison.paste(picture, (left + (panel_width - picture.width) // 2,
                                   panel_height - picture.height))
        draw.text((left + panel_width / 2, panel_height + 16),
                  'fan' if name == 'readme-fan' else 'post',
                  fill='#4a5261', font=font, anchor='mt')
    comparison.save(ROOT / 'docs/screenshots/options/style.png')
