# Firmware signing keys (public halves only)

`grok update` accepts an image only if its ECDSA P-256 signature block
(`espsecure.py sign_data --version 1`) verifies against the 64-byte raw
public key embedded at build time (`CONFIG_SECURE_BOOT_VERIFICATION_KEY` in
`sdkconfig.defaults`).

| File | What |
|---|---|
| `TEST_signature_verification_key.bin` | **TEST KEY.** Raw X‖Y. The private half exists only on the build box, outside git. Images signed with it are test images. Not for release. |
| `TEST_signature_verification_key.pub.pem` | Same TEST key as PEM, for `espsecure.py verify_signature`. |
| `signature_verification_key.bin` | Joe's release key (added when Joe sends his public PEM). |

Never commit a private key. `.gitignore` blocks `*.pem` except `*.pub.pem`.
Back up the release private key in a password manager: without it no future
wireless update can be signed, and every device has to be reflashed over USB
with a new public key.

Converting a pasted public PEM to the raw file:

    python tools/pubkey_pem_to_bin.py joe_pub.pem keys/signature_verification_key.bin
