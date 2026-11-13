#!/usr/bin/env python3
"""Bundle Android's configured TMX maps and world map into native 3DS ROMFS."""

from __future__ import annotations

import argparse
import base64
import gzip
import json
import shutil
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

if __package__:
    from .pixel_sprite_downsample import build_actor_atlases
else:
    from pixel_sprite_downsample import build_actor_atlases

RENDER_MAPS = ("home", "crossglen", "crossglen_hall", "wild1")
RENDERED_LAYERS = {"base", "ground", "objects", "above", "top"}
ANDROID_GID_FLAGS = 0xF0000000
ACTOR_SPRITE_SHEETS = ("monsters_mage2.png", "monsters_men.png", "monsters_rats.png")
COMPATIBILITY_UI_ASSETS = (
    "ui_red_foreground.9.png",
    "ui_yellow_foreground.9.png",
)
UI_ASSETS = (
    "ui_bar_background.9.png",
    "ui_bar_red_foreground.9.png",
    "ui_bar_blue_foreground.9.png",
    "ui_icon_combat.png",
    "ui_icon_equipment.png",
    "ui_icon_map.png",
    "ui_icon_quest.png",
    "ui_icon_save.png",
    "ui_icon_skill.png",
    "ui_quickslots.png",
    "ui_red_foreground.9.png",
    "ui_selections.png",
    "ui_yellow_foreground.9.png",
    "ui_icon_coins.png",
    "equip_weapon.png",
    "equip_shield.png",
    "equip_body.png",
    "equip_feet.png",
    "equip_hand.png",
    "equip_head.png",
    "equip_neck.png",
    "equip_ring.png",
    "items_weapons.png",
    "items_armours.png",
    "items_jewelry.png",
    "items_consumables.png",
    "items_misc.png",
    "ui_blue_stdframe.9.png",
    "ui_blue_tabframe.9.png",
    "ui_blue_tabwidget_enabled_selected.9.png",
    "ui_blue_tabwidget_enabled_unselected.9.png",
    "ui_blue_tabframe_topoverlay.9.png",
    "char_hero_large.png",
    "equip_square.png",
)
TITLE_ASSETS = (
    "ts_background.png",
    "ts_midground.png",
    "ts_foreground.png",
    "logo_anim_shine_00.png",
)

def copy_map_data(source_root: Path, project_root: Path) -> tuple[int, int]:
    xml_root = source_root / "res" / "xml"
    map_list = source_root / "res" / "values" / "loadresources.xml"
    if not xml_root.is_dir() or not map_list.is_file():
        raise ValueError("Android source must contain res/xml and res/values/loadresources.xml")

    resources = ET.parse(map_list).getroot()
    map_array = next(
        (array for array in resources.findall("array")
         if array.get("name") == "loadresource_maps"),
        None,
    )
    if map_array is None:
        raise ValueError("Android loadresources.xml has no loadresource_maps array")

    names = []
    for item in map_array.findall("item"):
        reference = (item.text or "").strip()
        if not reference.startswith("@xml/"):
            raise ValueError(f"expected @xml map reference, got {reference!r}")
        name = reference[len("@xml/"):]
        if not name or "/" in name or "\\" in name or name in names:
            raise ValueError(f"invalid or duplicate map resource reference: {reference!r}")
        if not (xml_root / f"{name}.tmx").is_file():
            raise ValueError(f"configured Android map is missing: {name}.tmx")
        names.append(name)

    worldmap = xml_root / "worldmap.xml"
    if not worldmap.is_file():
        raise ValueError(f"Android world-map data is missing: {worldmap}")

    destination = project_root / "romfs" / "maps"
    destination.mkdir(parents=True, exist_ok=True)
    for name in names:
        shutil.copyfile(xml_root / f"{name}.tmx", destination / f"{name}.tmx")
    shutil.copyfile(worldmap, destination / "worldmap.xml")
    (destination / "maps.list").write_text("".join(f"{name}\n" for name in names), encoding="ascii")
    byte_count = sum((destination / f"{name}.tmx").stat().st_size for name in names)
    byte_count += (destination / "worldmap.xml").stat().st_size
    return len(names), byte_count


