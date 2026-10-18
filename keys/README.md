# Firmware signing keys (public halves only)

`grok update` accepts an image only if its ECDSA P-256 signature block
(`espsecure.py sign_data --version 1`) verifies against the 64-byte raw
public key embedded at build time (`CONFIG_SECURE_BOOT_VERIFICATION_KEY` in
`sdkconfig.defaults`). The firmware trusts exactly one key.

| File | What |
|---|---|
| `joe_signing.pub.pem` | Joe's release public key (ECDSA P-256). SHA-256 of its DER form: `13b86339c36256dab861eb69ab39ef3e6f0ba973162c5154d20f170fc9587f80`. |
| `joe_signature_verification_key.bin` | The same key as raw X‖Y (64 bytes), the file the build embeds. Made with `tools/pubkey_pem_to_bin.py`. |

`grok update status` prints the first 16 hex digits of that DER hash
(`13b86339c36256da`) and "(Joe release key)".

The private key (`chromatic_signing_key.pem`) exists only on Joe's PC. It is
never on the build machine, in git, or in chat. Back it up in a password
manager: without it no future wireless update can be signed, and every
device then needs a USB flash of a build with a new public key.
`.gitignore` blocks `*.pem` except `*.pub.pem`.

Changing the key:

    python tools/pubkey_pem_to_bin.py new.pub.pem keys/<name>_signature_verification_key.bin

then point `CONFIG_SECURE_BOOT_VERIFICATION_KEY` at it, rebuild, and flash
that build over USB once (a device only trusts the key in the firmware it runs).
