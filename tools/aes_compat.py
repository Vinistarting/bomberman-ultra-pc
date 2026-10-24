"""AES mínimo para os scripts do port: usa 'cryptography' ou 'pycryptodome'
(o que estiver instalado -- o ps3recomp já pede pycryptodome)."""
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

    def ctr(key, iv, data):
        d = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
        return d.update(data) + d.finalize()

    def cbc_dec(key, iv, data):
        return Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor().update(data)

    def ecb_dec(key, data):
        return Cipher(algorithms.AES(key), modes.ECB()).decryptor().update(data)
except ImportError:
    from Crypto.Cipher import AES
    from Crypto.Util import Counter

    def ctr(key, iv, data):
        c = Counter.new(128, initial_value=int.from_bytes(iv, "big"))
        return AES.new(key, AES.MODE_CTR, counter=c).decrypt(data)

    def cbc_dec(key, iv, data):
        return AES.new(key, AES.MODE_CBC, iv).decrypt(data)

    def ecb_dec(key, data):
        return AES.new(key, AES.MODE_ECB).decrypt(data)
