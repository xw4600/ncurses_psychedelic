# ncurses_psychedelic

A psychedelic ANSI "video" for the terminal, written in C with ncurses.

Renders three full-screen animated effects — sine **plasma**, polar **tunnel**,
and **kaleidoscope** — as a field of random letters and symbols, colored with a
hue-cycling 64-entry palette. Falls back to 16/8 colors on simpler terminals.

## Build

```sh
make
```

Requires `libncurses-dev` and a C compiler.

## Run

```sh
./ncurses_psychedelic
```

## Keys

| Key | Action |
|-----|--------|
| `q` / `Esc` | quit |
| `m` | switch mode (also auto-rotates) |
| `space` | pause / resume |
| `+` / `-` | speed up / slow down |
| `h` | toggle HUD |
