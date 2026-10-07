# Amiga amiga_gfx BOB fixture

Owner-run. Compares `bob_create(img, 1)` (save/restore background) with
`bob_create(img, 0)` (paint-only trail).

```text
make -f Makefile.amiga ext-amiga_gfx
pythonami tests/integration/amiga/test_amiga_gfx_bob.py
```

Host: `build/host/pythonami --check tests/integration/amiga/test_amiga_gfx_bob.py`
