# FreeReader

An e-book reader for DRM-free EPUB books, for Windows (tested) and Linux (experimental). It is being built to
make reading in another language easy: translations of selected words and passages are coming in a later
version.

![FreeReader showing Don Quijote with the contents panel open](.github/screenshot.png)

**Status:** early development (phase 2 of 7). The library, reading, themes and text settings work. Translation,
highlights and the word log come next.

## Download

Get the latest version from the [Releases](https://github.com/cincottash/FreeReader/releases) page.

| System | File | Status |
| --- | --- | --- |
| Windows 10 and 11 | `FreeReader.exe` (one file, nothing to install) | Tested |
| Linux | `FreeReader-x86_64.AppImage` | Experimental |

## Library

<img src=".github/readme/library.png" align="right" width="420" alt="The library with two books">

- **Add Books** takes files or a whole folder, or drop them on the window. The files stay where they are.
- Filter by **Reading**, **Finished**, **Not Started**, by **language**, or by your **collections**: make one with
  **+ New Collection**, then drag books onto it.
- Search by title or author, sort, and switch between covers and a list.
- **Continue Reading** at the bottom reopens the last book where you left off.
- A book whose file was moved shows **File Not Found**; right-click it to **Locate File…**.

<br clear="right">

## Reading

- **Turn pages:** arrow keys, Page Up and Page Down, Space, the mouse wheel, or the arrows at the edges of the page.
- **Contents:** the list button in the header (Ctrl+T). The current chapter is marked.
- **Go to a page or point:** Ctrl+G, then a page number or a percentage such as `45%`. Click the progress bar
  to jump there. **Back to Page …** (or Alt+Left) returns to where you were.
- **Footnotes** open in a small pop-up next to the reference. **Pictures** open full size when clicked.
- **Commands:** Ctrl+K lists everything FreeReader can do. There is no menu bar.

Page numbers stay the same whatever the text size or the window: one page is 1,800 characters of the book, or
the publisher's own page numbers when the book has them.

## Text and layout

![The text and layout panel](.github/readme/text-layout.png)

The **Aa** button sets the font (the book's own, the bundled Literata or Atkinson Hyperlegible, or any font on
your computer), text size, line spacing, margins, page width, one or two columns, justification and
hyphenation. **Apply To** decides which books a change is for: all books, all books in that language, or only
this one.

## Themes

![The theme list](.github/readme/themes.png)

43 themes restyle the whole app: FreeReader's reading themes (Midnight, Paper, Sepia, Slate and more) and
popular dark and light editor schemes. Point at a theme to preview it; your last few themes sit as dots in the
header. **Match Operating System** follows Windows' light or dark mode.

FreeReader only opens books without DRM. It never removes or works around DRM.

<details>
<summary>Keyboard shortcuts</summary>

Every shortcut can be changed in **Settings** (Ctrl+,).

| Command | Shortcut |
| --- | --- |
| Show Commands | Ctrl+K, Ctrl+Shift+P, F1 |
| Open Book… | Ctrl+O |
| Add Books… | Ctrl+Shift+O |
| Go to Library | Ctrl+L |
| Show or Hide Contents | Ctrl+T |
| Text and Layout… | Ctrl+Shift+A |
| Choose Theme… | Ctrl+Shift+T |
| Go to Page or Percent… | Ctrl+G |
| Back | Alt+Left, Backspace |
| Next Chapter / Previous Chapter | Ctrl+Page Down / Ctrl+Page Up |
| Go to Beginning of Book | Ctrl+Home |
| Start / End of Chapter | Home / End |
| Larger Text / Smaller Text / Default Text Size | Ctrl+= / Ctrl+- / Ctrl+0 |
| Full Screen | F11 |
| Copy | Ctrl+C |
| Settings… | Ctrl+, |
| Quit | Ctrl+Q |

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
FreeReader [book.epub] --screenshot out.png [--page N | --percent P] [--turn N] [--size WxH]
           [--columns one|two|auto] [--text-size PERCENT] [--font FAMILY] [--theme KEY]
           [--contents] [--palette] [--appearance] [--theme-list] [--add-books FOLDER] [--list-view]
```

`--data-folder` uses that folder for this run only. `--screenshot` draws the window off-screen to a picture and
exits; it never shows a window or a dialog, and it uses a throwaway library.

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
  tables with stable character positions, laid out into pages on background threads (with hyphenation by
  Liang's algorithm) and drawn with QPainter. No browser engine is used.
- `src/core/position`: positions as EPUB CFI, which keep working when the text size or window changes.
- `src/core/services`: the book engine (opening, layout cache), the library database (SQLite) and adding books.
- `src/app`: the windows.

</details>

<details>
<summary>Credits</summary>

- Fonts: [Literata](https://github.com/googlefonts/literata) (The Literata Project Authors) and
  [Atkinson Hyperlegible](https://www.brailleinstitute.org/freefont/) (Braille Institute of America), both under
  the SIL Open Font License (`resources/fonts`).
- Hyphenation patterns from the [hyph-utf8](https://github.com/hyphenation/tex-hyphen) project for Spanish,
  English, French, German, Italian and Portuguese, each under its own free license (`resources/hyphenation`).
- The dark and light themes are adjusted from popular editor color schemes (Nord, Dracula, Solarized, Gruvbox,
  Catppuccin, Rosé Pine, Tokyo Night, Everforest, Kanagawa, Ayu, One, Palenight, Night Owl, Cobalt2,
  Synthwave '84, Tomorrow, Monokai, Zenburn) and keep their authors' names.

</details>

## License

Copyright (C) 2026 cincottash. Licensed under the [GNU General Public License v3.0](LICENSE).
