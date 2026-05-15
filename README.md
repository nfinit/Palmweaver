Palmweaver: A Text Editor for Windows CE
----------------------------------------

Palmweaver is a generalist text editor for Windows CE Handheld PCs and an
experiment in the ability of frontier LLMs to develop for niche and unusual
platforms. The primary goal of this project is to fill a gap in CE's software
base, which up to this point has seemed to lack a decent editor for ordinary
plaintext between Notepad-likes and rich text document editors like Pocket Word
and TextMaker.

Palmweaver has a number of useful features for various use cases beyond that
of common CE Notepad clones, including:
- Unicode (UTF-16, UTF-8) and ANSI text editing support including heuristics for BOM-less encoding
- Fixed/proportional font toggle and size switching (currently fixed to Courier/Tahoma)
- Togglable line number indicator
- User-definable column width limits with toggleable visual indicator, auto-wrapping and paragraph reflowing
- Hotkeys for quickly inserting dates, timestamps and horizontal rules
- Large (>64K) file support
- LF/CRLF support that preserves original file newlines on save
- Inverse and color themes (with some support for grayscale devices)
- Full-screen display support with all elements (toolbar, status bar, scroll bars) toggleable
- Keyboard-friendly interface with comprehensive hotkeys and custom keyboard-driven file picker
- Quick Note mode to quickly create/switch to dated daily note files at storage card-aware configurable locations
- Custom multi-level undo/redo implementation for most common operations (typing, cut, paste, replace, insert) 

## Keyboard shortcuts

Palmweaver caters to keyboard-centric workflows with a goal of ensuring you
have to pull out a stylus as infrequently as possible. Below is a general list
of shortcuts currently supported in addition to standard text control options:

### Conveniences

| Shortcut   | Function                       |
|------------|--------------------------------|
| `Ctrl+Q`   | Quick Note                     |
| `Ctrl+R`   | Insert horizontal rule (`---`) |
| `Ctrl+;`   | Insert date                    |
| `Ctrl+'`   | Insert time                    |
| `Ctrl+:`   | Insert date and time           |

### Navigation 

| Shortcut   | Function                      |
|------------|-------------------------------|
| `Ctrl+F`   | Find                          |
| `Ctrl+3`   | Find Next (F3 also supported) |
| `Ctrl+G`   | Go To Line                    |

### Editing 

| Shortcut   | Function                      |
|------------|-------------------------------|
| `Ctrl+H`   | Replace                       |
| `Ctrl+Z`   | Undo                          |
| `Ctrl+Y`   | Redo                          |
| `Ctrl+J`   | Reflow paragraph at cursor    |

### Window and display operations 

| Shortcut    | Function                      |
|-------------|-------------------------------|
| `Alt+W`     | Toggle word wrap              |
| `Alt+L`     | Toggle line number indicator  |
| `Alt+B`     | Toggle status bar             |
| `Alt+S`     | Toggle scroll bars            |
| `Alt+Enter` | Toggle full screen            |
| `Alt+I`     | Inverse colors (any theme)    |
| `Ctrl+W`    | Exit Palmweaver               |

### File operations

| Shortcut   | Function                      |
|------------|-------------------------------|
| `Ctrl+N`   | New file                      |
| `Ctrl+O`   | Open file                     |
| `Ctrl+S`   | Save current file             |

## Installation and support

Palmweaver is a standalone executable that can be extracted from the release ZIP
and dropped anywhere on your Handheld PC. Official releases currently only
target SH3 and MIPS CE 2.0 devices, but Palmweaver will happily run on 2.11 and
3.0 as well, and also Palm-size PCs and Pocket PCs, though it's not yet
explicitly optimized for their narrow displays and thus you may see menu
clipping and unsatisfactory behavior in some features such as horizontal rule
insertion. Future releases of Palmweaver will also target CE 2.11 and thus
support the full range of architectures, including ARM, PowerPC, SH4 and x86.

## Development notes and caveats

Palmweaver is developed and tested step by step according to human-written
plans and specifications, but it's important to keep in mind that it is still 
very much an exercise in "vibe coding" in an environment unfamiliar to even the 
latest frontier models, with many strange patterns, even some hacks, and the
occasional bizarre edge case bug that comes with them. Palmweaver is continuously
tested and enhanced through real-world usage and has been judged stable and 
safe for my workflows, but this does not cover every case.

### CE limitations
Palmweaver not only aims to resolve a longstanding gap in CE's overall software
base, but also that in some great quality devices running the earlier CE 2.0
operating system like the HP 620/660LX and Cassiopeia A-10/A-11. This comes
with some tradeoffs, especially in the realm of support for things such as
syntax highlighting and theming, which are generally much more difficult to do
with the available controls. We may yet find ways to further enhance Palmweaver
in these and other areas, but it will likely take some time... and some bugs.

### Large file mode
Palmweaver's large file mode is easily one of its largest and most complex
features, working by splitting a file into 64K "pages" and then stitching them
together invisibly as a file is navigated. This requires a lot of "moving parts"
and thus presents a larger surface to potential edge case bugs, especially at
page boundaries. Large file mode is mostly being built and tested for read-only
use cases such as reading plaintext e-books and documentation, and is currently
generally stable for this kind of workflow. Editing large files is mostly
workable, but still slower than it should be and also may have some unexpected
behaviors, especially in undo/redo flows. It's expected that large file mode
will see a considerably larger amount of refactoring, optimization and further
testing over future releases. 
