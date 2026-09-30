#ifndef ANDORSTRAIL_GAME_RESOURCES_H
#define ANDORSTRAIL_GAME_RESOURCES_H

#include "game_json.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    GAME_RESOURCE_ITEM,
    GAME_RESOURCE_MONSTER,
    GAME_RESOURCE_QUEST,
    GAME_RESOURCE_CONVERSATION,
    GAME_RESOURCE_CONDITION,
    GAME_RESOURCE_DROP_LIST,
    GAME_RESOURCE_ITEM_CATEGORY,
    GAME_RESOURCE_ITEM_FILTER
} GameResourceType;

typedef struct {
    const char *name;
    GameResourceType type;
    uint32_t offset;
    uint32_t compressed_size;
    uint32_t source_size;
} GameResourceDocumentEntry;

typedef struct {
    const char *id;
    uint16_t document_index;
    uint32_t record_index;
    GameResourceType type;
} GameResourceRecordEntry;

typedef struct {
    FILE *pack;
} GameResources;

#ifdef __cplusplus
extern "C" {
#endif

extern const GameResourceDocumentEntry game_resource_documents[];
extern const size_t game_resource_document_count;
extern const GameResourceRecordEntry game_resource_records[];
extern const size_t game_resource_record_count;

bool game_resources_init(GameResources *resources, const char *pack_path);
void game_resources_dispose(GameResources *resources);
const GameResourceRecordEntry *game_resources_find(
    GameResourceType type,
    const char *id,
    size_t *matching_record_count
);
bool game_resources_open_document(
    GameResources *resources,
    size_t document_index,
    GameJsonDocument *document
);
bool game_resources_open_record(
    GameResources *resources,
    const GameResourceRecordEntry *record,
    GameJsonDocument *document,
    GameJsonRef *value
);

#ifdef __cplusplus
}
#endif

#endif