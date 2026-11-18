# Kalamus

A Markdown editor for KDE Plasma that formats your text as you write, in the spirit of
Typora, but free software (GPL-3.0-or-later) and built natively with Qt 6 and KDE Frameworks 6.

Named after the *calamus*, the reed pen scribes wrote with.

## Principles

- **Your file is the truth.** The editor shows the Markdown source styled like the
  finished page. It never rewrites your Markdown behind your back.
- **Syntax gets out of the way.** Markers such as `#`, `**` and `[](url)` are hidden
  except on the line you are editing.
- **KDE first.** Breeze colours, Plasma fonts, KXmlGui menus/toolbars/shortcuts,
  single-click setting, standard dialogs.

## Build

```bash
scripts/install-deps.sh          # once, needs sudo
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/kalamus [file-or-folder]
```

## Shortcuts

| Keys | Does |
|---|---|
| Ctrl+B / Ctrl+I | Bold / italic (wraps the selection, or toggles it off) |
| Alt+Shift+5 | Strikethrough |
| Ctrl+Shift+\` | Inline code |
| Ctrl+K | Link |
| Ctrl+1 … Ctrl+6, Ctrl+0 | Heading level, paragraph |
| Enter in a list or quote | Continues it; Enter on an empty item ends it |
| Tab / Shift+Tab in a list | Indent / outdent the item |
| Shift+Enter | Plain new line |
| Ctrl+Shift+O | Open folder in the Files panel |
| F9 | Show/hide the Files panel |
| Ctrl+E | Export to PDF |

## Roadmap

### v1
- [x] Live-formatted editing (headings, emphasis, code, links, lists, tasks, quotes)
- [x] Files panel (folder tree, Markdown files only)
- [x] Export: HTML, PDF (built in); Word, ODT (via pandoc)
- [ ] Tables rendered as a grid when the cursor is outside, with a table editor
- [ ] Math rendered inline (`$…$`, `$$…$$`), likely via MicroTeX
- [ ] Images shown inline
- [ ] Code block syntax highlighting (KSyntaxHighlighting) with full-width background
- [ ] Spell checking (Sonnet)
- [ ] Outline panel (headings), find/replace, recent files
- [ ] Paste images from the clipboard into an `assets/` folder
- [ ] Settings: font, line width, theme

### Later
- Focus / typewriter mode, word count
- Mermaid diagrams, KIO remote files, Flatpak
