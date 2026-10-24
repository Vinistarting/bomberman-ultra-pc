#!/usr/bin/env python3
"""Extrai os dois jobs SPURS embutidos no EBOOT do Bomberman ULTRA.

  python tools/extract_spu_jobs.py <EBOOT.elf decriptado> [pasta de saída]

Os jobs de renderização ("crTaskRunJobList") não são ELFs dentro do EBOOT,
e sim binários SPU crus carregados no LS 0:

  vaddr 0x00199A80, 0xA500 bytes  -> spu_jobs/job_199a80.elf  (render/scene)
  vaddr 0x001A4000, 0x02E0 bytes  -> spu_jobs/job_1a4000.elf

Cada um é embrulhado num ELF SPU mínimo (um PT_LOAD em 0, seções .text e
.shstrtab) para o spu_lifter/build_spu_workloads do ps3recomp. Nada do jogo
fica no repositório: estes arquivos são gerados a partir do EBOOT do próprio
usuário.
"""
import os
import struct
import sys

JOBS = [(0x00199A80, 0xA500), (0x001A4000, 0x02E0)]


def vaddr_to_off(elf, va):
    phoff = struct.unpack('>Q', elf[0x20:0x28])[0]
    phnum = struct.unpack('>H', elf[0x38:0x3A])[0]
    for i in range(phnum):
        t, _, off, pva, _, fsz, _, _ = struct.unpack('>IIQQQQQQ', elf[phoff + 56 * i:phoff + 56 * i + 56])
        if t == 1 and pva <= va < pva + fsz:
            return off + (va - pva)
    raise SystemExit(f'vaddr {va:#x} fora dos segmentos do EBOOT')


def wrap(body):
    shstr = b'\0.text\0.shstrtab\0'
    body_off = 0x60
    shstr_off = body_off + len(body)
    shoff = (shstr_off + len(shstr) + 3) & ~3
    eh = (b'\x7fELF' + bytes([1, 2, 1]) + b'\0' * 9 +
          struct.pack('>HHIIIIIHHHHHH', 2, 0x17, 1, 0, 0x34, shoff, 0, 0x34, 0x20, 1, 0x28, 3, 2))
    ph = struct.pack('>8I', 1, body_off, 0, 0, len(body), len(body), 5, 0x10)
    out = bytearray(eh + ph)
    out += b'\0' * (body_off - len(out))
    out += body + shstr
    out += b'\0' * (shoff - len(out))
    out += struct.pack('>10I', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0)                   # SHN_UNDEF (align 1, como o original)
    out += struct.pack('>10I', 1, 1, 6, 0, body_off, len(body), 0, 0, 0x10, 0)   # .text
    out += struct.pack('>10I', 7, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)   # .shstrtab
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    elf = open(sys.argv[1], 'rb').read()
    if elf[:4] != b'\x7fELF':
        raise SystemExit('isso não é o EBOOT.elf decriptado')
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'spu_jobs')
    os.makedirs(out_dir, exist_ok=True)
    for va, size in JOBS:
        off = vaddr_to_off(elf, va)
        path = os.path.join(out_dir, f'job_{va:06x}.elf')
        open(path, 'wb').write(wrap(elf[off:off + size]))
        print(f'{path}: {size} bytes de {va:#010x}')


if __name__ == '__main__':
    main()