def _layer_gids(layer: ET.Element, width: int, height: int) -> tuple[int, ...]:
    data = layer.find("data")
    if data is None or data.get("encoding") != "base64":
        raise ValueError(f"unsupported TMX layer encoding on {layer.get('name')}")
    try:
        encoded = base64.b64decode("".join((data.text or "").split()), validate=True)
        compression = data.get("compression")
        if compression == "zlib":
            decoded = zlib.decompress(encoded)
        elif compression == "gzip":
            decoded = gzip.decompress(encoded)
        else:
            raise ValueError(f"unsupported TMX compression on {layer.get('name')}: {compression!r}")
    except (ValueError, zlib.error, OSError) as error:
        raise ValueError(f"invalid TMX layer data on {layer.get('name')}: {error}") from error
    expected_size = width * height * 4
    if len(decoded) != expected_size:
        raise ValueError(f"TMX layer {layer.get('name')} has {len(decoded)} bytes, expected {expected_size}")
    return struct.unpack(f"<{width * height}I", decoded)

def actor_icon_entries(
    source_root: Path,
    map_names: tuple[str, ...],
) -> tuple[tuple[str, int], ...]:
    resource_list = source_root / "res" / "values" / "loadresources.xml"
    raw_directory = source_root / "res" / "raw"
    arrays = ET.parse(resource_list).getroot()
    monster_array = next(
        (array for array in arrays.findall("array")
         if array.get("name") == "loadresource_monsters"),
        None,
    )
    if monster_array is None:
        raise ValueError("Android loadresources.xml has no loadresource_monsters array")

    monsters_by_group: dict[str, list[dict[str, object]]] = {}
    for item in monster_array.findall("item"):
        reference = (item.text or "").strip()
        if not reference.startswith("@raw/"):
            raise ValueError(f"expected @raw monster reference, got {reference!r}")
        name = reference[len("@raw/"):]
        path = raw_directory / f"{name}.json"
        if not name or "/" in name or "\\" in name or not path.is_file():
            raise ValueError(f"configured Android monster resource is missing: {name}")
        records = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(records, list):
            raise ValueError(f"Android monster resource is not an array: {name}")
        for monster in records:
            if not isinstance(monster, dict):
                raise ValueError(f"Android monster record is invalid: {name}")
            group = monster.get("spawnGroup", monster.get("id"))
            if isinstance(group, str):
                monsters_by_group.setdefault(group.lower(), []).append(monster)

    icons = [
        ("char_hero.png", 0),
        ("monsters_mage2.png", 0),
        ("monsters_men.png", 7),
    ]
    xml_root = source_root / "res" / "xml"
    for map_name in map_names:
        root = ET.parse(xml_root / f"{map_name}.tmx").getroot()
        for object_group in root.findall("objectgroup"):
            for spawn_area in object_group.findall("object"):
                properties = {
                    prop.get("name"): prop.get("value")
                    for prop in spawn_area.findall("properties/property")
                }
                if properties.get("active") == "false":
                    continue
                group = properties.get("spawngroup", spawn_area.get("name"))
                if not group:
                    continue
                candidates = monsters_by_group.get(group.lower(), ())
                if not candidates:
                    candidates = monsters_by_group.get(group.lower().replace(" ", "_"), ())
                for monster in candidates:
                    icon_id = monster.get("iconID")
                    if not isinstance(icon_id, str) or ":" not in icon_id:
                        continue
                    sheet, raw_index = icon_id.rsplit(":", 1)
                    if raw_index.isdigit():
                        icon = (sheet + ".png", int(raw_index))
                        if icon not in icons:
                            icons.append(icon)
    return tuple(icons)


def actor_icon_sheets(
    source_root: Path,
    map_names: tuple[str, ...],
) -> tuple[str, ...]:
    sheets = list(ACTOR_SPRITE_SHEETS)
    for sheet_name, _ in actor_icon_entries(source_root, map_names):
        if sheet_name != "char_hero.png" and sheet_name not in sheets:
            sheets.append(sheet_name)
    return tuple(sheets)


