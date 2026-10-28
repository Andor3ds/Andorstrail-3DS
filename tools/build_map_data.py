#!/usr/bin/env python3
"""Bundle Android's configured TMX maps and world map into native 3DS ROMFS."""

from __future__ import annotations

import argparse
import base64
import gzip
import shutil
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

RENDER_MAPS = ("home", "crossglen", "crossglen_hall", "wild1")
RENDERED_LAYERS = {"base", "ground", "objects", "above", "top"}
ANDROID_GID_FLAGS = 0xF0000000
ACTOR_SPRITE_SHEETS = ("monsters_mage2.png", "monsters_men.png", "monsters_rats.png")
UI_ASSETS = (
    "ui_bar_background.9.png",
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
    for name in ACTOR_SPRITE_SHEETS:
        actor_path = asset_directory / name
        if not actor_path.is_file():
            raise ValueError(f"original actor sprite sheet is missing: {actor_path}")
        source_path_by_name[name] = actor_path
    source_names = used_images + [hero_path.name] + list(ACTOR_SPRITE_SHEETS)

    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory = project_root / "romfs" / "gfx"
    gfx_directory.mkdir(parents=True, exist_ok=True)
    for name in source_names:
        source_path = source_path_by_name[name]
        target_path = asset_directory / name
        if source_path.resolve() != target_path.resolve():
            shutil.copyfile(source_path, target_path)

    (gfx_directory / "tilesets.list").write_text(
        "".join(f"{name}\n" for name in source_names), encoding="ascii"
    )
    byte_count = sum(source_path_by_name[name].stat().st_size for name in source_names)
    return len(source_names), byte_count


def copy_ui_assets(source_root: Path, project_root: Path) -> tuple[int, int]:
    source_directory = source_root / "res" / "drawable"
    asset_directory = project_root / "assets" / "ui"
    gfx_directory = project_root / "romfs" / "gfx" / "ui"
    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory.mkdir(parents=True, exist_ok=True)

    byte_count = 0
    for name in UI_ASSETS:
        source_path = source_directory / name
        if not source_path.is_file():
            raise ValueError(f"original Android UI asset is missing: {source_path}")
        target_path = asset_directory / name
        if source_path.resolve() != target_path.resolve():
            shutil.copyfile(source_path, target_path)
        byte_count += target_path.stat().st_size
    (gfx_directory / "ui.list").write_text(
        "".join(f"{name}\n" for name in UI_ASSETS), encoding="ascii"
    )
    return len(UI_ASSETS), byte_count


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
    map_count, byte_count = copy_map_data(arguments.source, arguments.project)
    print(f"Bundled {map_count} Android maps and worldmap.xml ({byte_count} bytes)")
    image_count, image_bytes = copy_map_render_assets(
        arguments.source, arguments.project, tuple(arguments.render_maps)
    )
    print(f"Bundled original map/player/actor images for {', '.join(arguments.render_maps)} ({image_count} images, {image_bytes} bytes)")
    ui_count, ui_bytes = copy_ui_assets(arguments.source, arguments.project)
    print(f"Bundled {ui_count} original Android UI images ({ui_bytes} bytes)")


if __name__ == "__main__":
    main()