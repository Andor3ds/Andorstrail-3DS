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
- `tools/build_map_data.py` bundles Android's configured maps, the four maps
  rendered by this build, and the original player/actor sprite sheets.

The resource layer follows Android's `loadresources.xml` lists, preserves those
JSON documents, and exposes records through a generated `(resource type, ID)`
index. Debug-only and explicitly unused raw files are excluded. The gameplay
layer interprets Mikhail's conversation requirements/rewards, item pricing and
drops, generic monster stats and spawn groups, and source-derived combat
statistics and effects for the bundled maps.
The bundled index identifies source commit
`8b91a68bc3dc14181c5f0d89da781dfe5be87b09` of
[AndorsTrailMods/andors-trail](https://github.com/AndorsTrailMods/andors-trail/tree/8b91a68bc3dc14181c5f0d89da781dfe5be87b09).
To regenerate from an Android source checkout, run:

```sh
python3 tools/build_game_data.py --source /path/to/andors-trail/AndorsTrail
```

The resource pack, map files, and map/player/actor graphics were verified
against that pinned checkout. Its graphics bundle omits the red and yellow
nine-patch UI overlays used by this port, so those two compatibility images
remain in the existing UI bundle and cannot be regenerated from this checkout.
The source content and graphics remain subject to the upstream copyright and
licensing notices in its
[README](https://github.com/AndorsTrailMods/andors-trail/blob/8b91a68bc3dc14181c5f0d89da781dfe5be87b09/readme.md)
and [graphics contributions](https://github.com/AndorsTrailMods/andors-trail/blob/8b91a68bc3dc14181c5f0d89da781dfe5be87b09/gfxcontrib.md).

The app starts on the original Android Home TMX map. Use the D-pad to move,
A to talk/interact or attack a nearby hostile monster, X as an alternate
attack button, B to end a combat turn, and START to exit. Android target
selection uses touch; on 3DS, L/R cycles adjacent combat targets. Open Hero
Info and select Items to use consumables or equip gear; tap an item and press A
to use/equip, or tap a worn slot and press A to unequip. Four inventory rows
are visible at once, with category and sort controls above them. These actions use the
source-defined action-point costs during combat. Combat uses player and monster
action points, source monster stats, drops and experience, source-derived
equipment/condition bonuses, hit effects, and skill bonuses.
Combat adjacency includes diagonal tiles, and monsters use the original
obstacle-aware pathfinding rules. Enemy combat actions are paced to the
original game's one-second turn delay.
Mikhail is drawn with his original sprite at his Home spawn;
press A within two tiles to talk. Travel south to Crossglen, then enter the
Crossglen Hall to find Mara at her original spawn and buy bread. The two
training rats appear in Crossglen's original spawn area and wander within it;
Mikhail's quest asks the player to deal with them.
Generic spawn groups from the bundled maps also load their source monster
records and actor sheets. The status overlay shows nearby talk, shop, or combat
prompts. During
dialogue, the lower screen displays the message and controls; use Up/Down to
select a reply, A to choose it, and B to close. Mikhail blocks movement through
his occupied map tile.
Mikhail's Home introduction and bread/rat quests use the original conversation,
item, monster, drop-list, map-spawn, and actor-sprite records. Quest progress,
inventory, health, and gold are saved to `sdmc:/3ds/andorstrail-save.dat`.
In Hero Info > Items, tap the left selector to cycle the original item
categories and the right selector to cycle the original sort modes; Y/X cycle
categories and L/R cycle sort modes when using buttons.

The lower screen uses the original Android player portrait, health/experience
bars, toolbox, Hero Info tab names, blue frame/tab graphics, and UI/item icons.
Monster defeats award the source-derived experience;
the level-up choices and progression values are based on Android, and older
saves are migrated to the current progression format. The portrait opens Hero
Info and the toolbox exposes save and navigation entries. This UI port is still
incomplete: the quest log reads active, visible entries from the original quest
records for progress values in the save, but gameplay progression is only
implemented for the current Andor/Mikhail slice. The Skills tab lists the
original always-visible skill names and short descriptions; ranks, point
spending, rank limits, and visible-skill prerequisites are implemented from
the Android definitions. Weapon Accuracy, Hard Hit, Dodge, and Bark Skin
affect combat using the original per-rank bonuses; Quick Learner increases
combat experience and Corpse Eater restores health on kills. Fortitude
restores health on level-up. Critical skill, critical damage, monster immunity
to critical hits, evasion, taunt, and critical-condition skills use
source-derived rules. Equipment slots, equipment effects, consumable use,
condition immunity/resistance, timed player and monster condition effects,
and current-HP/AP hit and miss effects are implemented. Outside combat,
regular condition effects run on the source's six-second rounds and distinct
full-round effects run every 25 seconds; monster full-round effects also
resolve when combat ends. Timed conditions resolve before monsters act in
combat. Weapon and armor
proficiencies and the unarmed, two-handed, weapon-and-shield, and dual-wield
styles use source-derived formulas. Three quick-item slots persist in saves,
and non-unique spawn areas use their source respawn chance while outside
combat. Enemy conditions and respawn IDs persist in saves. Map visits retain
monster positions, health, and action points; revisits within the source's
three-minute window restore encounters, and older maps reset non-unique
monsters while retaining unique ones. Resting and player defeat apply the
source encounter reset behavior.

The broader world and script-driven spawn-area activation are still incomplete,
so encounter behavior outside the bundled gameplay slice can differ from
Android.
The world-map view is unavailable until the original visited-map tiles are
bundled. Inventory names and item sprites come from the original item records
and can be scrolled.
HUD and dialogue text use Citro2D's system-font fallback when a font handle is
not available; visual rendering still needs confirmation on hardware.

The Home map's source GIDs resolve to
the original tileset PNGs; the top-screen camera follows the player, and D-pad
movement uses the Android `Walkable` layer (`0` is walkable, nonzero is blocked).
The visible draw order follows Android: base, ground, objects, player, above,
top. Rendering is in the native 3DS layer and remains separate from map/movement
logic. Conditional replacement layers are not activated until their gameplay
requirements are ported; maps, encounter lifecycle, and gameplay beyond the
bundled-map combat and Mikhail slice are still incomplete. No NDS
renderer/backgrounds or libnds APIs are used.