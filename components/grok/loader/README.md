# Loader frames

Original 160×144 loading art by Carty G (not an official logo). White circle
character with slanted eyes plus three loading dots on a near-black background.

- `loader_0.png` … `loader_9.png` — Carty G v2 source frames (blink, glance, bob)
- Timings (from Carty’s `loader.gif`): 300, 200, 200, 150, 120, 200, 300, 300, 120, 300 ms
- Shared 3-color palette (`#141414`, `#464646`, `#f5f5f5`) packed into firmware by
  `tools/pack_loader_frames.py` as palette + RLE (`grok_loader_frames.c`).
  Regenerate after editing PNGs:

```bash
python3 tools/pack_loader_frames.py
```

Earlier 4-frame drafts and the 64×64 1-bit pair live outside this repo under
`/workspace/grokbot-loader/` (not shipped in flash). Only the v2 ten-frame set
above is embedded.
