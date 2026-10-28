"""AES mínimo para os scripts do port (PKG e EBOOT): CTR, CBC (decriptar) e
ECB (decriptar), com chaves de 128, 192 ou 256 bits.

Usa, na ordem, o primeiro que houver:
  1. 'cryptography' ou 'pycryptodome' (Python normal com pip);
  2. a BCrypt do Windows via ctypes (Python embutido do launcher: rápido e sem
     instalar nada);
  3. AES em Python puro (só a biblioteca padrão; lento, serve para testes).
BACKEND diz qual foi escolhido. AES_COMPAT_BACKEND=bcrypt|pure força um deles.
"""
import os
import sys

BACKEND = None
_force = os.environ.get("AES_COMPAT_BACKEND", "")

if not _force:
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

        def ctr(key, iv, data):
            d = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
            return d.update(data) + d.finalize()

        def cbc_dec(key, iv, data):
            return Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor().update(data)

        def ecb_dec(key, data):
            return Cipher(algorithms.AES(key), modes.ECB()).decryptor().update(data)

        BACKEND = "cryptography"
    except ImportError:
        try:
            from Crypto.Cipher import AES
            from Crypto.Util import Counter

            def ctr(key, iv, data):
                c = Counter.new(128, initial_value=int.from_bytes(iv, "big"))
                return AES.new(key, AES.MODE_CTR, counter=c).decrypt(data)

            def cbc_dec(key, iv, data):
                return AES.new(key, AES.MODE_CBC, iv).decrypt(data)

            def ecb_dec(key, data):
                return AES.new(key, AES.MODE_ECB).decrypt(data)

            BACKEND = "pycryptodome"
        except ImportError:
            pass


# ---------------------------------------------------------------- blocos ECB
# Os dois backends abaixo só sabem cifrar/decifrar blocos ECB; CTR e CBC são
# montados em cima disso.

def _bcrypt_backend():
    import ctypes
    from ctypes import wintypes
    bc = ctypes.WinDLL("bcrypt.dll")
    for fn in (bc.BCryptOpenAlgorithmProvider, bc.BCryptSetProperty, bc.BCryptGenerateSymmetricKey,
               bc.BCryptEncrypt, bc.BCryptDecrypt):
        fn.restype = ctypes.c_long
    alg = ctypes.c_void_p()
    if bc.BCryptOpenAlgorithmProvider(ctypes.byref(alg), ctypes.c_wchar_p("AES"), None, 0):
        raise OSError("BCryptOpenAlgorithmProvider(AES) falhou")
    mode = ctypes.create_unicode_buffer("ChainingModeECB")
    if bc.BCryptSetProperty(alg, ctypes.c_wchar_p("ChainingMode"), mode, ctypes.sizeof(mode), 0):
        raise OSError("BCryptSetProperty(ChainingModeECB) falhou")
    keys = {}

    def handle(key):
        h = keys.get(key)
        if h is None:
            h = ctypes.c_void_p()
            kb = ctypes.create_string_buffer(bytes(key), len(key))
            if bc.BCryptGenerateSymmetricKey(alg, ctypes.byref(h), None, 0, kb, len(key), 0):
                raise OSError("BCryptGenerateSymmetricKey falhou")
            keys[key] = h
        return h

    def run(fn, key, data):
        n = len(data)
        if not n:
            return b""
        src = ctypes.create_string_buffer(bytes(data), n)
        dst = ctypes.create_string_buffer(n)
        got = wintypes.ULONG()
        st = fn(handle(key), src, n, None, None, 0, dst, n, ctypes.byref(got), 0)
        if st or got.value != n:
            raise OSError(f"BCrypt AES falhou (0x{st & 0xFFFFFFFF:08X})")
        return dst.raw

    return (lambda k, d: run(bc.BCryptEncrypt, k, d)), (lambda k, d: run(bc.BCryptDecrypt, k, d))


