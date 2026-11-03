# Vendored: esptool-js 0.5.7

- Source: npm `esptool-js@0.5.7` (https://github.com/espressif/esptool-js), file `bundle.js`, unmodified.
- npm integrity: `sha512-k3pkXU9OTySCd58OUDjuJWNnFjM+QpPWAghxyWPm3zNfaLiP4ex2jNd7Rj0jWPu3/fgvwau236tetsTZrh4x5g==`
- `bundle.js` SHA-256: `a2bc5bcbb3811e48781de165f2267f9c9c51e63fac7e8866765745e8a197cec0`
- License: Apache-2.0, Copyright 2019 Espressif Systems (Shanghai) CO LTD. Full text in `LICENSE` next to this file.
- The upstream package ships no NOTICE file. This file records provenance only; nothing upstream was removed.

`bundle.js` also contains code from its bundled dependencies, whose license headers are kept inside the file:

- pako 2.1.0 (MIT AND Zlib), https://github.com/nodeca/pako
- atob-lite (MIT) and tslib (0BSD), as listed in the upstream package.json

Our changes to esptool-js behavior live outside this folder, in `../../src/serial.js` (a `Transport` subclass).
