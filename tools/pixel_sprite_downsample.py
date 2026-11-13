"""Build compact, pixel-art-aware actor sprite atlases."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ATLAS_COLUMNS = 8
TILE_SIZE = 32


def _read_rgba(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"not a PNG image: {path}")

    width = height = bit_depth = color_type = -1
    palette = b""
    transparency = b""
    compressed = bytearray()
    offset = len(PNG_SIGNATURE)
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        offset += length + 12
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", chunk)
            )
            if compression or filtering or interlace:
                raise ValueError(f"unsupported PNG encoding: {path}")
        elif kind == b"PLTE":
            palette = chunk
        elif kind == b"tRNS":
            transparency = chunk
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break

    channels_by_type = {2: 3, 3: 1, 4: 2, 6: 4}
    channels = channels_by_type.get(color_type)
    if width <= 0 or height <= 0 or bit_depth != 8 or channels is None:
        raise ValueError(f"unsupported PNG color format: {path}")
    if color_type == 3 and (not palette or len(palette) % 3):
        raise ValueError(f"indexed PNG has no valid palette: {path}")

    stride = width * channels
    raw = zlib.decompress(compressed)
    if len(raw) != height * (stride + 1):
        raise ValueError(f"PNG pixel data has an invalid size: {path}")
    decoded = bytearray(height * stride)
    source_offset = 0
    for y in range(height):
        filter_type = raw[source_offset]
        source_offset += 1
        row_offset = y * stride
        previous_offset = row_offset - stride
        for x in range(stride):
            value = raw[source_offset + x]
            left = decoded[row_offset + x - channels] if x >= channels else 0
            above = decoded[previous_offset + x] if y else 0
            upper_left = (
                decoded[previous_offset + x - channels]
                if y and x >= channels else 0
            )
            if filter_type == 1:
                value += left
            elif filter_type == 2:
                value += above
            elif filter_type == 3:
                value += (left + above) // 2
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (
                    abs(estimate - left),
                    abs(estimate - above),
                    abs(estimate - upper_left),
                )
                value += (
                    left if distances[0] <= distances[1]
                    and distances[0] <= distances[2]
                    else above if distances[1] <= distances[2]
                    else upper_left
                )
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}: {path}")
            decoded[row_offset + x] = value & 0xFF
        source_offset += stride

    rgba = bytearray(width * height * 4)
    for pixel in range(width * height):
        source = pixel * channels
        target = pixel * 4
        if color_type == 6:
            rgba[target:target + 4] = decoded[source:source + 4]
        elif color_type == 2:
            rgba[target:target + 3] = decoded[source:source + 3]
            rgba[target + 3] = 255
        elif color_type == 4:
            rgba[target:target + 3] = bytes((decoded[source],) * 3)
            rgba[target + 3] = decoded[source + 1]
        else:
            palette_index = decoded[source]
            palette_offset = palette_index * 3
            if palette_offset + 3 > len(palette):
                raise ValueError(f"PNG palette index is out of range: {path}")
            rgba[target:target + 3] = palette[palette_offset:palette_offset + 3]
            rgba[target + 3] = (
                transparency[palette_index]
                if palette_index < len(transparency) else 255
            )
    return width, height, bytes(rgba)


def _write_rgba(path: Path, width: int, height: int, pixels: bytes) -> None:
    def chunk(name: bytes, content: bytes) -> bytes:
        body = name + content
        return struct.pack(">I", len(content)) + body + struct.pack(
            ">I", zlib.crc32(body) & 0xFFFFFFFF
        )

    stride = width * 4
    scanlines = b"".join(
        b"\0" + pixels[y * stride:(y + 1) * stride]
        for y in range(height)
    )
    output = PNG_SIGNATURE
    output += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    output += chunk(b"IDAT", zlib.compress(scanlines, 9))
    output += chunk(b"IEND", b"")
    path.write_bytes(output)


def _reduce_icon(
    source: bytes,
    source_width: int,
    tile_size: int,
) -> bytes:
    def pixel_at(x: int, y: int) -> bytes:
        offset = (y * source_width + x) * 4
        return source[offset:offset + 4]

    def outline_weight(x: int, y: int, color: bytes) -> float:
        transparent_neighbors = 0
        brighter_neighbors = 0
        luminance = (
            299 * color[0] + 587 * color[1] + 114 * color[2]
        ) // 1000
        for neighbor_y in range(max(0, y - 1), min(TILE_SIZE, y + 2)):
            for neighbor_x in range(max(0, x - 1), min(TILE_SIZE, x + 2)):
                if neighbor_x == x and neighbor_y == y:
                    continue
                neighbor = pixel_at(neighbor_x, neighbor_y)
                if neighbor[3] < 128:
                    transparent_neighbors += 1
                    continue
                neighbor_luminance = (
                    299 * neighbor[0] + 587 * neighbor[1] + 114 * neighbor[2]
                ) // 1000
                if neighbor_luminance >= luminance + 40:
                    brighter_neighbors += 1
        transparent_neighbors += 8 - (
            min(TILE_SIZE, y + 2) - max(0, y - 1)
        ) * (
            min(TILE_SIZE, x + 2) - max(0, x - 1)
        ) + 1
        return 1.0 + transparent_neighbors * 0.5 + min(brighter_neighbors, 2) * 0.75

    output = bytearray(tile_size * tile_size * 4)
    for y in range(tile_size):
        top = y * TILE_SIZE // tile_size
        bottom = (y + 1) * TILE_SIZE // tile_size
        for x in range(tile_size):
            left = x * TILE_SIZE // tile_size
            right = (x + 1) * TILE_SIZE // tile_size
            candidates: dict[bytes, tuple[float, float]] = {}
            solid_candidates: dict[bytes, tuple[float, float]] = {}
            for source_y in range(top, bottom):
                for source_x in range(left, right):
                    color = pixel_at(source_x, source_y)
                    if color[3] < 128:
                        continue
                    target_candidates = (
                        solid_candidates if color[3] >= 250 else candidates
                    )
                    previous = target_candidates.get(
                        color, (0.0, float("inf"))
                    )
                    weight = outline_weight(source_x, source_y, color)
                    center_x = (left + right - 1) / 2
                    center_y = (top + bottom - 1) / 2
                    distance = (
                        (source_x - center_x) ** 2
                        + (source_y - center_y) ** 2
                    )
                    target_candidates[color] = (
                        previous[0] + weight,
                        min(previous[1], distance),
                    )
            if solid_candidates:
                candidates = solid_candidates
            if candidates:
                color = max(
                    candidates,
                    key=lambda candidate: (
                        candidates[candidate][0],
                        -candidates[candidate][1],
                        candidate,
                    ),
                )
            else:
                color = pixel_at((left + right - 1) // 2, (top + bottom - 1) // 2)
            target = (y * tile_size + x) * 4
            output[target:target + 4] = color
    return bytes(output)


def build_actor_atlases(
    sprite_directory: Path,
    icons: tuple[tuple[str, int], ...],
    atlas_directory: Path,
    manifest_path: Path,
) -> tuple[int, int]:
    """Create one small atlas per zoom and a stable source-icon-to-slot map."""
    if not icons:
        raise ValueError("actor atlas requires at least one sprite")
    decoded_sheets: dict[str, tuple[int, int, bytes]] = {}
    for sheet_name in dict.fromkeys(name for name, _ in icons):
        width, height, pixels = _read_rgba(sprite_directory / sheet_name)
        if width % TILE_SIZE or height % TILE_SIZE:
            raise ValueError(f"sprite sheet dimensions are not aligned to 32px tiles: {sheet_name}")
        decoded_sheets[sheet_name] = (width, height, pixels)

    unique_icons = tuple(dict.fromkeys(icons))
    for sheet_name, index in unique_icons:
        width, height, _ = decoded_sheets[sheet_name]
        if index < 0 or index >= (width // TILE_SIZE) * (height // TILE_SIZE):
            raise ValueError(f"sprite index is outside {sheet_name}: {index}")

    atlas_directory.mkdir(parents=True, exist_ok=True)
    columns = ATLAS_COLUMNS
    rows = (len(unique_icons) + columns - 1) // columns
    total_bytes = 0
    for tile_size, suffix in ((16, "050"), (32, "075"), (32, "100")):
        width = 1 << (columns * tile_size - 1).bit_length()
        height = 1 << (rows * tile_size - 1).bit_length()
        atlas = bytearray(width * height * 4)
        for slot, (sheet_name, index) in enumerate(unique_icons):
            sheet_width, _, pixels = decoded_sheets[sheet_name]
            sheet_columns = sheet_width // TILE_SIZE
            tile_x = index % sheet_columns
            tile_y = index // sheet_columns
            icon_pixels = b"".join(
                pixels[(tile_y * TILE_SIZE + y) * sheet_width * 4
                    + tile_x * TILE_SIZE * 4:
                    (tile_y * TILE_SIZE + y) * sheet_width * 4
                    + (tile_x + 1) * TILE_SIZE * 4]
                for y in range(TILE_SIZE)
            )
            icon = icon_pixels if suffix == "075" else _reduce_icon(
                icon_pixels, TILE_SIZE, tile_size
            )
            target_x = (slot % columns) * tile_size
            target_y = (slot // columns) * tile_size
            for y in range(tile_size):
                source_start = y * tile_size * 4
                target_start = ((target_y + y) * width + target_x) * 4
                atlas[target_start:target_start + tile_size * 4] = (
                    icon[source_start:source_start + tile_size * 4]
                )
        output_path = atlas_directory / f"actor_atlas_{suffix}.png"
        _write_rgba(output_path, width, height, bytes(atlas))
        total_bytes += output_path.stat().st_size

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "".join(f"{sheet} {index} {slot}\n"
            for slot, (sheet, index) in enumerate(unique_icons)),
        encoding="ascii",
    )
    total_bytes += manifest_path.stat().st_size
    return len(unique_icons), total_bytes
