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

INITIAL_MAP = "home"
RENDERED_LAYERS = {"base", "ground", "objects", "above", "top"}
ANDROID_GID_FLAGS = 0xF0000000

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


def copy_home_render_assets(source_root: Path, project_root: Path) -> tuple[int, int]:
    xml_root = source_root / "res" / "xml"
    map_path = xml_root / f"{INITIAL_MAP}.tmx"
    if not map_path.is_file():
        raise ValueError(f"initial Android map is missing: {map_path}")

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
        tilesets.append({
            "first_gid": int(element.get("firstgid", "1")),
            "tile_count": int(element.get("tilecount", "0")),
            "name": element.get("name", ""),
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
            raise ValueError(f"Home layer GID {gid} has no valid Android tileset")
        if tileset not in used_tilesets:
            used_tilesets.append(tileset)
    used_tilesets.sort(key=lambda tileset: tileset["first_gid"])

    android_res = source_root / "res"
    source_files = []
    seen_names = set()
    for tileset in used_tilesets:
        source_path = (xml_root / tileset["image_source"]).resolve()
        if android_res.resolve() not in source_path.parents or not source_path.is_file():
            raise ValueError(f"Android tileset image is missing or outside res: {tileset['image_source']}")
        if source_path.name in seen_names:
            raise ValueError(f"duplicate Home tileset image basename: {source_path.name}")
        seen_names.add(source_path.name)
        source_files.append(source_path)

    hero_path = android_res / "drawable" / "char_hero.png"
    if not hero_path.is_file():
        raise ValueError(f"original player image is missing: {hero_path}")
    if hero_path.name in seen_names:
        raise ValueError("player image conflicts with a map tileset image")
    source_files.append(hero_path)

    asset_directory = project_root / "assets" / "map" / INITIAL_MAP
    asset_directory.mkdir(parents=True, exist_ok=True)
    gfx_directory = project_root / "romfs" / "gfx"
    gfx_directory.mkdir(parents=True, exist_ok=True)
    for path in source_files:
        shutil.copyfile(path, asset_directory / path.name)

    names = [path.name for path in source_files]
    (gfx_directory / "tilesets.list").write_text(
        "".join(f"{name}\n" for name in names), encoding="ascii"
    )
    byte_count = sum(path.stat().st_size for path in source_files)
    return len(source_files), byte_count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Android Andor's Trail project root")
    parser.add_argument(
        "--project",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="native 3DS project root",
    )
    arguments = parser.parse_args()
    map_count, byte_count = copy_map_data(arguments.source, arguments.project)
    print(f"Bundled {map_count} Android maps and worldmap.xml ({byte_count} bytes)")
    image_count, image_bytes = copy_home_render_assets(arguments.source, arguments.project)
    print(f"Bundled {image_count - 1} original Home tilesets and player image ({image_bytes} bytes)")


if __name__ == "__main__":
    main()