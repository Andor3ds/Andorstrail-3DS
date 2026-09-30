import tempfile
import unittest
import base64
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from tools.build_map_data import copy_home_render_assets, copy_map_data


class MapDataBuilderTests(unittest.TestCase):
    def test_bundled_home_images_cover_default_layer_gids(self):
        project = Path(__file__).resolve().parents[1]
        map_root = ET.parse(project / "romfs" / "maps" / "home.tmx").getroot()
        tilesets = []
        for element in map_root.findall("tileset"):
            image = element.find("image")
            if image is not None:
                tilesets.append((
                    int(element.get("firstgid", "1")),
                    int(element.get("tilecount", "0")),
                    image.get("source"),
                    int(image.get("width", "0")),
                    int(image.get("height", "0")),
                ))

        used = set()
        for layer in map_root.findall("layer"):
            if layer.get("name", "").lower() not in {"base", "ground", "objects", "above", "top"}:
                continue
            data = layer.find("data")
            compressed = base64.b64decode("".join((data.text or "").split()))
            raw = zlib.decompress(compressed)
            gids = struct.unpack(f"<{len(raw) // 4}I", raw)
            used.update(gid & 0x0FFFFFFF for gid in gids if gid != 0)

        required_images = []
        for gid in sorted(used):
            tileset = next(entry for entry in reversed(tilesets) if entry[0] <= gid)
            self.assertLess(gid - tileset[0], tileset[1])
            image_name = Path(tileset[2]).name
            if image_name not in required_images:
                required_images.append(image_name)
        expected_list = required_images + ["char_hero.png"]

        gfx_list = (project / "romfs" / "gfx" / "tilesets.list").read_text(encoding="ascii").splitlines()
        self.assertEqual(gfx_list, expected_list)
        for image_name in expected_list:
            image = project / "assets" / "map" / "home" / image_name
            self.assertTrue(image.is_file(), image_name)
            if image_name != "char_hero.png":
                tileset = next(entry for entry in tilesets if Path(entry[2]).name == image_name)
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
        self.assertGreater(len(used), 0)

    def test_bundles_only_original_images_used_by_default_home_layers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            xml = source / "res" / "xml"
            drawable = source / "res" / "drawable"
            xml.mkdir(parents=True)
            drawable.mkdir(parents=True)
            (drawable / "ground.png").write_bytes(b"original-ground-png")
            (drawable / "replacement.png").write_bytes(b"replacement-only-png")
            (drawable / "char_hero.png").write_bytes(b"original-player-png")

            ground_data = base64.b64encode(zlib.compress(struct.pack("<2I", 1, 0))).decode("ascii")
            replacement_data = base64.b64encode(zlib.compress(struct.pack("<2I", 5, 5))).decode("ascii")
            (xml / "home.tmx").write_text(
                """<map width="2" height="1">
<tileset firstgid="1" name="ground" tilewidth="32" tileheight="32" tilecount="4" columns="2"><image source="../drawable/ground.png" width="64" height="64"/></tileset>
<tileset firstgid="5" name="replacement" tilewidth="32" tileheight="32" tilecount="4" columns="2"><image source="../drawable/replacement.png" width="64" height="64"/></tileset>
<layer name="Ground"><data encoding="base64" compression="zlib">""" + ground_data + """</data></layer>
<layer name="Objects_c"><data encoding="base64" compression="zlib">""" + replacement_data + """</data></layer>
</map>""",
                encoding="utf-8",
            )

            count, byte_count = copy_home_render_assets(source, root / "project")

            asset_directory = root / "project" / "assets" / "map" / "home"
            self.assertEqual(count, 2)
            self.assertEqual(byte_count, len(b"original-ground-png") + len(b"original-player-png"))
            self.assertEqual((asset_directory / "ground.png").read_bytes(), b"original-ground-png")
            self.assertEqual((asset_directory / "char_hero.png").read_bytes(), b"original-player-png")
            self.assertFalse((asset_directory / "replacement.png").exists())
            gfx_list = (root / "project" / "romfs" / "gfx" / "tilesets.list").read_text(encoding="ascii")
            self.assertEqual(gfx_list, "ground.png\nchar_hero.png\n")

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