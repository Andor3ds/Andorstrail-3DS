#ifndef ANDOR_GAME_JSON_H
#define ANDOR_GAME_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_JSON_NO_NODE UINT32_MAX

typedef enum {
    GAME_JSON_NULL,
    GAME_JSON_BOOLEAN,
    GAME_JSON_NUMBER,
    GAME_JSON_STRING,
    GAME_JSON_ARRAY,
    GAME_JSON_OBJECT
} GameJsonType;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t first_child;
    uint32_t next_sibling;
    uint32_t child_count;
    uint8_t type;
} GameJsonNode;

typedef struct {
    char *source;
    size_t source_size;
    GameJsonNode *nodes;
    size_t node_count;
    size_t node_capacity;
    uint32_t root;
} GameJsonDocument;

typedef struct {
    const GameJsonDocument *document;
    uint32_t node;
} GameJsonRef;

#ifdef __cplusplus
extern "C" {
#endif

bool game_json_parse(const char *source, size_t source_size, GameJsonDocument *document);
bool game_json_parse_owned(char *source, size_t source_size, GameJsonDocument *document);
void game_json_dispose(GameJsonDocument *document);
GameJsonRef game_json_root(const GameJsonDocument *document);
GameJsonRef game_json_object_get(GameJsonRef object, const char *key);
GameJsonRef game_json_array_get(GameJsonRef array, size_t index);
size_t game_json_array_size(GameJsonRef array);
bool game_json_string_copy(GameJsonRef value, char *destination, size_t capacity);
bool game_json_string_equals(GameJsonRef value, const char *text);
bool game_json_integer(GameJsonRef value, int64_t *result);
bool game_json_number(GameJsonRef value, double *result);
bool game_json_boolean(GameJsonRef value, bool *result);
bool game_json_is_null(GameJsonRef value);

#ifdef __cplusplus
}
#endif

#endif