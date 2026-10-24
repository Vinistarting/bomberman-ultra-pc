#!/usr/bin/env python3
"""Decriptador de EBOOT.BIN (SELF NPDRM) -> EBOOT.elf usando o RAP do usuário.

Port em Python da lógica de rpcs3/Crypto/unself.cpp. As chaves NÃO ficam neste
arquivo: são lidas em tempo de execução do código-fonte do RPCS3
(rpcs3/Crypto/key_vault.h e key_vault.cpp), passado via --rpcs3-crypto.

Uso:
  python unself_npdrm.py EBOOT.BIN EBOOT.elf --rap EP...rap --rpcs3-crypto <dir>
Alternativa sem este script: RPCS3 > Utilitários > Decrypt PS3 Binaries.
"""
import argparse, re, struct, zlib
import aes_compat

def load_keys(crypto_dir):
    h = open(f"{crypto_dir}/key_vault.h", encoding="utf-8").read()
    cpp = open(f"{crypto_dir}/key_vault.cpp", encoding="utf-8").read()
    def arr(name):
        m = re.search(r"constexpr u8 %s\[[^\]]*\]\s*=\s*\{([^}]*)\}" % name, h)
        return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    consts = {n: arr(n) for n in ("NP_KLIC_KEY", "RAP_KEY", "RAP_PBOX", "RAP_E1", "RAP_E2")}
    selfkeys = {}
    for fn in ("LoadSelfAPPKeys", "LoadSelfNPDRMKeys"):
        body = re.search(r"void KeyVault::%s\(\)\s*\{(.*?)\n\}" % fn, cpp, re.S).group(1)
        for m in re.finditer(r"emplace_back\(\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(KEY_\w+),\s*\"([0-9A-F]+)\",\s*\"([0-9A-F]+)\"", body):
            selfkeys[(m.group(4), int(m.group(3), 16))] = (bytes.fromhex(m.group(5)), bytes.fromhex(m.group(6)))
    return consts, selfkeys

def rap_to_rif(rap, K):
    key = bytearray(aes_compat.cbc_dec(K["RAP_KEY"], b"\0" * 16, rap))
    pbox, e1, e2 = K["RAP_PBOX"], K["RAP_E1"], K["RAP_E2"]
    for _ in range(5):
        for i in range(16):
            key[pbox[i]] ^= e1[pbox[i]]
        for i in range(15, 0, -1):
            key[pbox[i]] ^= key[pbox[i - 1]]
        o = 0
        for i in range(16):
            p = pbox[i]
            kc = (key[p] - o) & 0xFF
            ec2 = e2[p]
            if o != 1 or kc != 0xFF:
                o = 1 if kc < ec2 else 0
                key[p] = (kc - ec2) & 0xFF
            elif kc == 0xFF:
                key[p] = (kc - ec2) & 0xFF
            else:
                key[p] = kc
    return bytes(key)

KEYTYPE = {4: "KEY_APP", 8: "KEY_NPDRM"}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("self_in"); ap.add_argument("elf_out")
    ap.add_argument("--rap", required=True); ap.add_argument("--rpcs3-crypto", required=True)
    a = ap.parse_args()
    K, SK = load_keys(a.rpcs3_crypto)
    d = open(a.self_in, "rb").read()
    magic, hver, flags, stype, meta, hsize, esize = struct.unpack(">IIHHIQQ", d[:0x20])
    assert magic == 0x53434500, "não é SELF"
    ext = struct.unpack(">10Q", d[0x20:0x70])
    _, progid_off, ehdr_off, phdr_off, shdr_off, segext_off, ver_off, sup_off, sup_size, _ = ext
    authid, vendor, ptype, pver = struct.unpack(">QIIQ", d[progid_off:progid_off + 24])
    print(f"SELF rev=0x{flags:X} program_type={ptype} authid=0x{authid:X} sdk=0x{pver:X}")

    ehdr = d[ehdr_off:ehdr_off + 0x40]
    assert ehdr[4] == 2, "apenas ELF64 (PPU)"
    e_phoff, e_shoff = struct.unpack(">QQ", ehdr[0x20:0x30])
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack(">HHHH", ehdr[0x36:0x3E])
    phdrs = [struct.unpack(">IIQQQQQQ", d[phdr_off + i * 0x38: phdr_off + (i + 1) * 0x38]) for i in range(e_phnum)]

    # NPD header (supplemental type 3)
    npd = None; pos = sup_off
    while pos < sup_off + sup_size:
        t, sz, nxt = struct.unpack(">IIQ", d[pos:pos + 16])
        if t == 3: npd = d[pos + 16: pos + 16 + 0x80]
        pos += sz
    license = struct.unpack(">i", npd[8:12])[0] if npd else None
    cid = npd[16:64].rstrip(b"\0").decode() if npd else ""
    print(f"NPD license={license} content_id={cid}")

    erk, riv = SK[(KEYTYPE[ptype], flags)]
    info = d[0x20 + meta: 0x20 + meta + 0x40]
    if npd:
        rif = rap_to_rif(open(a.rap, "rb").read(16), K)
        klic = aes_compat.ecb_dec(K["NP_KLIC_KEY"], rif)
        info = aes_compat.cbc_dec(klic, b"\0" * 16, info)
    info = aes_compat.cbc_dec(erk, riv, info)
    key, kpad, iv, ivpad = info[:16], info[16:32], info[32:48], info[48:64]
    assert kpad[0] == 0 and ivpad[0] == 0, "falha ao decriptar metadata (RAP errado?)"

    mh = d[0x20 + meta + 0x40: hsize]
    mh = aes_compat.ctr(key, iv, mh)
    _, _, nsec, nkeys, _, _, _ = struct.unpack(">QIIIIII", mh[:0x20])
    secs = [struct.unpack(">QQIIIIIIII", mh[0x20 + i * 0x30: 0x50 + i * 0x30]) for i in range(nsec)]
    kbase = 0x20 + nsec * 0x30
    keys = [mh[kbase + i * 16: kbase + (i + 1) * 16] for i in range(nkeys)]

    out = bytearray(max(e_shoff + e_shnum * e_shentsize, max(p[2] + p[5] for p in phdrs)))
    out[0:0x40] = ehdr
    out[e_phoff:e_phoff + e_phnum * 0x38] = d[phdr_off:phdr_off + e_phnum * 0x38]
    for (off, size, typ, pidx, hashed, sha, enc, kidx, ividx, comp) in secs:
        if typ != 2: continue
        data = d[off:off + size]
        if enc == 3:
            data = aes_compat.ctr(keys[kidx], keys[ividx], data)
        if comp == 2:
            data = zlib.decompress(data)
        p_off = phdrs[pidx][2]
        out[p_off:p_off + len(data)] = data
        print(f"  seg{pidx}: off=0x{p_off:X} vaddr=0x{phdrs[pidx][3]:X} {len(data)} bytes {'zlib' if comp == 2 else ''}")
    if shdr_off:
        out[e_shoff:e_shoff + e_shnum * e_shentsize] = d[shdr_off:shdr_off + e_shnum * e_shentsize]
    open(a.elf_out, "wb").write(out)
    print(f"OK -> {a.elf_out} ({len(out)} bytes)")

if __name__ == "__main__":
    main()
