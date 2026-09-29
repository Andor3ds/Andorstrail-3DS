# Andor's Trail for Nintendo 3DS

This repository is the foundation for a native Nintendo 3DS port. It uses
devkitARM and libctru; no NDS libraries or NDS project files are used.

## Build

With devkitPro installed and `DEVKITPRO` and `DEVKITARM` set, run:

```sh
make
```

The build produces `andorstrail-3ds.3dsx`. `make clean` removes generated
build files.

## Structure

- `source/core` is platform-independent and contains the game-core lifecycle.
- `source/platform/3ds` contains the libctru application entry point, rendering,
  and input polling.

The core is intentionally inert: no gameplay, controls, data, or Android
behavior have been ported yet. The NDS reference projects suggest useful
structural boundaries for future work, including player/world runtime state,
map-place data, resource definitions, and (in the actor-spawn WIP) spawn-area
data. Their implementations and platform-specific code are not included here.
The original Android source and data remain the authority for future behavior.