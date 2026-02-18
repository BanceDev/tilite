<p align="center">
  <img width="256" height=auto src="logo.png">
  <br/>
  <img src="https://img.shields.io/github/contributors/bancedev/tilite" alt="contributors">
  <img src="https://img.shields.io/github/license/bancedev/tilite" alt="license">
  <img src="https://img.shields.io/github/forks/bancedev/tilite" alt="forks">
</p>

---

# Tilite

Tilite is a ultra-light minimal dynamic window manager with just under 2k SLOC. This project seeks to cover the exact minimum number of features I need to have the desktop experience I want. This window manager is not designed to be general purpose but if it fits your use case I hope you find it as useful as I do.

---

## Dependencies

- `libX11`
- `Xinerama`
- `XCursor`
- `CC`
- `Make`

## Compiling/Installation

```
make
sudo make install
```

Then just add tilite to your `.xinitrc` and you're good to go. You could also make a desktop entry for it if you want to use a display manager but this repo doesn't provide one.

## Configuration

All configuration of tilite is done at compile time in the config.h header. The config on this repo is my personal one but I'll probably add more generic ones in the future.

## Thanks & Inspiration

- dwm - the basis for suckless tiling
- sxwm - lots of source code basis
