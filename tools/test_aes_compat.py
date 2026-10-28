#!/usr/bin/env python3
"""Confere o AES em Python puro do aes_compat (o mesmo código de CTR/CBC usado
com a BCrypt do Windows) contra o pacote 'cryptography'. Usado pelo CI."""
import os
import sys

os.environ["AES_COMPAT_BACKEND"] = "pure"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aes_compat as P  # noqa: E402
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  # noqa: E402

assert P.BACKEND == "pure", P.BACKEND
for t in range(42):
    k = os.urandom((16, 24, 32)[t % 3])
    iv = b"\xff" * 16 if t == 0 else os.urandom(16)        # t == 0: o contador dá a volta
    n = [0, 1, 15, 16, 17, 100, 1000][t % 7] + (t // 7) * 16
    d = os.urandom(n)
    assert P.ctr(k, iv, d) == Cipher(algorithms.AES(k), modes.CTR(iv)).decryptor().update(d), ("ctr", t)
    m = n - n % 16
    assert P.cbc_dec(k, iv, d[:m]) == Cipher(algorithms.AES(k), modes.CBC(iv)).decryptor().update(d[:m]), ("cbc", t)
    assert P.ecb_dec(k, d[:m]) == Cipher(algorithms.AES(k), modes.ECB()).decryptor().update(d[:m]), ("ecb", t)
print("aes_compat (puro) confere com cryptography")
