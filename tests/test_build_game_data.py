import tempfile
import unittest
import zlib
from pathlib import Path

from tools.build_game_data import build_game_data


class GameDataBuilderTests(unittest.TestCase):
    def test_builds_resource_pack_and_sorted_index(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "android" / "res" / "raw"
            raw.mkdir(parents=True)
            (raw / "itemlist_fixture.json").write_text(
                '[{"id":"z_item"},{"id":"a_item"}]', encoding="utf-8"
            )
            (raw / "questlist_fixture.json").write_text(
                '[{"id":"quest","stages":[]}]', encoding="utf-8"
            )
            project = root / "project"

            count, packed_size = build_game_data(root / "android", project)

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
            first_document = zlib.decompressobj().decompress(pack)
            self.assertIn(b'"z_item"', first_document)
            self.assertIn("GAME_RESOURCE_SOURCE_COMMIT", header)

    def test_rejects_unrecognized_source_files(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "android" / "res" / "raw"
            raw.mkdir(parents=True)
            (raw / "unexpected.json").write_text("[]", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unrecognized"):
                build_game_data(raw.parents[1], Path(directory) / "project")


if __name__ == "__main__":
    unittest.main()