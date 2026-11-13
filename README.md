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

The app opens on a source-labeled start menu. Continue resumes the hidden
quicksave; New game confirms before replacing an existing quicksave; Load opens
numbered manual save slots. Preferences exposes 3DS-native options for screen
placement, Circle Pad movement, touch movement, and scaling (0.75x and 1x).
Scaling zooms the map by changing tile size and the visible map
area; menus, HUD, and dialogs remain at their normal screen size. Map touch
movement follows the zoomed camera. The About / Help page keeps
the original section names, while detailed Android help content is not bundled.
The menu is adapted to the two 3DS screens and uses the original title-screen
background and logo artwork.

Use the D-pad or Circle Pad to move in eight directions, A to talk/interact,
open encounter choices, or attack during combat, Y to swap the map and Hero
Info screens, X to open Overview and press X again to return to the previous
map screen, B to end a combat turn, and START to exit. When
the map is on the bottom screen, tap or drag across the map to walk toward a location. Android
target selection uses touch; on 3DS, L/R cycles adjacent combat targets. Open
Hero Info and select Items to use consumables or equip gear. The Items screen shows
worn equipment above the categorized inventory, following the original portrait
layout. Tap a worn slot, then tap gear and press A to equip it there, or press
A on the selected slot to unequip it. Tap an item and press A to use/equip it
in its default slot. Four inventory rows are visible at once, with
original-style category and sort selectors above them. These actions use the
source-defined action-point costs during combat. Combat uses player and monster
action points, source monster stats, drops and experience, source-derived
equipment/condition bonuses, hit effects, and skill bonuses.
Combat adjacency includes diagonal tiles, and monsters use the original
obstacle-aware pathfinding rules. Aggressive monsters can engage from adjacent
tiles using the original evasion-based chance. Approaching an aggressive
monster opens the original Encounter choices: Yes enters combat, Info shows
monster details, and No declines. Use Left/Right and A to choose, or B to
decline; touching the corresponding option also selects it. Enemy combat
actions are paced to the original game's one-second turn delay.
Mikhail is drawn with his original sprite at his Home spawn; move next to him
or press A while adjacent (including diagonally) to talk. Travel south to Crossglen, then enter the
Crossglen Hall to find Mara at her original spawn and buy bread. The two
training rats appear in Crossglen's original spawn area and wander within it;
Mikhail's quest asks the player to deal with them.
Generic spawn groups from the bundled maps also load their source monster
records and actor sheets. The status overlay shows nearby talk, shop, or combat
prompts. During
dialogue, the lower screen displays the message and controls; use Up/Down to
select a reply, A to choose it, and B to close. Mikhail blocks movement through
his occupied map tile; NPC spawn tiles are solid to both player movement and
monster pathfinding.
Mikhail's Home introduction and bread/rat quests use the original conversation,
item, monster, drop-list, map-spawn, and actor-sprite records. Quest progress,
inventory, health, and gold are saved in the hidden Continue quicksave at
`sdmc:/3ds/andorstrail/quicksave.dat`; preferences are stored at
`sdmc:/3ds/andorstrail/preferences.cfg`, and manual slots are stored under
`sdmc:/3ds/andorstrail/Saves/savegameN`. On first launch after this path change,
existing quicksave, preferences, and manual slots are migrated from their old
locations. The game writes a manual slot only when you choose Save, writes a
new game to Continue as soon as it starts, and refreshes Continue when
suspended, on a normal software-close event, or on a clean exit with START.
Gameplay changes are not otherwise written to disk automatically. A forced
power-off or power loss may interrupt saving and lose recent changes.
In Hero Info > Items, tap the left selector to cycle the original item
categories and the right selector to cycle the original sort modes; Y/X cycle
categories when using buttons. X opens Hero Info on Overview from the map;
L/R changes Hero Info tabs without moving the player. In Overview, Up/Down or
a vertical touch drag browses the current and base combat statistics;
HP, XP, and AP bars are shown above them.

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
the Android definitions. Up/Down or vertical touch drags scroll the list; tap
a skill to select it, then tap Learn or press A. Weapon Accuracy, Hard Hit, Dodge,
and Bark Skin
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

Conversation-driven spawn-area activation, deactivation, and removal are
persisted. Effects for the current map apply immediately; effects for other
maps apply when those maps load. Other world/script effects remain incomplete,
so encounter behavior outside the bundled gameplay slice can differ from
Android.
The world-map view is unavailable until the original visited-map tiles are
bundled. Inventory names and item sprites come from the original item records
and can be scrolled.
HUD and dialogue text use Citro2D's system-font fallback when a font handle is
not available; visual rendering still needs confirmation on hardware.

The Home map's source GIDs resolve to
the original tileset PNGs; the top-screen camera follows the player, and D-pad
and Circle Pad movement use the Android `Walkable` layer (`0` is walkable,
nonzero is blocked).
The visible draw order follows Android: base, ground, objects, player, above,
top. Rendering is in the native 3DS layer and remains separate from map/movement
logic. Conditional replacement layers are not activated until their gameplay
requirements are ported; maps, encounter lifecycle, and gameplay beyond the
bundled-map combat and Mikhail slice are still incomplete. No NDS
renderer/backgrounds or libnds APIs are used.