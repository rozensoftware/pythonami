# ASL drawer requester (owner Amiga check)

Build: `make amiga-ext` → `ext/asl/asl.py68k`.

On Amiga (same drawer as `pythonami`):

```text
pythonami -c "asl=load_library('PROGDIR:ext/asl/asl.py68k'); print(asl.ask_drawer('Test','SYS:'))"
```

Cancel returns empty string. Used by `examples/installer/installer.py`.
