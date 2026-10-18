#!/usr/bin/env python3
"""Convert an ECDSA P-256 public key PEM to the raw 64-byte X||Y file that
CONFIG_SECURE_BOOT_VERIFICATION_KEY expects. Refuses private keys.

    python tools/pubkey_pem_to_bin.py pub.pem keys/signature_verification_key.bin
"""
import hashlib
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    data = open(sys.argv[1], "rb").read()
    if b"PRIVATE" in data:
        print("refusing: that is a PRIVATE key. Send only the public key.")
        return 1
    key = serialization.load_pem_public_key(data)
    if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
        print("refusing: need an ECDSA NIST P-256 public key")
        return 1
    nums = key.public_numbers()
    raw = nums.x.to_bytes(32, "big") + nums.y.to_bytes(32, "big")
    open(sys.argv[2], "wb").write(raw)
    print("wrote", sys.argv[2], "key id", hashlib.sha256(raw).hexdigest()[:16])
    return 0


if __name__ == "__main__":
    sys.exit(main())
