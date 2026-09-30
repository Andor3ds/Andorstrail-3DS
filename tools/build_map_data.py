#!/usr/bin/env python3
"""Bundle Android's configured TMX maps and world map into native 3DS ROMFS."""

from __future__ import annotations

import argparse
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path


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


if __name__ == "__main__":
    main()