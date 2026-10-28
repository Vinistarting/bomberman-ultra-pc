#!/usr/bin/env python3
"""Extrator de PKG retail do PS3 (PSN).

Formato (big-endian):
  0x00 magic 0x7F'PKG'   0x04 revision (0x8000 = retail)   0x06 type (1 = PS3)
  0x08 meta_offset  0x0C meta_count  0x10 header_size
  0x14 item_count   0x18 total_size(u64)
  0x20 data_offset(u64)  0x28 data_size(u64)
  0x30 content_id[0x30]  0x60 digest[0x10]  0x70 data_riv[0x10]

Área de dados criptografada com AES-128-CTR, contador = data_riv + (offset/16).
A chave (PKG_AES_KEY) NÃO fica neste repositório: é lida do código-fonte do
RPCS3 (rpcs3/Crypto/key_vault.h), passado em --rpcs3-crypto, como no
unself_npdrm.py. Alternativa sem scripts: instalar o PKG no RPCS3.
Tabela de itens: item_count * 32 bytes no início da área de dados.
Uso: python extract_pkg.py game.pkg pasta_saida --rpcs3-crypto <rpcs3/rpcs3/Crypto>
"""
import argparse, re, struct, sys, os, json
import aes_compat

PS3_PKG_KEY = b""

def load_pkg_key(crypto_dir):
    h = open(os.path.join(crypto_dir, "key_vault.h"), encoding="utf-8").read()
    m = re.search(r"constexpr u8 PKG_AES_KEY\[[^\]]*\]\s*=\s*\{([^}]*)\}", h)
    if not m:
        raise SystemExit("PKG_AES_KEY não encontrada em key_vault.h")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))

class Pkg:
    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(0x80)
        assert h[:4] == b"\x7fPKG", "não é um PKG"
        self.revision, self.type = struct.unpack(">HH", h[4:8])
        self.meta_off, self.meta_cnt, self.hdr_size, self.item_cnt = struct.unpack(">IIII", h[8:0x18])
        self.total, self.data_off, self.data_size = struct.unpack(">QQQ", h[0x18:0x30])
        self.content_id = h[0x30:0x60].rstrip(b"\0").decode()
        self.riv = int.from_bytes(h[0x70:0x80], "big")
        assert self.revision & 0x8000, "PKG debug não suportado aqui"

    def read(self, rel, size):
        """lê `size` bytes descriptografados a partir de `rel` dentro da área de dados"""
        start = rel & ~0xF
        pad = rel - start
        self.f.seek(self.data_off + start)
        enc = self.f.read(size + pad)
        ctr = ((self.riv + start // 16) & ((1 << 128) - 1)).to_bytes(16, "big")
        return aes_compat.ctr(PS3_PKG_KEY, ctr, enc)[pad:pad + size]

    def meta(self):
        self.f.seek(self.meta_off)
        out = {}
        for _ in range(self.meta_cnt):
            t, s = struct.unpack(">II", self.f.read(8))
            out[t] = self.f.read(s)
        return out

    def items(self):
        tbl = self.read(0, self.item_cnt * 32)
        for i in range(self.item_cnt):
            no, ns, do, ds, flags, _ = struct.unpack(">IIQQII", tbl[i*32:(i+1)*32])
            name = self.read(no, ns).decode("utf-8", "replace")
            yield dict(name=name, offset=do, size=ds, flags=flags)

def extract(pkg_path, root, key, only=None, log=print):
    """Extrai os itens do PKG para root/<nome do item>. only: nomes (relativos,
    com /) a extrair; None = tudo. Devolve a lista de itens extraídos."""
    global PS3_PKG_KEY
    PS3_PKG_KEY = key
    p = Pkg(pkg_path)
    done = []
    for it in p.items():
        kind = it["flags"] & 0xFF
        name = it["name"].replace("\\", "/").lstrip("/")
        if ".." in name.split("/"):
            raise SystemExit(f"nome inválido no PKG: {name}")
        if only is not None and name not in only:
            continue
        dst = os.path.join(root, *name.split("/"))
        if kind == 4:  # diretório
            os.makedirs(dst, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst + ".tmp", "wb") as o:
            pos, left = it["offset"], it["size"]
            while left:
                n = min(left, 8 << 20)
                o.write(p.read(pos, n)); pos += n; left -= n
        os.replace(dst + ".tmp", dst)
        done.append({**it, "kind": kind})
        log(f"   {it['size']:>10} {name}")
    return p, done


def main():
    ap = argparse.ArgumentParser(description="Extrai um PKG retail do PS3")
    ap.add_argument("pkg"); ap.add_argument("out")
    ap.add_argument("--rpcs3-crypto", required=True, help="pasta rpcs3/rpcs3/Crypto do código-fonte do RPCS3")
    a = ap.parse_args()
    key = load_pkg_key(a.rpcs3_crypto)
    p = Pkg(a.pkg)
    print(f"content_id={p.content_id} items={p.item_cnt} data=0x{p.data_size:X} (AES: {aes_compat.BACKEND})")
    root = os.path.join(a.out, p.content_id.split("-")[1][:9])
    p, manifest = extract(a.pkg, root, key)
    meta = {f"0x{k:X}": v.hex() for k, v in p.meta().items()}
    with open(os.path.join(a.out, "pkg_manifest.json"), "w") as m:
        json.dump({"content_id": p.content_id, "meta": meta, "items": manifest}, m, indent=2)

if __name__ == "__main__":
    main()
