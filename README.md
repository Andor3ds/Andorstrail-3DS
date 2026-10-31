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
`make test` for the host-side parser, resource, gameplay-state, dialogue,
inventory, combat, save/load, and asset-builder tests. Host tests require
GCC/G++, Python 3, TinyXML-2, and zlib development headers.

## Structure

- `source/core` contains the platform-independent JSON/resource and TMX/map
  parsers, GID lookup, collision, movement, and camera-coordinate logic.
- `source/platform/3ds` contains the libctru app and Citro2D map renderer.
- `tests` contains host tests for JSON parsing, original resource lookups, map
  decoding/collision/coordinates, Mikhail's gameplay slice, and asset bundling.
- `tools/build_game_data.py` rebuilds the compressed pack and index from the
    Android project's `res/raw` JSON files.
- `tools/build_map_data.py` bundles Android's configured maps, the Home and
  Crossglen Hall render textures, and the original player/actor sprite sheets.

The resource layer follows Android's `loadresources.xml` lists, preserves those
JSON documents, and exposes records through a generated `(resource type, ID)`
index. Debug-only and explicitly unused raw files are excluded. The Mikhail
gameplay layer interprets the conversation requirements/rewards, item pricing,
drop lists, and training-rat combat needed for this slice; other effects,
requirements, actors, and gameplay rules remain deferred.
The bundled index identifies Android source commit
`0dfa1f63ea6d944df2c6d34e641000c09d3e917e`.
To regenerate from an Android source checkout, run:

```sh
python3 tools/build_game_data.py --source /path/to/AndorsTrail
```

The source checkout used to verify this baseline is maintained separately from
this project. The pack and index are generated from that Android source tree;
the NDS resource pack and actor-spawn WIP pack were secondary references only.
The additional Crossglen Hall textures and actor sprite sheets come from the
Android source at
[commit `0acd31f`](https://github.com/karim14/andors-trail/tree/0acd31f090c735c5e357ce0ae4673974e92d7307/AndorsTrail).
The Hero Info item, equipment, and blue frame graphics were cross-checked against the
requested [AndorsTrailMods source at commit
`2194222`](https://github.com/AndorsTrailMods/andors-trail/tree/2194222d1d9fe8163ecd5ef00d3469f88f6f5218/AndorsTrail).
Retain the original asset credits and notices in
[the Android About strings](https://github.com/karim14/andors-trail/blob/0acd31f090c735c5e357ce0ae4673974e92d7307/AndorsTrail/res/values/strings_about.xml);
some bundled graphics are restricted to use as part of Andor's Trail.

The app starts on the original Android Home TMX map. Use the D-pad to move,
A to talk/interact, X to attack nearby training rats during Mikhail's rat quest,
and START to exit. Mikhail is drawn with his original sprite at his Home spawn;
press A within two tiles to talk. Travel south to Crossglen, then enter the
Crossglen Hall to find Mara at her original spawn and buy bread. The two
training rats appear in Crossglen's original spawn area after Mikhail gives you
that quest. The status overlay shows nearby talk, shop, or rat prompts. During
dialogue, the lower screen displays the message and controls; use Up/Down to
select a reply, A to choose it, and B to close. Mikhail blocks movement through
his occupied map tile.
Mikhail's Home introduction and bread/rat quests use the original conversation,
item, monster, drop-list, map-spawn, and actor-sprite records. Quest progress,
inventory, health, and gold are saved to `sdmc:/3ds/andorstrail-save.dat`.

The lower screen uses the original Android player portrait, health/experience
bars, toolbox, Hero Info tab names, blue frame/tab graphics, and UI/item icons.
Rat defeats award the source-derived experience;
the level-up choices and progression values are based on Android, and older
saves are migrated to the current progression format. The portrait opens Hero
Info and the toolbox exposes save and navigation entries. This UI port is still
incomplete: the quest log reads active, visible entries from the original quest
records for progress values in the save, but gameplay progression is only
implemented for the current Andor/Mikhail slice. Equipment is not yet backed by
equipped-item state, skill allocation and quick-item actions are not ported, and
the world-map view is unavailable until the original visited-map tiles are
bundled. Inventory names and item sprites come from the original item records
and can be scrolled. HUD and dialogue text use Citro2D's system-font fallback
when a font handle is not available; visual rendering still needs confirmation
on hardware.

The Home map's source GIDs resolve to
the original tileset PNGs; the top-screen camera follows the player, and D-pad
movement uses the Android `Walkable` layer (`0` is walkable, nonzero is blocked).
The visible draw order follows Android: base, ground, objects, player, above,
top. Rendering is in the native 3DS layer and remains separate from map/movement
logic. Conditional replacement layers are not activated until their gameplay
requirements are ported. Combat beyond Mikhail's two training rats, other
actors, shops, and quest chains remain deferred. No NDS renderer/backgrounds or
libnds APIs are used.