def _pure_backend():
    sbox = [0] * 256
    p = q = 1
    while True:                      # S-box gerada (sem tabela de 256 números colada aqui)
        p = (p ^ (p << 1) ^ (0x1B if p & 0x80 else 0)) & 0xFF
        q ^= q << 1; q ^= q << 2; q ^= q << 4; q &= 0xFF
        if q & 0x80: q ^= 0x09
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    inv = [0] * 256
    for i, v in enumerate(sbox):
        inv[v] = i

    def xt(a):
        return ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1

    def mul(a, b):
        r = 0
        while b:
            if b & 1: r ^= a
            a = xt(a); b >>= 1
        return r

    def expand(key):                 # AES-128/192/256 (o SELF usa chaves de 256 bits)
        nk = len(key) // 4
        if len(key) not in (16, 24, 32):
            raise ValueError("chave AES inválida")
        nr = nk + 6
        w = [list(key[i:i + 4]) for i in range(0, len(key), 4)]
        rc = 1
        for i in range(nk, 4 * (nr + 1)):
            t = list(w[i - 1])
            if i % nk == 0:
                t = [sbox[b] for b in t[1:] + t[:1]]
                t[0] ^= rc; rc = xt(rc)
            elif nk > 6 and i % nk == 4:
                t = [sbox[b] for b in t]
            w.append([a ^ b for a, b in zip(w[i - nk], t)])
        return [sum(w[r * 4:r * 4 + 4], []) for r in range(nr + 1)]

    cache = {}

    def rk(key):
        if key not in cache:
            cache[key] = expand(key)
        return cache[key]

    def enc_block(k, s):
        nr = len(k) - 1
        s = [a ^ b for a, b in zip(s, k[0])]
        for r in range(1, nr + 1):
            s = [sbox[b] for b in s]
            s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]           # ShiftRows
            if r < nr:
                o = []
                for c in range(4):
                    a = s[4 * c:4 * c + 4]
                    o += [xt(a[0]) ^ xt(a[1]) ^ a[1] ^ a[2] ^ a[3], a[0] ^ xt(a[1]) ^ xt(a[2]) ^ a[2] ^ a[3],
                          a[0] ^ a[1] ^ xt(a[2]) ^ xt(a[3]) ^ a[3], xt(a[0]) ^ a[0] ^ a[1] ^ a[2] ^ xt(a[3])]
                s = o
            s = [a ^ b for a, b in zip(s, k[r])]
        return bytes(s)

    def dec_block(k, s):
        nr = len(k) - 1
        s = [a ^ b for a, b in zip(s, k[nr])]
        for r in range(nr - 1, -1, -1):
            s = [s[(i - 4 * (i % 4)) % 16] for i in range(16)]           # InvShiftRows
            s = [inv[b] for b in s]
            s = [a ^ b for a, b in zip(s, k[r])]
            if r > 0:
                o = []
                for c in range(4):
                    a = s[4 * c:4 * c + 4]
                    o += [mul(a[0], 14) ^ mul(a[1], 11) ^ mul(a[2], 13) ^ mul(a[3], 9),
                          mul(a[0], 9) ^ mul(a[1], 14) ^ mul(a[2], 11) ^ mul(a[3], 13),
                          mul(a[0], 13) ^ mul(a[1], 9) ^ mul(a[2], 14) ^ mul(a[3], 11),
                          mul(a[0], 11) ^ mul(a[1], 13) ^ mul(a[2], 9) ^ mul(a[3], 14)]
                s = o
        return bytes(s)

    def enc(key, data):
        k = rk(bytes(key))
        return b"".join(enc_block(k, data[i:i + 16]) for i in range(0, len(data), 16))

    def dec(key, data):
        k = rk(bytes(key))
        return b"".join(dec_block(k, data[i:i + 16]) for i in range(0, len(data), 16))

    return enc, dec


def _xor(a, b):
    n = len(a)
    return (int.from_bytes(a, "little") ^ int.from_bytes(b[:n], "little")).to_bytes(n, "little")


def _install_block_backend(name, enc, dec):
    global ctr, cbc_dec, ecb_dec, BACKEND
    BACKEND = name
    chunk_blocks = 1 << 16                                       # 1 MB por vez

    def _ctr(key, iv, data):
        c0 = int.from_bytes(iv, "big")
        mask = (1 << 128) - 1
        out = []
        for off in range(0, len(data), chunk_blocks * 16):
            part = data[off:off + chunk_blocks * 16]
            nb = (len(part) + 15) // 16
            base = c0 + off // 16
            ctrs = b"".join(((base + i) & mask).to_bytes(16, "big") for i in range(nb))
            out.append(_xor(part, enc(key, ctrs)))
        return b"".join(out)

    def _cbc_dec(key, iv, data):
        n = len(data) - len(data) % 16
        plain = dec(key, data[:n])
        return _xor(plain, bytes(iv) + data[:n - 16]) if n else b""

    def _ecb_dec(key, data):
        return dec(key, data[:len(data) - len(data) % 16])

    ctr, cbc_dec, ecb_dec = _ctr, _cbc_dec, _ecb_dec


if BACKEND is None:
    if _force != "pure" and sys.platform == "win32":
        try:
            _e, _d = _bcrypt_backend()
            # vetor do FIPS-197: se a BCrypt não der o resultado certo, Python puro.
            _k, _p = bytes(range(16)), bytes.fromhex("00112233445566778899aabbccddeeff")
            _c = _e(_k, _p)
            if _c == bytes.fromhex("69c4e0d86a7b0430d8cdb78070b4c55a") and _d(_k, _c) == _p:
                _install_block_backend("bcrypt", _e, _d)
        except (OSError, AttributeError, ValueError):
            pass
    if BACKEND is None:
        _install_block_backend("pure", *_pure_backend())
