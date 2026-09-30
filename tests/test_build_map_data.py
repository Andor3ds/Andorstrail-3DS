import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from tools.build_map_data import copy_map_data


class MapDataBuilderTests(unittest.TestCase):
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