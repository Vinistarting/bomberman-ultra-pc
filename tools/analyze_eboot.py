#!/usr/bin/env python3
"""Análise estrutural do EBOOT.elf (PPU) do Bomberman Ultra.

Gera em analysis/:
  elf_analysis.json   segmentos, seções, entry/TOC, proc_param, TLS
  imports.json        módulos importados, NIDs, nomes resolvidos, stubs
  opd_functions.json  descritores .opd (entrada de função + TOC)
  functions.json      OPD + alvos de 'bl' + prólogos (stwu/stdu r1) -> limites
  nids.txt            um NID por linha (para o nid_database do ps3recomp)

Uso: python analyze_eboot.py game/EBOOT.elf analysis/ [--names nomes.txt]
  --names: arquivo com um nome de função por linha para resolver NIDs
           (NID = SHA1(nome + sufixo)[0:4] little-endian)
"""
import hashlib, json, os, struct, sys, argparse, bisect

NID_SUFFIX = bytes.fromhex("6759659904250490566427499489741A")
def nid_of(name):
    return struct.unpack("<I", hashlib.sha1(name.encode() + NID_SUFFIX).digest()[:4])[0]

class Elf:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        self.entry, = struct.unpack(">Q", d[0x18:0x20])
        phoff, shoff = struct.unpack(">QQ", d[0x20:0x30])
        phnum, = struct.unpack(">H", d[0x38:0x3A]); shnum, shstr = struct.unpack(">HH", d[0x3C:0x40])
        self.ph = [dict(zip(("type","flags","offset","vaddr","paddr","filesz","memsz","align"),
                   struct.unpack(">IIQQQQQQ", d[phoff+i*56:phoff+(i+1)*56]))) for i in range(phnum)]
        self.sh = [dict(zip(("name","type","flags","addr","offset","size","link","info","align","entsize"),
                   struct.unpack(">IIQQQQIIQQ", d[shoff+i*64:shoff+(i+1)*64]))) for i in range(shnum)]
    def off(self, va):
        for p in self.ph:
            if p["type"] == 1 and p["vaddr"] <= va < p["vaddr"] + p["filesz"]:
                return p["offset"] + va - p["vaddr"]
        return None
    def u32(self, va):
        o = self.off(va); return struct.unpack(">I", self.d[o:o+4])[0] if o is not None else None
    def cstr(self, va):
        o = self.off(va); return self.d[o:self.d.index(b"\0", o)].decode("latin1")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf"); ap.add_argument("out"); ap.add_argument("--names")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    e = Elf(a.elf)
    names = {}
    if a.names:
        for line in open(a.names, encoding="utf-8"):
            n = line.strip()
            if n: names.setdefault(nid_of(n), n)

    # entry point: o e_entry do PS3 aponta para um OPD (func, toc)
    entry_func, entry_toc = e.u32(e.entry), e.u32(e.entry + 4)

    # proc_param (PT 0x60000001) e prx_param (PT 0x60000002)
    pp = next(p for p in e.ph if p["type"] == 0x60000001)
    px = next(p for p in e.ph if p["type"] == 0x60000002)
    pp_raw = struct.unpack(">8I", e.d[pp["offset"]:pp["offset"]+32])
    proc_param = dict(size=pp_raw[0], magic=hex(pp_raw[1]), version=hex(pp_raw[2]), sdk_version=hex(pp_raw[3]),
                      primary_prio=pp_raw[4], primary_stacksize=hex(pp_raw[5]), malloc_pagesize=hex(pp_raw[6]), ppc_seg=pp_raw[7])
    size, magic, ver, _, libent_s, libent_e, libstub_s, libstub_e = struct.unpack(">8I", e.d[px["offset"]:px["offset"]+32])
    assert magic == 0x1B434CEC, hex(magic)

    imports, all_nids, stubs = [], [], {}
    va = libstub_s
    while va < libstub_e:
        o = e.off(va); s = e.d[o]
        hdr = struct.unpack(">BBHHHHHBB2sIIIIIII", e.d[o:o+0x2C])
        _, _, version, attr, nfunc, nvar, ntls, _, _, _, name_p, nids_p, addrs_p, vnids_p, vstubs_p, _, _ = hdr
        mod = e.cstr(name_p)
        funcs = []
        for i in range(nfunc):
            nid = e.u32(nids_p + 4*i); slot = addrs_p + 4*i
            funcs.append(dict(nid=f"0x{nid:08X}", name=names.get(nid), slot=hex(slot), target=hex(e.u32(slot))))
            all_nids.append(nid)
        vars_ = [dict(nid=f"0x{e.u32(vnids_p+4*i):08X}", name=names.get(e.u32(vnids_p+4*i))) for i in range(nvar)]
        imports.append(dict(module=mod, version=version, attributes=hex(attr), functions=funcs, variables=vars_))
        va += s

    # .opd: seção cujo conteúdo é uma lista de pares (func em .text, toc) — 8 bytes no PS3
    text = next(p for p in e.ph if p["type"] == 1 and p["flags"] & 1)
    tlo, thi = text["vaddr"], text["vaddr"] + text["filesz"]
    opd_sec, best = None, 0
    for s in e.sh:
        if s["type"] != 1 or s["size"] < 64 or s["size"] % 8: continue
        cnt = s["size"] // 8; ok = 0
        for i in range(cnt):
            f, t = struct.unpack(">II", e.d[s["offset"]+i*8:s["offset"]+i*8+8])
            ok += (tlo <= f < thi and t == entry_toc)
        if ok > best and ok > cnt * 0.9: opd_sec, best = s, ok
    opd = []
    if opd_sec:
        for i in range(opd_sec["size"] // 8):
            f, t = struct.unpack(">II", e.d[opd_sec["offset"]+i*8:opd_sec["offset"]+i*8+8])
            if tlo <= f < thi: opd.append(dict(opd=hex(opd_sec["addr"]+i*8), func=hex(f), toc=hex(t)))

    # stubs de import (código): procurar os padrões que leem os slots de import
    slot_set = {int(f["slot"], 16): (m["module"], f) for m in imports for f in m["functions"]}
    # alvos iniciais das entradas de import apontam para os stubs em .text
    for m in imports:
        for f in m["functions"]:
            stubs[int(f["target"], 16)] = f

    # Varredura linear: alvos de 'bl' e prólogos
    starts = set(int(o["func"], 16) for o in opd) | {entry_func}
    bl_targets = set()
    for va in range(tlo, thi, 4):
        o = e.off(va); ins = struct.unpack(">I", e.d[o:o+4])[0]
        op = ins >> 26
        if op == 18 and (ins & 3) == 1:  # bl
            li = ins & 0x03FFFFFC
            if li & 0x02000000: li -= 0x04000000
            t = va + li
            if tlo <= t < thi: bl_targets.add(t)
    starts |= bl_targets
    srt = sorted(starts)
    funcs = []
    for i, s in enumerate(srt):
        end = srt[i+1] if i + 1 < len(srt) else thi
        funcs.append(dict(start=hex(s), end=hex(end), size=end - s,
                          src=("opd" if s in {int(o["func"],16) for o in opd} else "bl"),
                          import_stub=stubs.get(s, {}).get("name") or (stubs[s]["nid"] if s in stubs else None)))

    # strings de assets referenciadas
    secs = [dict(idx=i, addr=hex(s["addr"]), offset=hex(s["offset"]), size=hex(s["size"]), type=s["type"], flags=hex(s["flags"])) for i, s in enumerate(e.sh)]
    if opd_sec: secs[e.sh.index(opd_sec)]["guess"] = ".opd"
    elf_analysis = dict(
        entry_opd=hex(e.entry), entry_func=hex(entry_func), toc=hex(entry_toc),
        segments=[{k: (hex(v) if k != "type" else hex(v)) for k, v in p.items()} for p in e.ph],
        sections=secs, proc_param=proc_param,
        prx_param=dict(libent=[hex(libent_s), hex(libent_e)], libstub=[hex(libstub_s), hex(libstub_e)]),
        counts=dict(opd=len(opd), bl_targets=len(bl_targets), functions=len(funcs),
                    imported_modules=len(imports), imported_functions=sum(len(m["functions"]) for m in imports),
                    resolved=sum(1 for m in imports for f in m["functions"] if f["name"])))
    json.dump(elf_analysis, open(f"{a.out}/elf_analysis.json", "w"), indent=2)
    json.dump(imports, open(f"{a.out}/imports.json", "w"), indent=2)
    json.dump(opd, open(f"{a.out}/opd_functions.json", "w"), indent=1)
    json.dump(funcs, open(f"{a.out}/functions.json", "w"), indent=1)
    open(f"{a.out}/nids.txt", "w").write("".join(f"0x{n:08X}\n" for n in sorted(set(all_nids))))
    print(json.dumps(elf_analysis["counts"], indent=1))
    print("entry", elf_analysis["entry_func"], "toc", elf_analysis["toc"])
    for m in imports:
        r = sum(1 for f in m["functions"] if f["name"])
        print(f"  {m['module']:<20} {len(m['functions']):>4} funcs ({r} nomeadas) {len(m['variables'])} vars")

if __name__ == "__main__":
    main()
