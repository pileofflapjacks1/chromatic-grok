# Loader frames

Original 160×144 loading art by Carty G (not an official logo).

- `loader_0.png` … `loader_3.png` — source frames
- Timings (from `loader.gif`): 300, 300, 300, 150 ms
- Packed into firmware by `tools/pack_loader_frames.py` as a shared 5-color
  palette plus RLE (`grok_loader_frames.c`). Regenerate after editing PNGs:

```bash
python3 tools/pack_loader_frames.py
```
