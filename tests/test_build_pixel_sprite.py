import tempfile
import unittest
from pathlib import Path

from tools.pixel_sprite_downsample import (
    _read_rgba,
    _reduce_icon,
    _write_rgba,
    build_actor_atlases,
)


class PixelSpriteDownsampleTests(unittest.TestCase):
    def test_downsampling_preserves_dark_outline_over_transparency(self):
        source = bytearray(32 * 32 * 4)
        source[(0 * 32 + 1) * 4:(0 * 32 + 1) * 4 + 4] = bytes(
            (12, 12, 12, 255)
        )
        source[(1 * 32 + 0) * 4:(1 * 32 + 0) * 4 + 4] = bytes(
            (220, 80, 30, 255)
        )

        reduced = _reduce_icon(bytes(source), 32, 16)

        self.assertEqual(reduced[:4], bytes((12, 12, 12, 255)))

    def test_downsampling_leaves_fully_transparent_regions_transparent(self):
        source = bytes(32 * 32 * 4)

        reduced = _reduce_icon(source, 32, 16)

        self.assertEqual(reduced, bytes(16 * 16 * 4))

    def test_downsampling_prefers_solid_pixels_over_antialiased_fringe(self):
        source = bytearray(32 * 32 * 4)
        source[:4] = bytes((12, 12, 12, 160))
        source[(1 * 32 + 1) * 4:(1 * 32 + 1) * 4 + 4] = bytes(
            (220, 80, 30, 255)
        )

        reduced = _reduce_icon(bytes(source), 32, 16)

        self.assertEqual(reduced[:4], bytes((220, 80, 30, 255)))

    def test_075_atlas_keeps_original_32px_actor_pixels(self):
        source = bytearray(32 * 32 * 4)
        source[(3 * 32 + 2) * 4:(3 * 32 + 2) * 4 + 4] = bytes(
            (220, 80, 30, 255)
        )
        source[(3 * 32 + 3) * 4:(3 * 32 + 3) * 4 + 4] = bytes(
            (12, 12, 12, 96)
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _write_rgba(root / "actor.png", 32, 32, bytes(source))
            build_actor_atlases(
                root,
                (("actor.png", 0),),
                root / "atlases",
                root / "actor_icons.list",
            )

            width, height, atlas = _read_rgba(
                root / "atlases" / "actor_atlas_075.png"
            )

        self.assertEqual((width, height), (256, 32))
        for y in range(32):
            source_row = source[y * 32 * 4:(y + 1) * 32 * 4]
            atlas_row = atlas[y * width * 4:y * width * 4 + 32 * 4]
            self.assertEqual(atlas_row, source_row)


if __name__ == "__main__":
    unittest.main()
