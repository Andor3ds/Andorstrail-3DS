# Andor's Trail for Nintendo 3DS

This repository is a native Nintendo 3DS port using devkitARM and libctru. It
does not use libnds or NDS display, input, or NitroFS APIs.

## Build

With devkitPro installed and `DEVKITPRO` and `DEVKITARM` set, run:

```sh
make
```

The build produces `andorstrail-3ds.3dsx` and bundles the original map/resource
data and generated native textures into 3DS ROMFS. `make clean` removes
generated build files and textures. Run
`make test` for the host-side parser, resource-lookup, and pack-builder tests;
host tests require GCC/G++, Python 3, TinyXML-2, and zlib development headers.

## Structure

- `source/core` contains the platform-independent JSON/resource and TMX/map
  parsers, GID lookup, collision, movement, and camera-coordinate logic.
- `source/platform/3ds` contains the libctru app and Citro2D map renderer.
- `tests` contains host tests for JSON parsing, original resource lookups, and
  map decoding/collision/coordinates, and asset bundling.
- `tools/build_game_data.py` rebuilds the compressed pack and index from the
    Android project's `res/raw` JSON files.
- `tools/build_map_data.py` bundles Android's configured maps and the original
  Home map/player PNG assets.

The resource layer follows Android's `loadresources.xml` lists, preserves those
JSON documents, and exposes records through a generated `(resource type, ID)`
index. Debug-only and explicitly unused raw files are excluded. It does not
interpret effects, requirements, actor definitions, or other gameplay rules.
The bundled index identifies Android source commit
`0dfa1f63ea6d944df2c6d34e641000c09d3e917e`.
To regenerate from an Android source checkout, run:

```sh
python3 tools/build_game_data.py --source /path/to/AndorsTrail
```

The source checkout used to verify this baseline is maintained separately from
this project. The pack and index are generated from that Android source tree;
the NDS resource pack and actor-spawn WIP pack were secondary references only.

The app starts on the original Android Home TMX map. Use the D-pad to move and
START to exit. Its source GIDs resolve to
the original tileset PNGs; the top-screen camera follows the player, and D-pad
movement uses the Android `Walkable` layer (`0` is walkable, nonzero is blocked).
The visible draw order follows Android: base, ground, objects, player, above,
top. Rendering is in the native 3DS layer and remains separate from map/movement
logic. Conditional replacement layers are not activated until their gameplay
requirements are ported. Combat, actors, dialogue, inventory, quests, and saves
remain deferred. No NDS renderer/backgrounds or libnds APIs are used.