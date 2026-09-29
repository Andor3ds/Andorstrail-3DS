import tempfile
import unittest
import zlib
from pathlib import Path

from tools.build_game_data import build_game_data


class GameDataBuilderTests(unittest.TestCase):
    def test_builds_only_android_configured_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "android"
            raw = source / "res" / "raw"
            raw.mkdir(parents=True)
            (raw / "itemlist_fixture.json").write_text(
                '[{"id":"z_item"},{"id":"a_item"}]', encoding="utf-8"
            )
            (raw / "questlist_fixture.json").write_text(
                '[{"id":"quest","stages":[]}]', encoding="utf-8"
            )
            (raw / "itemlist_debug.json").write_text(
                '[{"id":"debug_item"}]', encoding="utf-8"
            )
            values = source / "res" / "values"
            values.mkdir(parents=True)
            (values / "loadresources.xml").write_text(
                """<resources>
<array name="loadresource_items"><item>@raw/itemlist_fixture</item></array>
<array name="loadresource_monsters" />
<array name="loadresource_quests"><item>@raw/questlist_fixture</item></array>
<array name="loadresource_conversationlists" />
<array name="loadresource_actorconditions" />
<array name="loadresource_droplists" />
<array name="loadresource_itemcategories" />
<array name="loadresource_itemfilters" />
</resources>""",
                encoding="utf-8",
            )
            project = root / "project"

            count, packed_size = build_game_data(source, project)

            pack = (project / "romfs" / "game_data.pack").read_bytes()
            index = (project / "source" / "core" / "game_resource_index.c").read_text(
                encoding="ascii"
            )
            header = (project / "source" / "core" / "game_resource_index.h").read_text(
                encoding="ascii"
            )
            self.assertEqual(count, 2)
            self.assertEqual(packed_size, len(pack))
            self.assertIn('"a_item"', index)
            self.assertIn('"quest"', index)
            self.assertLess(index.index('"a_item"'), index.index('"z_item"'))
            self.assertNotIn('"debug_item"', index)
            first_document = zlib.decompressobj().decompress(pack)
            self.assertEqual(
                first_document,
                (raw / "itemlist_fixture.json").read_bytes(),
            )
            self.assertIn("GAME_RESOURCE_SOURCE_COMMIT", header)

    def test_rejects_missing_configured_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "android"
            raw = source / "res" / "raw"
            raw.mkdir(parents=True)
            values = source / "res" / "values"
            values.mkdir(parents=True)
            (values / "loadresources.xml").write_text(
                """<resources>
<array name="loadresource_items"><item>@raw/missing</item></array>
<array name="loadresource_monsters" />
<array name="loadresource_quests" />
<array name="loadresource_conversationlists" />
<array name="loadresource_actorconditions" />
<array name="loadresource_droplists" />
<array name="loadresource_itemcategories" />
<array name="loadresource_itemfilters" />
</resources>""",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "missing"):
                build_game_data(source, Path(directory) / "project")


if __name__ == "__main__":
    unittest.main()