def copy_map_render_assets(
    source_root: Path,
    project_root: Path,
    map_names: tuple[str, ...] = RENDER_MAPS,
) -> tuple[int, int]:
    xml_root = source_root / "res" / "xml"
    used_images = []
    source_path_by_name = {}
    android_res = source_root / "res"
    for map_name in map_names:
        if not map_name or "/" in map_name or "\\" in map_name:
            raise ValueError(f"invalid map name for renderer asset bundle: {map_name!r}")
        map_path = xml_root / f"{map_name}.tmx"
        if not map_path.is_file():
            raise ValueError(f"Android renderer map is missing: {map_path}")
        map_root = ET.parse(map_path).getroot()
        width = int(map_root.get("width", "0"))
        height = int(map_root.get("height", "0"))
        if width <= 0 or height <= 0:
            raise ValueError(f"invalid dimensions in {map_path}")

        tilesets = []
        for element in map_root.findall("tileset"):
            image = element.find("image")
            if image is None or not image.get("source"):
                continue
            tile_width = int(element.get("tilewidth", "32"))
            tile_height = int(element.get("tileheight", "32"))
            image_width = int(image.get("width", "0"))
            image_height = int(image.get("height", "0"))
            columns = int(element.get("columns", "0"))
            if columns <= 0 and tile_width > 0 and image_width > 0:
                columns = image_width // tile_width
            tile_count = int(element.get("tilecount", "0"))
            if tile_count <= 0 and columns > 0 and tile_height > 0 and image_height > 0:
                tile_count = columns * (image_height // tile_height)
            tilesets.append({
                "first_gid": int(element.get("firstgid", "1")),
                "tile_count": tile_count,
                "image_source": image.get("source"),
            })

        used_gids = set()
        for layer in map_root.findall("layer"):
            if layer.get("name", "").lower() in RENDERED_LAYERS:
                used_gids.update(gid & ~ANDROID_GID_FLAGS
                    for gid in _layer_gids(layer, width, height) if gid != 0)

        used_tilesets = []
        for gid in sorted(used_gids):
            tileset = next((candidate for candidate in reversed(tilesets)
                if candidate["first_gid"] <= gid), None)
            if tileset is None or gid - tileset["first_gid"] >= tileset["tile_count"]:
                raise ValueError(f"{map_name} layer GID {gid} has no valid Android tileset")
            if tileset not in used_tilesets:
                used_tilesets.append(tileset)
        used_tilesets.sort(key=lambda tileset: tileset["first_gid"])

        for tileset in used_tilesets:
            source_path = (xml_root / tileset["image_source"]).resolve()
            if android_res.resolve() not in source_path.parents or not source_path.is_file():
                raise ValueError(f"Android tileset image is missing or outside res: {tileset['image_source']}")
            if source_path.name not in used_images:
                used_images.append(source_path.name)
                source_path_by_name[source_path.name] = source_path

    hero_path = android_res / "drawable" / "char_hero.png"
    if not hero_path.is_file():
        raise ValueError(f"original player image is missing: {hero_path}")
    if hero_path.name in used_images:
        raise ValueError("player image conflicts with a map tileset image")
    asset_directory = project_root / "assets" / "map" / "home"
    source_path_by_name[hero_path.name] = hero_path
    actor_sheets = actor_icon_sheets(source_root, map_names)
    actor_icons = actor_icon_entries(source_root, map_names)
    for name in actor_sheets:
        actor_path = android_res / "drawable" / name
        if not actor_path.is_file():
            raise ValueError(f"original actor sprite sheet is missing: {actor_path}")
        source_path_by_name[name] = actor_path
    source_names = list(dict.fromkeys(
        used_images + [hero_path.name] + list(actor_sheets)
    ))

    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory = project_root / "romfs" / "gfx"
    gfx_directory.mkdir(parents=True, exist_ok=True)
    for name in source_names:
        source_path = source_path_by_name[name]
        target_path = asset_directory / name
        if source_path.resolve() != target_path.resolve():
            shutil.copyfile(source_path, target_path)

    icon_count, icon_bytes = build_actor_atlases(
        asset_directory, actor_icons, asset_directory,
        gfx_directory / "actor_icons.list",
    )
    atlas_names = (
        "actor_atlas_050.png", "actor_atlas_075.png",
        "actor_atlas_100.png",
    )
    runtime_names = list(dict.fromkeys(used_images + [hero_path.name]))
    runtime_names.extend(atlas_names)
    (gfx_directory / "tilesets.list").write_text(
        "".join(f"{name}\n" for name in runtime_names), encoding="ascii"
    )
    byte_count = sum(source_path_by_name[name].stat().st_size
        for name in source_path_by_name)
    byte_count += icon_bytes
    return len(source_names) + len(atlas_names), byte_count


def validate_ui_assets(source_root: Path, project_root: Path) -> None:
    source_directory = source_root / "res" / "drawable"
    missing = [
        name for name in UI_ASSETS
        if not (source_directory / name).is_file()
        and not (
            name in COMPATIBILITY_UI_ASSETS
            and (project_root / "assets" / "ui" / name).is_file()
        )
    ]
    if missing:
        raise ValueError(
            "original Android UI assets are missing: " + ", ".join(missing)
        )


def copy_ui_assets(source_root: Path, project_root: Path) -> tuple[int, int]:
    validate_ui_assets(source_root, project_root)
    source_directory = source_root / "res" / "drawable"
    asset_directory = project_root / "assets" / "ui"
    gfx_directory = project_root / "romfs" / "gfx" / "ui"
    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory.mkdir(parents=True, exist_ok=True)

    byte_count = 0
    for name in UI_ASSETS:
        source_path = source_directory / name
        if not source_path.is_file():
            source_path = project_root / "assets" / "ui" / name
        target_path = asset_directory / name
        if source_path.resolve() != target_path.resolve():
            shutil.copyfile(source_path, target_path)
        byte_count += target_path.stat().st_size
    (gfx_directory / "ui.list").write_text(
        "".join(f"{name}\n" for name in UI_ASSETS), encoding="ascii"
    )
    return len(UI_ASSETS), byte_count


def copy_title_assets(source_root: Path, project_root: Path) -> tuple[int, int]:
    source_directories = {
        name: source_root / "res" / (
            "drawable-nodpi" if name.startswith("ts_") else "drawable"
        )
        for name in TITLE_ASSETS
    }
    missing = [
        name for name in TITLE_ASSETS
        if not (source_directories[name] / name).is_file()
    ]
    if missing:
        raise ValueError("original Android title assets are missing: " + ", ".join(missing))

    asset_directory = project_root / "assets" / "title"
    gfx_directory = project_root / "romfs" / "gfx" / "title"
    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory.mkdir(parents=True, exist_ok=True)
    byte_count = 0
    for name in TITLE_ASSETS:
        source_path = source_directories[name] / name
        target_path = asset_directory / name
        if source_path.resolve() != target_path.resolve():
            shutil.copyfile(source_path, target_path)
        byte_count += target_path.stat().st_size
    (gfx_directory / "title.list").write_text(
        "".join(f"{name}\n" for name in TITLE_ASSETS), encoding="ascii"
    )
    return len(TITLE_ASSETS), byte_count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Android Andor's Trail project root")
    parser.add_argument(
        "--project",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="native 3DS project root",
    )
    parser.add_argument(
        "--render-maps",
        nargs="+",
        default=RENDER_MAPS,
        help="Android map IDs whose default layer textures should be bundled",
    )
    arguments = parser.parse_args()
    validate_ui_assets(arguments.source, arguments.project)
    map_count, byte_count = copy_map_data(arguments.source, arguments.project)
    print(f"Bundled {map_count} Android maps and worldmap.xml ({byte_count} bytes)")
    image_count, image_bytes = copy_map_render_assets(
        arguments.source, arguments.project, tuple(arguments.render_maps)
    )
    print(f"Bundled original map/player/actor images for {', '.join(arguments.render_maps)} ({image_count} images, {image_bytes} bytes)")
    ui_count, ui_bytes = copy_ui_assets(arguments.source, arguments.project)
    print(f"Bundled {ui_count} original Android UI images ({ui_bytes} bytes)")
    title_count, title_bytes = copy_title_assets(arguments.source, arguments.project)
    print(f"Bundled {title_count} original Android title images ({title_bytes} bytes)")


if __name__ == "__main__":
    main()