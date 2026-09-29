#!/usr/bin/env python3
"""Build the indexed, compressed resource pack from original Android raw JSON."""

from __future__ import annotations

import argparse
import json
import zlib
from dataclasses import dataclass
from pathlib import Path


SOURCE_COMMIT = "0dfa1f63ea6d944df2c6d34e641000c09d3e917e"
FAMILIES = (
    ("itemlist_", "GAME_RESOURCE_ITEM"),
    ("monsterlist_", "GAME_RESOURCE_MONSTER"),
    ("questlist", "GAME_RESOURCE_QUEST"),
    ("conversationlist_", "GAME_RESOURCE_CONVERSATION"),
    ("actorconditions_", "GAME_RESOURCE_CONDITION"),
    ("droplists_", "GAME_RESOURCE_DROP_LIST"),
    ("itemcategories_", "GAME_RESOURCE_ITEM_CATEGORY"),
    ("itemfilters_", "GAME_RESOURCE_ITEM_FILTER"),
)
TYPE_ORDER = {enum_name: index for index, (_, enum_name) in enumerate(FAMILIES)}


@dataclass(frozen=True)
class PackedDocument:
    name: str
    resource_type: str
    offset: int
    compressed_size: int
    source_size: int
    compressed: bytes
    records: tuple[tuple[str, int], ...]


def resource_type(filename: str) -> str:
    for prefix, enum_name in FAMILIES:
        if filename.startswith(prefix):
            return enum_name
    raise ValueError(f"unrecognized Andor's Trail raw resource family: {filename}")


def c_string(value: str) -> str:
    try:
        value.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError(f"resource index identifiers must be ASCII: {value!r}") from error
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def collect_documents(raw_directory: Path) -> tuple[list[PackedDocument], bytes]:
    if not raw_directory.is_dir():
        raise ValueError(f"raw resource directory does not exist: {raw_directory}")
    documents = []
    pack = bytearray()
    for source_path in sorted(raw_directory.glob("*.json")):
        kind = resource_type(source_path.name)
        source = source_path.read_bytes()
        payload = json.loads(source)
        if not isinstance(payload, list):
            raise ValueError(f"expected a JSON list in {source_path}")
        records = []
        for record_index, record in enumerate(payload):
            if not isinstance(record, dict) or not isinstance(record.get("id"), str):
                raise ValueError(f"record {record_index} in {source_path} has no string id")
            records.append((record["id"], record_index))
        compressed = zlib.compress(source, level=9)
        documents.append(PackedDocument(
            source_path.name,
            kind,
            len(pack),
            len(compressed),
            len(source),
            compressed,
            tuple(records),
        ))
        pack.extend(compressed)
    if not documents:
        raise ValueError(f"no raw JSON resources found in {raw_directory}")
    return documents, bytes(pack)


def write_index(output_directory: Path, documents: list[PackedDocument]) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    document_rows = [
        "    {%s, %s, %d, %d, %d}," % (
            c_string(document.name), document.resource_type, document.offset,
            document.compressed_size, document.source_size
        )
        for document in documents
    ]
    records = sorted(
        [
            (document.resource_type, identifier, document_index, record_index)
            for document_index, document in enumerate(documents)
            for identifier, record_index in document.records
        ],
        key=lambda record: (TYPE_ORDER[record[0]], record[1], record[2], record[3]),
    )
    record_rows = [
        "    {%s, %d, %d, %s}," % (
            c_string(identifier), document_index, record_index, kind
        )
        for kind, identifier, document_index, record_index in records
    ]
    header = "\n".join((
        "#ifndef ANDORSTRAIL_GAME_RESOURCE_INDEX_H",
        "#define ANDORSTRAIL_GAME_RESOURCE_INDEX_H",
        f'#define GAME_RESOURCE_SOURCE_COMMIT "{SOURCE_COMMIT}"',
        '#include "game_resources.h"',
        "extern const GameResourceDocumentEntry game_resource_documents[];",
        "extern const size_t game_resource_document_count;",
        "extern const GameResourceRecordEntry game_resource_records[];",
        "extern const size_t game_resource_record_count;",
        "#endif",
        "",
    ))
    source = "\n".join((
        '#include "game_resource_index.h"',
        "const GameResourceDocumentEntry game_resource_documents[] = {",
        *document_rows,
        "};",
        "const size_t game_resource_document_count =",
        "    sizeof(game_resource_documents) / sizeof(game_resource_documents[0]);",
        "const GameResourceRecordEntry game_resource_records[] = {",
        *record_rows,
        "};",
        "const size_t game_resource_record_count =",
        "    sizeof(game_resource_records) / sizeof(game_resource_records[0]);",
        "",
    ))
    (output_directory / "game_resource_index.h").write_text(header, encoding="ascii")
    (output_directory / "game_resource_index.c").write_text(source, encoding="ascii")


def build_game_data(source_root: Path, project_root: Path) -> tuple[int, int]:
    documents, pack = collect_documents(source_root / "res" / "raw")
    (project_root / "romfs").mkdir(parents=True, exist_ok=True)
    (project_root / "romfs" / "game_data.pack").write_bytes(pack)
    write_index(project_root / "source" / "core", documents)
    return len(documents), len(pack)


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
    count, packed_size = build_game_data(arguments.source, arguments.project)
    print(f"Wrote {count} JSON resources ({packed_size} compressed bytes)")


if __name__ == "__main__":
    main()