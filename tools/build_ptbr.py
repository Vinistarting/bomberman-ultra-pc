#!/usr/bin/env python3
"""Gera traducao/strings_ptbr.xus a partir de traducao/pt-br.tsv.

  python tools/build_ptbr.py <strings_en.xus original> [saida]

O .xus é a tabela de strings XUI do jogo: 'XUIS', u16 versão (0x0100),
u32 tamanho total, u16 número de entradas, e para cada entrada
(texto, chave) como u16 comprimento + UTF-16BE. A tradução mantém a ordem
e as chaves do inglês; chave sem tradução no TSV fica em inglês.
No TSV, ⏎ = quebra de linha (\\r\\n). Tokens %c, %1, %2 e os ícones de
botão (caracteres U+0B00..U+0B7F) precisam ficar intactos.
"""
import struct, sys, os

def read_xus(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'XUIS', 'não é um .xus'
    cnt = struct.unpack('>H', d[10:12])[0]
    p, out = 12, []
    def rd(p):
        n = struct.unpack('>H', d[p:p + 2])[0]
        return d[p + 2:p + 2 + 2 * n].decode('utf-16-be'), p + 2 + 2 * n
    for _ in range(cnt):
        v, p = rd(p); k, p = rd(p); out.append((k, v))
    return out

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    tsv = os.path.join(here, '..', 'traducao', 'pt-br.tsv')
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'traducao', 'strings_ptbr.xus')
    en = read_xus(sys.argv[1])
    pt = {}
    for line in open(tsv, encoding='utf-8'):
        line = line.rstrip('\n')
        if line:
            k, v = line.split('\t', 1)
            pt[k] = v.replace('⏎', '\r\n')
    body = bytearray()
    for k, v in en:
        for s in (pt.get(k, v), k):
            e = s.encode('utf-16-be'); body += struct.pack('>H', len(e) // 2) + e
    out = b'XUIS' + struct.pack('>HIH', 0x0100, 12 + len(body), len(en)) + body
    open(dst, 'wb').write(out)
    print(f'{dst}: {len(en)} entradas, {sum(k in pt for k, _ in en)} traduzidas')

if __name__ == '__main__':
    main()
