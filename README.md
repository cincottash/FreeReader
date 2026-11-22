# FreeReader

An e-book reader for DRM-free EPUB books, for Windows (tested) and Linux (experimental). It is being built to
make reading in another language easy: translations of selected words and passages are coming in a later
version.

![FreeReader showing Don Quijote with the contents panel open](.github/screenshot.png)

**Status:** early development (phase 1 of 7). Reading works: books open, pages lay out and turn, and your place
is remembered. The library, themes, translation, highlights and the word log come next.

## Download

There is no release yet. Each release will have:

| System | File | Status |
| --- | --- | --- |
| Windows 10 and 11 | `FreeReader.exe` (one file, nothing to install) | Tested |
| Linux | `FreeReader-x86_64.AppImage` | Experimental |

## Reading

- **Open a book:** click **Open Book…** or drop an EPUB file on the window. Recent books are listed on the start
  screen; FreeReader reopens each one where you left off.
- **Turn pages:** arrow keys, Page Up and Page Down, Space, the mouse wheel, or the arrows that appear at the
  edges of the page.
- **Contents:** the list button in the header (Ctrl+T). The current chapter is marked.
- **Go to a page or point:** Ctrl+G, then a page number or a percentage such as `45%`. Click the progress bar
  to jump there.
- **Back:** after any jump, **Back to Page …** at the bottom (or Alt+Left) returns to where you were.
- **Footnotes** open in a small pop-up next to the reference. **Pictures** open full size when clicked.
- **Commands:** Ctrl+K lists everything FreeReader can do. There is no menu bar.

Page numbers stay the same when you change the text size or the window: one page is 1,800 characters of the
book, or the publisher's own page numbers when the book has them.

FreeReader only opens books without DRM. It never removes or works around DRM.

<details>
<summary>Keyboard shortcuts</summary>

| Command | Shortcut |
| --- | --- |
| Show Commands | Ctrl+K, Ctrl+Shift+P, F1 |
| Open Book… | Ctrl+O |
| Go to Start Screen | Ctrl+L |
| Show or Hide Contents | Ctrl+T |
| Go to Page or Percent… | Ctrl+G |
| Back | Alt+Left, Backspace |
| Next Chapter / Previous Chapter | Ctrl+Page Down / Ctrl+Page Up |
| Go to Beginning of Book | Ctrl+Home |
| Start / End of Chapter | Home / End |
| Larger Text / Smaller Text / Default Text Size | Ctrl+= / Ctrl+- / Ctrl+0 |
| Full Screen | F11 |
| Copy | Ctrl+C |
| Quit | Ctrl+Q |

One Column, Two Columns and Automatic Columns are in the command list (Ctrl+K).

</details>

<details>
<summary>Linux (experimental)</summary>

The AppImage is built and started automatically on every change, but it hasn't been used on a real Linux
desktop yet. It runs through X11; on a Wayland desktop it uses XWayland, which most desktops turn on by default.

</details>

<details>
<summary>Command line</summary>

```
FreeReader [book.epub] [--data-folder FOLDER]
FreeReader book.epub --screenshot out.png [--page N | --percent P] [--turn N] [--size WxH]
           [--columns one|two|auto] [--text-size PERCENT] [--contents] [--palette]
```

`--data-folder` uses that folder for this run only. `--screenshot` draws the window off-screen to a picture and
exits; it never shows a window or a dialog.

</details>

<details>
<summary>Building from source</summary>

C++20, Qt 6 (Widgets, Svg, Sql, Concurrent) and zlib, built with CMake.

- **Windows:** MSYS2 UCRT64 with `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-svg}`. For the single-file
  exe, add `qt6-static` and configure with `-DSTATIC_BUILD=ON -DCMAKE_PREFIX_PATH=<msys2>/ucrt64/qt6-static`.
- **Linux:** Qt 6.5 or newer, zlib, CMake and Ninja.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

</details>

<details>
<summary>How it works</summary>

- `src/core/format`: format plug-ins turn a file into a book (metadata, contents, chapters, resources). EPUB 2
  and 3 first.
- `src/core/html`, `src/core/css`: a forgiving HTML parser and the CSS subset books use, with the cascade.
- `src/core/layout`: FreeReader's own layout engine. Each chapter becomes a flow of paragraphs, images and
  tables with stable character positions, laid out into pages with Qt's text layout on background threads and
  drawn with QPainter. No browser engine is used.
- `src/core/position`: positions as EPUB CFI, which keep working when the text size or window changes.
- `src/core/services`: the book engine (opening, layout cache) and the library database (SQLite).
- `src/app`: the windows.

</details>

## License

Copyright (C) 2026 cincottash. Licensed under the [GNU General Public License v3.0](LICENSE).
