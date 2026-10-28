#!/usr/bin/env python3
"""Gera o ícone do jogo e do launcher a partir do ICON0.PNG do usuário.

  python tools/make_icon.py <game/PS3_GAME/ICON0.PNG> <saida.ico> [<outra_saida.ico> ...]

A arte do jogo não fica no repositório: o .ico é feito na hora do build a
partir do ICON0.PNG que vem no PKG do próprio usuário, em 16 a 256 px. Usa o
Pillow se houver; sem ele, um decodificador PNG próprio (só biblioteca
padrão, para o Python embutido do launcher). Se o ICON0 faltar, sai um ícone
genérico (uma bomba desenhada aqui), para o build nunca depender disso.
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


def png_decode(data):
    """PNG 8 bits RGB/RGBA sem entrelaçamento -> (w, h, bytes RGBA). Sem Pillow."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('não é PNG')
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        c = data[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', c)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError('formato de PNG não suportado')
        elif t == b'IDAT':
            idat += c
        pos += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(w):
            o = (y * w + x) * 4
            out[o:o + 3] = line[x * bpp:x * bpp + 3]
            out[o + 3] = line[x * bpp + 3] if bpp == 4 else 255
        prev = line
    return w, h, bytes(out)


def png_encode(w, h, rgba):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def square_resize(w, h, rgba, size):
    """Centraliza num quadrado transparente e reduz por média de área."""
    side = max(w, h)
    ox, oy = (side - w) // 2, (side - h) // 2
    out = bytearray(size * size * 4)
    for ty in range(size):
        y0, y1 = ty * side // size, max((ty + 1) * side // size, ty * side // size + 1)
        for tx in range(size):
            x0, x1 = tx * side // size, max((tx + 1) * side // size, tx * side // size + 1)
            acc = [0, 0, 0, 0]; n = 0
            for sy in range(y0, y1):
                yy = sy - oy
                for sx in range(x0, x1):
                    xx = sx - ox
                    n += 1
                    if 0 <= yy < h and 0 <= xx < w:
                        o = (yy * w + xx) * 4
                        al = rgba[o + 3]
                        acc[0] += rgba[o] * al; acc[1] += rgba[o + 1] * al; acc[2] += rgba[o + 2] * al; acc[3] += al
            o = (ty * size + tx) * 4
            if acc[3]:
                out[o] = acc[0] // acc[3]; out[o + 1] = acc[1] // acc[3]; out[o + 2] = acc[2] // acc[3]
            out[o + 3] = acc[3] // n
    return bytes(out)


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
    except ImportError:                                 # sem Pillow: decodificador próprio
        try:
            w, h, rgba = png_decode(open(src, 'rb').read())
            ico = ico_from_pngs([(s, png_encode(s, s, square_resize(w, h, rgba, s))) for s in (256, 64, 48, 32, 16)])
            how = 'arte do ICON0.PNG (sem Pillow)'
        except Exception as e:
            ico = ico_from_pngs([(64, generic_png(64))])
            how = f'genérico ({e.__class__.__name__})'
    except Exception as e:                              # sem ICON0
        ico = ico_from_pngs([(64, generic_png(64))])
        how = f'genérico ({e.__class__.__name__})'
    for o in outs:
        open(o, 'wb').write(ico)
        print(f'{o}: ícone {how}')


if __name__ == '__main__':
    main()
