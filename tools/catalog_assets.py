#!/usr/bin/env python3
"""Cataloga os assets do Bomberman Ultra (USRDIR) e opcionalmente extrai o media.zip.

Uso:
  python catalog_assets.py <USRDIR> <saida.json> [--extrair <pasta>]

Formatos identificados (engine Hudson "SG" + XUI portado do Xbox 360):
  *.texture.ps3  magic 'SG28PT00' - textura SceneGraph. Após o nome (C-string):
                 byte de formato (0x16 = A8R8G8B8 linear, 0x08 ~ DXT1, 0x0A ~ DXT5,
                 deduzido pelos tamanhos), largura/altura u16 BE.
  *.xsg.ps3      cena/modelo SceneGraph (u32 LE count + bloco 'SG28PT00')
  *.xui          cena XUI em XML (<XuiCanvas>) - menus/HUD, editável em texto
  *.xus          tabela de strings XUI binária ('XUIS'), uma por idioma
  *.pfx          parâmetros de partículas em texto (sEmitter...)
  *.abc          fonte bitmap (glyph table) - usada com <nome>.texture.ps3
  Bman.bnk       banco de som SCREAM (Sony) - tocado via MultiStream (SPU)
"""
import json, os, sys, zipfile, collections

DESC = {
    "texture.ps3": "Textura SceneGraph (SG28PT00)",
    "xsg.ps3": "Modelo/cena SceneGraph",
    "xui": "Cena XUI (XML) - telas e HUD",
    "xus": "Strings XUI por idioma",
    "pfx": "Definição de partículas (texto)",
    "abc": "Tabela de glifos de fonte",
}

def kind(name):
    n = name.lower()
    for k in DESC:
        if n.endswith("." + k):
            return k
    return n.rsplit(".", 1)[-1]

def main():
    usrdir, out = sys.argv[1], sys.argv[2]
    extrair = sys.argv[sys.argv.index("--extrair") + 1] if "--extrair" in sys.argv else None
    z = zipfile.ZipFile(os.path.join(usrdir, "media.zip"))
    groups = collections.defaultdict(list)
    for i in z.infolist():
        if i.is_dir():
            continue
        k = kind(i.filename)
        e = dict(name=i.filename, size=i.file_size, packed=i.compress_size)
        if k == "texture.ps3":
            h = z.read(i.filename)[:64]
            nul = h.index(b"\0", 20)
            e["format"] = f"0x{h[nul + 1]:02X}"
        groups[k].append(e)
    cat = dict(
        media_zip=dict(entries=sum(len(v) for v in groups.values()), compression="deflate",
                       types={k: dict(description=DESC.get(k, "?"), count=len(v),
                                      bytes=sum(x["size"] for x in v), files=v) for k, v in groups.items()}),
        Bman_bnk=dict(size=os.path.getsize(os.path.join(usrdir, "Bman.bnk")),
                      description="Banco SCREAM (Sony): cabeçalho com blocos 'SBlk' (lido como 'klBS'); áudio via MultiStream/SPU"),
        guest_paths=["/app_home/media.zip", "/app_home/Bman.bnk", "/dev_hdd1/cache.dat", "/dev_hdd1/cache.idx",
                     "/dev_flash/sys/external/flashMP3.pic", "/dev_flash/sys/external/flashATRAC.pic"],
    )
    json.dump(cat, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    for k, v in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"{k:<14}{len(v):>5} arquivos  {sum(x['size'] for x in v) / 1e6:8.1f} MB  {DESC.get(k, '')}")
    if extrair:
        z.extractall(extrair)
        print("extraído em", extrair)

if __name__ == "__main__":
    main()
