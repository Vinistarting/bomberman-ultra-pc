#!/usr/bin/env python3
"""Gera o ícone do jogo e do launcher a partir do ICON0.PNG do usuário.

  python tools/make_icon.py <game/PS3_GAME/ICON0.PNG> <saida.ico> [<outra_saida.ico> ...]

A arte do jogo não fica no repositório: o .ico é feito na hora do build a
partir do ICON0.PNG que vem no PKG do próprio usuário. Com o Pillow
instalado (pip install pillow) sai um ícone de 16 a 256 px com a arte do
jogo; sem ele, sai um ícone genérico (uma bomba desenhada aqui), para o build
nunca depender disso.
"""
import struct
import sys
import zlib


def generic_png(size=64):
    """Uma bomba preta com pavio, RGBA, sem dependências."""
    px = bytearray()
    c, r = size * 0.45, size * 0.36
    for y in range(size):
        px.append(0)                                  # filtro PNG "none"
        for x in range(size):
            dx, dy = x + 0.5 - c, y + 0.5 - (size * 0.58)
            d = (dx * dx + dy * dy) ** 0.5
            if d <= r:
                hl = (x + 0.5 - (c - r * 0.35)) ** 2 + (y + 0.5 - (size * 0.58 - r * 0.35)) ** 2 <= (r * 0.22) ** 2
                px += bytes((200, 210, 235, 255)) if hl else bytes((30, 30, 45, 255))
            elif abs(x - (c + r * 0.55)) < size * 0.06 and size * 0.10 < y < size * 0.30:
                px += bytes((150, 110, 60, 255))      # pavio
            elif (x - (c + r * 0.62)) ** 2 + (y - size * 0.10) ** 2 < (size * 0.08) ** 2:
                px += bytes((255, 190, 40, 255))      # faísca
            else:
                px += bytes((0, 0, 0, 0))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(px), 9)) + chunk(b'IEND', b''))


def ico_from_pngs(pngs):
    """ICO com imagens PNG embutidas (Windows Vista+)."""
    head = struct.pack('<HHH', 0, 1, len(pngs))
    off = 6 + 16 * len(pngs)
    dirs, data = b'', b''
    for size, png in pngs:
        dirs += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(png), off)
        off += len(png)
        data += png
    return head + dirs + data


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, outs = sys.argv[1], sys.argv[2:]
    try:
        from PIL import Image
        import io
        im = Image.open(src).convert('RGBA')
        side = max(im.size)
        sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
        sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
        pngs = []
        for s in (256, 64, 48, 32, 16):
            b = io.BytesIO()
            sq.resize((s, s), Image.LANCZOS).save(b, 'PNG')
            pngs.append((s, b.getvalue()))
        ico = ico_from_pngs(pngs)
        how = 'arte do ICON0.PNG'
    except Exception as e:                              # sem Pillow ou sem ICON0
        ico = ico_from_pngs([(64, generic_png(64))])
        how = f'genérico ({e.__class__.__name__})'
    for o in outs:
        open(o, 'wb').write(ico)
        print(f'{o}: ícone {how}')


if __name__ == '__main__':
    main()
