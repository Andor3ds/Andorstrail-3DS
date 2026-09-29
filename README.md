# Andor's Trail for Nintendo 3DS

This repository is a native Nintendo 3DS port using devkitARM and libctru. It
does not use libnds or NDS display, input, or NitroFS APIs.

## Build

With devkitPro installed and `DEVKITPRO` and `DEVKITARM` set, run:

```sh
make
```

The build produces `andorstrail-3ds.3dsx` and bundles `romfs/game_data.pack`
into native 3DS ROMFS. `make clean` removes generated build files. Run
`make test` for the host-side parser, resource-lookup, and pack-builder tests;
host tests require GCC, Python 3, and zlib development headers.

## Structure

- `source/core` contains the platform-independent JSON parser and indexed
  resource reader, as well as the existing core lifecycle scaffold.
- `source/platform/3ds` contains the libctru application entry point and the
  ROMFS mount/resource-pack path.
- `tests` contains host tests for JSON parsing, original resource lookups, and
  data-pack generation.
- `tools/build_game_data.py` rebuilds the compressed pack and index from the
    Android project's `res/raw` JSON files.

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

The NDS resource module's `GameResources`/JSON traversal and sorted lookup
structure informed this port. The Android TMX map list, world map, map parser,
translations, and graphics/tile resources are not part of this JSON pack and
remain to be ported. NDS NitroFS setup, NDS build rules, player/world state,
typed gameplay views with fallback values, requirement/effect handling, and
drop-list generation were intentionally left behind. No gameplay systems or
replacement rules are implemented here.