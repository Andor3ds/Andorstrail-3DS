#include "game_resources.h"

#include "game_resource_index.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static bool inflate_resource(
    GameResources *resources,
    const GameResourceDocumentEntry *entry,
    GameJsonDocument *document
)
{
    if (resources == NULL || resources->pack == NULL || entry == NULL || document == NULL
        || entry->source_size == UINT32_MAX) return false;
    const long offset = (long)entry->offset;
    if (offset < 0 || (uint32_t)offset != entry->offset
        || fseek(resources->pack, offset, SEEK_SET) != 0) return false;

    unsigned char *compressed = malloc(entry->compressed_size != 0
        ? entry->compressed_size : 1);
    char *source = malloc((size_t)entry->source_size + 1);
    if (compressed == NULL || source == NULL) {
        free(compressed);
        free(source);
        return false;
    }

    const size_t read_size = fread(compressed, 1, entry->compressed_size, resources->pack);
    if (read_size != entry->compressed_size) {
        free(compressed);
        free(source);
        return false;
    }

    uLongf source_size = entry->source_size;
    const int status = uncompress(
        (Bytef *)source, &source_size, compressed, entry->compressed_size
    );
    free(compressed);
    if (status != Z_OK || source_size != entry->source_size) {
        free(source);
        return false;
    }
    return game_json_parse_owned(source, entry->source_size, document);
}

bool game_resources_init(GameResources *resources, const char *pack_path)
{
    if (resources == NULL || pack_path == NULL) return false;
    memset(resources, 0, sizeof(*resources));

    FILE *pack = fopen(pack_path, "rb");
    if (pack == NULL) return false;
    if (fseek(pack, 0, SEEK_END) != 0) {
        fclose(pack);
        return false;
    }

    const long pack_size = ftell(pack);
    uint64_t required_size = 0;
    for (size_t index = 0; index < game_resource_document_count; ++index) {
        const GameResourceDocumentEntry *entry = &game_resource_documents[index];
        const uint64_t end = (uint64_t)entry->offset + entry->compressed_size;
        if (end > required_size) required_size = end;
    }
    if (pack_size < 0 || (uint64_t)pack_size < required_size || fseek(pack, 0, SEEK_SET) != 0) {
        fclose(pack);
        return false;
    }

    resources->pack = pack;
    return true;
}

void game_resources_dispose(GameResources *resources)
{
    if (resources == NULL) return;
    if (resources->pack != NULL) fclose(resources->pack);
    resources->pack = NULL;
}

static int compare_record_key(
    const GameResourceRecordEntry *record,
    GameResourceType type,
    const char *id
)
{
    if (record->type != type) return record->type < type ? -1 : 1;
    return strcmp(record->id, id);
}

const GameResourceRecordEntry *game_resources_find(
    GameResourceType type,
    const char *id,
    size_t *matching_record_count
)
{
    if (matching_record_count != NULL) *matching_record_count = 0;
    if (id == NULL) return NULL;

    size_t low = 0;
    size_t high = game_resource_record_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (compare_record_key(&game_resource_records[middle], type, id) < 0) low = middle + 1;
        else high = middle;
    }
    if (low >= game_resource_record_count
            || compare_record_key(&game_resource_records[low], type, id) != 0) return NULL;

    size_t end = low + 1;
    while (end < game_resource_record_count
            && compare_record_key(&game_resource_records[end], type, id) == 0) {
        ++end;
    }
    if (matching_record_count != NULL) *matching_record_count = end - low;
    return &game_resource_records[low];
}

bool game_resources_open_document(
    GameResources *resources,
    size_t document_index,
    GameJsonDocument *document
)
{
    if (document_index >= game_resource_document_count) return false;
    return inflate_resource(resources, &game_resource_documents[document_index], document);
}

bool game_resources_open_record(
    GameResources *resources,
    const GameResourceRecordEntry *record,
    GameJsonDocument *document,
    GameJsonRef *value
)
{
    if (value != NULL) {
        value->document = NULL;
        value->node = GAME_JSON_NO_NODE;
    }
    if (record == NULL || document == NULL || value == NULL || record->id == NULL
            || !game_resources_open_document(resources, record->document_index, document)) {
        return false;
    }

    const GameJsonRef root = game_json_root(document);
    *value = game_json_array_get(root, record->record_index);
    if (!game_json_string_equals(game_json_object_get(*value, "id"), record->id)) {
        game_json_dispose(document);
        value->document = NULL;
        value->node = GAME_JSON_NO_NODE;
        return false;
    }
    return true;
}