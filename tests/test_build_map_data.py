import tempfile
import unittest
import base64
import re
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from tools.build_map_data import (
    ACTOR_SPRITE_SHEETS,
    TITLE_ASSETS,
    UI_ASSETS,
    actor_icon_sheets,
    copy_map_render_assets,
    copy_map_data,
    copy_ui_assets,
    copy_title_assets,
)

def _png(width, height, color=(12, 34, 56, 255)):
    def chunk(name, content):
        body = name + content
        return (
            struct.pack(">I", len(content)) + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    row = bytes(color) * width
    pixels = b"".join(b"\0" + row for _ in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(pixels))
        + chunk(b"IEND", b"")
    )


class MapDataBuilderTests(unittest.TestCase):
    def test_hud_image_order_matches_bundled_ui_assets(self):
        project = Path(__file__).resolve().parents[1]
        source = (project / "source" / "platform" / "3ds" / "hud_view.cpp").read_text(
            encoding="utf-8"
        )
        match = re.search(r"UI_IMAGE_NAMES\[\] = \{(.*?)\};", source, re.DOTALL)
        self.assertIsNotNone(match)
        self.assertEqual(re.findall(r'"([^"]+)"', match.group(1)), list(UI_ASSETS))

    def test_bundles_original_ui_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            drawable = source / "res" / "drawable"
            drawable.mkdir(parents=True)
            for name in UI_ASSETS:
                (drawable / name).write_bytes(f"original-{name}".encode("ascii"))

            count, byte_count = copy_ui_assets(source, root / "project")

            asset_directory = root / "project" / "assets" / "ui"
            self.assertEqual(count, len(UI_ASSETS))
            self.assertEqual(
                byte_count,
                sum(len(f"original-{name}".encode("ascii")) for name in UI_ASSETS),
            )
            for name in UI_ASSETS:
                self.assertEqual(
                    (asset_directory / name).read_bytes(),
                    f"original-{name}".encode("ascii"),
                )
            self.assertEqual(
                (root / "project" / "romfs" / "gfx" / "ui" / "ui.list")
                .read_text(encoding="ascii").splitlines(),
                list(UI_ASSETS),
            )

    def test_missing_ui_asset_does_not_partially_replace_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android" / "res" / "drawable"
            source.mkdir(parents=True)
            project = root / "project"
            asset_directory = project / "assets" / "ui"
            asset_directory.mkdir(parents=True)
            existing_asset = asset_directory / UI_ASSETS[0]
            existing_asset.write_bytes(b"existing")
            for name in UI_ASSETS[1:]:
                (source / name).write_bytes(f"original-{name}".encode("ascii"))

            with self.assertRaisesRegex(ValueError, "ui_bar_background"):
                copy_ui_assets(source.parents[1], project)

            self.assertEqual(existing_asset.read_bytes(), b"existing")

    def test_bundles_original_title_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            for name in TITLE_ASSETS:
                resource_dir = "drawable-nodpi" if name.startswith("ts_") else "drawable"
                drawable = source / "res" / resource_dir
                drawable.mkdir(parents=True, exist_ok=True)
                (drawable / name).write_bytes(f"original-{name}".encode("ascii"))

            count, byte_count = copy_title_assets(source, root / "project")

            self.assertEqual(count, len(TITLE_ASSETS))
            self.assertEqual(
                byte_count,
                sum(len(f"original-{name}".encode("ascii")) for name in TITLE_ASSETS),
            )
            for name in TITLE_ASSETS:
                self.assertEqual(
                    (root / "project" / "assets" / "title" / name).read_bytes(),
                    f"original-{name}".encode("ascii"),
                )
            self.assertEqual(
                (root / "project" / "romfs" / "gfx" / "title" / "title.list")
                .read_text(encoding="ascii").splitlines(),
                list(TITLE_ASSETS),
            )

    def test_missing_title_asset_does_not_partially_replace_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            for name in TITLE_ASSETS[1:]:
                resource_dir = "drawable-nodpi" if name.startswith("ts_") else "drawable"
                drawable = source / "res" / resource_dir
                drawable.mkdir(parents=True, exist_ok=True)
                (drawable / name).write_bytes(b"existing source")
            project = root / "project"
            asset_directory = project / "assets" / "title"
            asset_directory.mkdir(parents=True)
            existing_asset = asset_directory / TITLE_ASSETS[0]
            existing_asset.write_bytes(b"existing")

            with self.assertRaisesRegex(ValueError, "ts_background.png"):
                copy_title_assets(source, project)

            self.assertEqual(existing_asset.read_bytes(), b"existing")

    def test_bundled_transition_map_images_cover_default_layer_gids(self):
        project = Path(__file__).resolve().parents[1]
        required_images = []
        tilesets_by_image = {}
        used_cell_count = 0
        for map_id in ("home", "crossglen", "crossglen_hall", "wild1"):
            map_root = ET.parse(project / "romfs" / "maps" / f"{map_id}.tmx").getroot()
            tilesets = []
            for element in map_root.findall("tileset"):
                image = element.find("image")
                if image is not None:
                    tile_width = int(element.get("tilewidth", "32"))
                    tile_height = int(element.get("tileheight", "32"))
                    image_width = int(image.get("width", "0"))
                    image_height = int(image.get("height", "0"))
                    columns = int(element.get("columns", "0")) or image_width // tile_width
                    tile_count = int(element.get("tilecount", "0")) or (
                        columns * (image_height // tile_height)
                    )
                    tileset = (
                        int(element.get("firstgid", "1")),
                        tile_count,
                        image.get("source"),
                        int(image.get("width", "0")),
                        int(image.get("height", "0")),
                    )
                    tilesets.append(tileset)
                    tilesets_by_image.setdefault(Path(tileset[2]).name, tileset)

            used = set()
            for layer in map_root.findall("layer"):
                if layer.get("name", "").lower() not in {"base", "ground", "objects", "above", "top"}:
                    continue
                data = layer.find("data")
                compressed = base64.b64decode("".join((data.text or "").split()))
                raw = zlib.decompress(compressed)
                gids = struct.unpack(f"<{len(raw) // 4}I", raw)
                used.update(gid & 0x0FFFFFFF for gid in gids if gid != 0)
                used_cell_count += sum(gid != 0 for gid in gids)

            for gid in sorted(used):
                tileset = next(entry for entry in reversed(tilesets) if entry[0] <= gid)
                self.assertLess(gid - tileset[0], tileset[1])
                image_name = Path(tileset[2]).name
                if image_name not in required_images:
                    required_images.append(image_name)
        gfx_list = (project / "romfs" / "gfx" / "tilesets.list").read_text(encoding="ascii").splitlines()
        atlas_names = {
            "actor_atlas_050.png", "actor_atlas_075.png", "actor_atlas_100.png"
        }
        self.assertEqual(
            gfx_list,
            required_images + ["char_hero.png"] + sorted(atlas_names),
        )
        self.assertEqual(len(gfx_list), len(set(gfx_list)))
        for image_name in gfx_list:
            image = project / "assets" / "map" / "home" / image_name
            self.assertTrue(image.is_file(), image_name)
            if image_name in atlas_names:
                png_header = image.read_bytes()[:24]
                self.assertEqual(png_header[:8], b"\x89PNG\r\n\x1a\n")
            elif image_name != "char_hero.png":
                tileset = tilesets_by_image[image_name]
                png_header = image.read_bytes()[:24]
                self.assertEqual(png_header[:8], b"\x89PNG\r\n\x1a\n")
                png_width, png_height = struct.unpack(">II", png_header[16:24])
                self.assertGreaterEqual(png_width, tileset[3])
                self.assertGreaterEqual(png_height, tileset[4])
            else:
                png_header = image.read_bytes()[:26]
                self.assertEqual(struct.unpack(">II", png_header[16:24]), (32, 32))
                self.assertEqual(png_header[24], 8)
                self.assertEqual(png_header[25], 6)
        self.assertGreater(used_cell_count, 0)

    def test_bundles_only_original_images_used_by_default_home_layers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            xml = source / "res" / "xml"
            drawable = source / "res" / "drawable"
            xml.mkdir(parents=True)
            drawable.mkdir(parents=True)
            raw = source / "res" / "raw"
            values = source / "res" / "values"
            raw.mkdir()
            values.mkdir()
            (values / "loadresources.xml").write_text(
                '<resources><array name="loadresource_monsters">'
                '<item>@raw/monsters</item></array></resources>',
                encoding="utf-8",
            )
            (raw / "monsters.json").write_text(
                '[{"id":"rat","spawnGroup":"rat","iconID":"monsters_rats:0"},'
                '{"id":"wasp","spawnGroup":"wasps","iconID":"monsters_insects:1"}]',
                encoding="utf-8",
            )
            (drawable / "ground.png").write_bytes(b"original-ground-png")
            (drawable / "replacement.png").write_bytes(b"replacement-only-png")
            (drawable / "char_hero.png").write_bytes(_png(32, 32))
            for name in ACTOR_SPRITE_SHEETS:
                (drawable / name).write_bytes(f"original-{name}".encode("ascii"))
            (drawable / "monsters_insects.png").write_bytes(b"original-insects")
            for name, image in (
                ("monsters_mage2.png", _png(32, 32)),
                ("monsters_men.png", _png(256, 32)),
                ("monsters_insects.png", _png(64, 32)),
            ):
                (drawable / name).write_bytes(image)
            actor_directory = root / "project" / "assets" / "map" / "home"
            actor_directory.mkdir(parents=True)

            ground_data = base64.b64encode(zlib.compress(struct.pack("<2I", 1, 0))).decode("ascii")
            replacement_data = base64.b64encode(zlib.compress(struct.pack("<2I", 5, 5))).decode("ascii")
            (xml / "home.tmx").write_text(
                """<map width="2" height="1">
<tileset firstgid="1" name="ground" tilewidth="32" tileheight="32" tilecount="4" columns="2"><image source="../drawable/ground.png" width="64" height="64"/></tileset>
<tileset firstgid="5" name="replacement" tilewidth="32" tileheight="32" tilecount="4" columns="2"><image source="../drawable/replacement.png" width="64" height="64"/></tileset>
                <objectgroup name="Spawn"><object name="wasps"><properties><property name="spawngroup" value="wasps"/></properties></object></objectgroup>
                <layer name="Ground"><data encoding="base64" compression="zlib">""" + ground_data + """</data></layer>
<layer name="Objects_c"><data encoding="base64" compression="zlib">""" + replacement_data + """</data></layer>
</map>""",
                encoding="utf-8",
            )

            count, byte_count = copy_map_render_assets(source, root / "project", ("home",))

            asset_directory = root / "project" / "assets" / "map" / "home"
            self.assertEqual(count, 3 + len(ACTOR_SPRITE_SHEETS) + 3)
            self.assertEqual(
                byte_count,
                sum(path.stat().st_size for path in asset_directory.iterdir())
                + (root / "project" / "romfs" / "gfx" / "actor_icons.list").stat().st_size,
            )
            self.assertEqual((asset_directory / "ground.png").read_bytes(), b"original-ground-png")
            self.assertEqual((asset_directory / "char_hero.png").read_bytes(), _png(32, 32))
            self.assertFalse((asset_directory / "replacement.png").exists())
            gfx_list = (root / "project" / "romfs" / "gfx" / "tilesets.list").read_text(encoding="ascii")
            self.assertEqual(
                gfx_list,
                "ground.png\nchar_hero.png\n"
                + "actor_atlas_050.png\nactor_atlas_075.png\n"
                + "actor_atlas_100.png\n",
            )
            for name in ACTOR_SPRITE_SHEETS:
                self.assertEqual(
                    (asset_directory / name).read_bytes(),
                    _png(32, 32) if name == "monsters_mage2.png"
                    else _png(256, 32) if name == "monsters_men.png"
                    else f"original-{name}".encode("ascii"),
                )
            self.assertEqual(
                (asset_directory / "monsters_insects.png").read_bytes(),
                _png(64, 32),
            )
            icons = (root / "project" / "romfs" / "gfx" / "actor_icons.list")
            self.assertEqual(
                icons.read_text(encoding="ascii").splitlines(),
                [
                    "char_hero.png 0 0",
                    "monsters_mage2.png 0 1",
                    "monsters_men.png 7 2",
                    "monsters_insects.png 1 3",
                ],
            )

    def test_actor_icon_sheets_follow_spawn_group_records(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            (source / "res" / "xml").mkdir(parents=True)
            (source / "res" / "raw").mkdir()
            (source / "res" / "values").mkdir()
            (source / "res" / "values" / "loadresources.xml").write_text(
                '<resources><array name="loadresource_monsters">'
                '<item>@raw/monsters</item></array></resources>',
                encoding="utf-8",
            )
            (source / "res" / "raw" / "monsters.json").write_text(
                '[{"id":"rat","spawnGroup":"rats","iconID":"monsters_rats:0"},'
                '{"id":"wasp","spawnGroup":"wasps","iconID":"monsters_insects:1"}]',
                encoding="utf-8",
            )
            drawable = source / "res" / "drawable"
            drawable.mkdir()
            (drawable / "monsters_mage2.png").write_bytes(_png(32, 32))
            (drawable / "monsters_men.png").write_bytes(_png(256, 32))
            (source / "res" / "xml" / "home.tmx").write_text(
                '<map><objectgroup><object name="wasp_area"><properties>'
                '<property name="spawngroup" value="wasps"/>'
                '</properties></object></objectgroup></map>',
                encoding="utf-8",
            )

            self.assertEqual(
                actor_icon_sheets(source, ("home",)),
                ACTOR_SPRITE_SHEETS + ("monsters_insects.png",),
            )

    def test_infers_old_tmx_tileset_grid_from_image_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            xml = source / "res" / "xml"
            drawable = source / "res" / "drawable"
            xml.mkdir(parents=True)
            drawable.mkdir(parents=True)
            raw = source / "res" / "raw"
            values = source / "res" / "values"
            raw.mkdir()
            values.mkdir()
            (values / "loadresources.xml").write_text(
                '<resources><array name="loadresource_monsters" /></resources>',
                encoding="utf-8",
            )
            for name in ACTOR_SPRITE_SHEETS:
                (drawable / name).write_bytes(f"original-{name}".encode("ascii"))
            (drawable / "monsters_mage2.png").write_bytes(_png(32, 32))
            (drawable / "monsters_men.png").write_bytes(_png(256, 32))
            (drawable / "old_tiles.png").write_bytes(b"old tiles")
            (drawable / "char_hero.png").write_bytes(_png(32, 32))
            actor_directory = root / "project" / "assets" / "map" / "home"
            actor_directory.mkdir(parents=True)
            for name in ACTOR_SPRITE_SHEETS:
                (actor_directory / name).write_bytes(f"original-{name}".encode("ascii"))
            layer = base64.b64encode(zlib.compress(struct.pack("<I", 100))).decode("ascii")
            (xml / "wild1.tmx").write_text(
                """<map width="1" height="1"><tileset firstgid="1" name="old" tilewidth="32" tileheight="32"><image source="../drawable/old_tiles.png" width="576" height="192"/></tileset>
<layer name="Ground"><data encoding="base64" compression="zlib">""" + layer + """</data></layer></map>""",
                encoding="utf-8",
            )

            count, _ = copy_map_render_assets(source, root / "project", ("wild1",))

            self.assertEqual(count, 2 + len(ACTOR_SPRITE_SHEETS) + 3)
            self.assertTrue((root / "project/assets/map/home/old_tiles.png").is_file())

    def test_copies_only_configured_maps_and_worldmap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            xml = source / "res" / "xml"
            values = source / "res" / "values"
            xml.mkdir(parents=True)
            values.mkdir(parents=True)
            (xml / "home.tmx").write_text("<map name='home'/>", encoding="utf-8")
            (xml / "template.tmx").write_text("<map name='template'/>", encoding="utf-8")
            (xml / "worldmap.xml").write_text("<worldmap/>", encoding="utf-8")
            (values / "loadresources.xml").write_text(
                "<resources><array name='loadresource_maps'>"
                "<item>@xml/home</item></array></resources>",
                encoding="utf-8",
            )

            count, byte_count = copy_map_data(source, root / "project")

            target = root / "project" / "romfs" / "maps"
            self.assertEqual(count, 1)
            self.assertEqual(byte_count, len((xml / "home.tmx").read_bytes())
                + len((xml / "worldmap.xml").read_bytes()))
            self.assertEqual((target / "home.tmx").read_bytes(), (xml / "home.tmx").read_bytes())
            self.assertEqual((target / "worldmap.xml").read_bytes(), (xml / "worldmap.xml").read_bytes())
            self.assertEqual((target / "maps.list").read_text(encoding="ascii"), "home\n")
            self.assertFalse((target / "template.tmx").exists())

    def test_rejects_missing_configured_map(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            (source / "res" / "xml").mkdir(parents=True)
            values = source / "res" / "values"
            values.mkdir(parents=True)
            (source / "res" / "xml" / "worldmap.xml").write_text("<worldmap/>", encoding="utf-8")
            (values / "loadresources.xml").write_text(
                "<resources><array name='loadresource_maps'>"
                "<item>@xml/missing</item></array></resources>",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "missing.tmx"):
                copy_map_data(source, root / "project")


if __name__ == "__main__":
    unittest.main()