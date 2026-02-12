# CE Games

A collection of games for the **TI-84 Plus CE** graphing calculator.

## Toolchain

- **CE C/C++ Toolchain v12+** — cross-compiler and libraries for TI-84 Plus CE
  - Install location: `~/CEdev`
  - Env vars required: `CEDEV="$HOME/CEdev"`, `PATH="$CEDEV/bin:$PATH"`
  - Each game Makefile includes the toolchain via `include $(shell cedev-config --makefile)`
  - Docs: https://ce-programming.github.io/toolchain/

- **Target hardware**: TI-84 Plus CE (eZ80 CPU, 320x240 screen, 8-bit palette-indexed graphics)
- **Output format**: `.8xp` files (calculator program binaries)

## Build

```sh
make          # build all games
make pong     # build a single game
make clean    # clean all build artifacts
```

Each game has its own `Makefile` with `NAME`, `ICON`, `DESCRIPTION`, `CFLAGS` etc. The top-level `Makefile` delegates to each game directory.

## Testing / Emulation

Using the custom emulator at https://github.com/hunterchen7/ti84ce:

```sh
# Send a single program
cargo run --release --example debug -- sendfile ../games/pong/bin/PONG.8xp

# Bake all games + libs into a ROM
cargo run --release --example debug -- bakerom games.rom ../games/*/bin/*.8xp ../games/libs/*.8xv
```

## Project Structure

```
ce-games/
  CLAUDE.md
  Makefile          # Top-level build (iterates GAMES list)
  README.md
  shared/           # Common utilities (currently empty, for future use)
  libs/             # CE C library .8xv files (graphx, keypadc, fontlibc, fileioc, libload)
  <game>/
    Makefile         # Game-specific build config (NAME, ICON, CFLAGS, includes toolchain)
    icon.png         # 16x16 program icon
    src/main.c       # Game source (single-file C)
    bin/             # Build output (.8xp)
    obj/             # Build intermediates
```

## Adding a New Game

1. Create `<game>/` directory with `Makefile`, `icon.png`, and `src/main.c`
2. Game Makefile template:
   ```makefile
   NAME = GAMENAME
   ICON = icon.png
   DESCRIPTION = "Description"
   COMPRESSED = NO
   CFLAGS = -Wall -Wextra -Oz
   CXXFLAGS = -Wall -Wextra -Oz
   include $(shell cedev-config --makefile)
   ```
3. Add the game directory name to the `GAMES` list in the top-level `Makefile`

## CE C Programming Notes

- **Screen**: 320x240, 8-bit palette-indexed color (1555 RGB format via `gfx_RGBTo1555`)
- **Graphics library**: `graphx.h` — double-buffered rendering with `gfx_SetDrawBuffer()` / `gfx_SwapDraw()`
- **Input**: `keypadc.h` — call `kb_Scan()` once per frame, read `kb_Data[group]` for key states
  - Group 1: `kb_2nd`, `kb_Mode`, etc.
  - Group 6: `kb_Enter`, `kb_Clear`, etc.
  - Group 7: `kb_Up`, `kb_Down`, `kb_Left`, `kb_Right`
  - Edge detection pattern: `new_keys = cur_keys & ~prev_keys`
- **Timing**: `clock()` from `time.h`, `CLOCKS_PER_SEC` for frame timing
- **Compiler flags**: `-Wall -Wextra -Oz` (optimize for size — calculator has limited memory)
- **Program names**: Must be uppercase, max 8 characters (TI-OS limitation)
- **All games are single-file C** — keep source in `src/main.c`
