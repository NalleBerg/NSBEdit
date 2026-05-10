# NSBEdit

A lightweight, standalone RTF notepad for Windows.

## Download

Just grab **[NSBEdit.exe](NSBEdit.exe)** — no installer, no extra files, no dependencies. Drop it anywhere and run it.

## Features

- Full RTF formatting toolbar: bold, italic, underline, strikethrough, superscript, subscript
- Font face, size, colour and highlight
- Paragraph alignment (left / centre / right / justify)
- Bullet and numbered lists
- Image insertion
- File menu: New, Open, Save, Save As, Print
- Status bar showing line/column and file path
- DPI-aware (PerMonitorV2), statically linked — no external DLLs beyond Windows system ones

## Building from source

Requirements: MinGW-w64 (GCC 13+) with `g++` and `windres` on PATH.

```
.\makeit.bat
```

## License

GNU General Public License v2 — see [GPLv2.md](GPLv2.md).
