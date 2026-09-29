#include "game_json.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GameJsonDocument *document;
    size_t position;
    unsigned int depth;
    bool failed;
} JsonParser;

static void skip_whitespace(JsonParser *parser)
{
    while (parser->position < parser->document->source_size
            && isspace((unsigned char)parser->document->source[parser->position])) {
        ++parser->position;
    }
}

static bool reserve_nodes(GameJsonDocument *document, size_t count)
{
    if (document->node_capacity >= count) return true;
    size_t capacity = document->node_capacity == 0 ? 64 : document->node_capacity;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) return false;
        capacity *= 2;
    }
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(*document->nodes)) return false;
    GameJsonNode *nodes = realloc(document->nodes, capacity * sizeof(*nodes));
    if (nodes == NULL) return false;
    document->nodes = nodes;
    document->node_capacity = capacity;
    return true;
}

static uint32_t add_node(JsonParser *parser, GameJsonType type, size_t start, size_t end)
{
    GameJsonDocument *document = parser->document;
    if (document->node_count >= UINT32_MAX || !reserve_nodes(document, document->node_count + 1)) {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }
    const uint32_t index = (uint32_t)document->node_count++;
    GameJsonNode *node = &document->nodes[index];
    node->start = (uint32_t)start;
    node->end = (uint32_t)end;
    node->first_child = GAME_JSON_NO_NODE;
    node->next_sibling = GAME_JSON_NO_NODE;
    node->child_count = 0;
    node->type = (uint8_t)type;
    return index;
}

static void append_child(GameJsonDocument *document, uint32_t parent, uint32_t child, uint32_t *last_child)
{
    GameJsonNode *parent_node = &document->nodes[parent];
    if (parent_node->first_child == GAME_JSON_NO_NODE) parent_node->first_child = child;
    if (*last_child != GAME_JSON_NO_NODE) document->nodes[*last_child].next_sibling = child;
    *last_child = child;
    ++parent_node->child_count;
}

static uint32_t parse_value(JsonParser *parser);

static bool is_value_delimiter(char character)
{
    return character == ',' || character == ']' || character == '}'
        || isspace((unsigned char)character);
}

static uint32_t parse_string(JsonParser *parser)
{
    const size_t quote_position = parser->position;
    ++parser->position;
    const size_t string_start = parser->position;
    while (parser->position < parser->document->source_size) {
        const unsigned char character = (unsigned char)parser->document->source[parser->position++];
        if (character == '"') {
            return add_node(parser, GAME_JSON_STRING, string_start, parser->position - 1);
        }
        if (character < 0x20) break;
        if (character != '\\') continue;
        if (parser->position >= parser->document->source_size) break;
        const char escape = parser->document->source[parser->position++];
        if (escape == 'u') {
            for (int digit = 0; digit < 4; ++digit) {
                if (parser->position >= parser->document->source_size
                        || !isxdigit((unsigned char)parser->document->source[parser->position++])) {
                    parser->failed = true;
                    return GAME_JSON_NO_NODE;
                }
            }
        } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b'
                && escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
    }
    parser->position = quote_position;
    parser->failed = true;
    return GAME_JSON_NO_NODE;
}

static uint32_t parse_array(JsonParser *parser)
{
    const size_t start = parser->position++;
    const uint32_t array = add_node(parser, GAME_JSON_ARRAY, start, start + 1);
    uint32_t last_child = GAME_JSON_NO_NODE;
    ++parser->depth;
    if (parser->depth > 128) parser->failed = true;
    skip_whitespace(parser);
    if (!parser->failed && parser->position < parser->document->source_size
            && parser->document->source[parser->position] == ']') {
        ++parser->position;
    } else {
        while (!parser->failed) {
            const uint32_t child = parse_value(parser);
            if (child == GAME_JSON_NO_NODE) break;
            append_child(parser->document, array, child, &last_child);
            skip_whitespace(parser);
            if (parser->position >= parser->document->source_size) break;
            const char separator = parser->document->source[parser->position++];
            if (separator == ']') break;
            if (separator != ',') parser->failed = true;
            skip_whitespace(parser);
        }
    }
    --parser->depth;
    if (parser->failed || parser->position > parser->document->source_size
            || parser->document->source[parser->position - 1] != ']') {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }
    parser->document->nodes[array].end = (uint32_t)parser->position;
    return array;
}

static uint32_t parse_object(JsonParser *parser)
{
    const size_t start = parser->position++;
    const uint32_t object = add_node(parser, GAME_JSON_OBJECT, start, start + 1);
    uint32_t last_child = GAME_JSON_NO_NODE;
    ++parser->depth;
    if (parser->depth > 128) parser->failed = true;
    skip_whitespace(parser);
    if (!parser->failed && parser->position < parser->document->source_size
            && parser->document->source[parser->position] == '}') {
        ++parser->position;
    } else {
        while (!parser->failed) {
            if (parser->position >= parser->document->source_size
                    || parser->document->source[parser->position] != '"') {
                parser->failed = true;
                break;
            }
            const uint32_t key = parse_string(parser);
            if (key == GAME_JSON_NO_NODE) break;
            append_child(parser->document, object, key, &last_child);
            skip_whitespace(parser);
            if (parser->position >= parser->document->source_size
                    || parser->document->source[parser->position++] != ':') {
                parser->failed = true;
                break;
            }
            skip_whitespace(parser);
            const uint32_t value = parse_value(parser);
            if (value == GAME_JSON_NO_NODE) break;
            append_child(parser->document, object, value, &last_child);
            skip_whitespace(parser);
            if (parser->position >= parser->document->source_size) break;
            const char separator = parser->document->source[parser->position++];
            if (separator == '}') break;
            if (separator != ',') parser->failed = true;
            skip_whitespace(parser);
        }
    }
    --parser->depth;
    if (parser->failed || parser->position > parser->document->source_size
            || parser->document->source[parser->position - 1] != '}') {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }
    parser->document->nodes[object].end = (uint32_t)parser->position;
    return object;
}

static uint32_t parse_value(JsonParser *parser)
{
    skip_whitespace(parser);
    if (parser->position >= parser->document->source_size) return GAME_JSON_NO_NODE;
    const size_t start = parser->position;
    const char character = parser->document->source[parser->position];
    if (character == '{') return parse_object(parser);
    if (character == '[') return parse_array(parser);
    if (character == '"') return parse_string(parser);
    if (character == 't' && parser->position + 4 <= parser->document->source_size
            && memcmp(parser->document->source + parser->position, "true", 4) == 0) {
        parser->position += 4;
        if (parser->position < parser->document->source_size
                && !is_value_delimiter(parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
        return add_node(parser, GAME_JSON_BOOLEAN, start, parser->position);
    }
    if (character == 'f' && parser->position + 5 <= parser->document->source_size
            && memcmp(parser->document->source + parser->position, "false", 5) == 0) {
        parser->position += 5;
        if (parser->position < parser->document->source_size
                && !is_value_delimiter(parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
        return add_node(parser, GAME_JSON_BOOLEAN, start, parser->position);
    }
    if (character == 'n' && parser->position + 4 <= parser->document->source_size
            && memcmp(parser->document->source + parser->position, "null", 4) == 0) {
        parser->position += 4;
        if (parser->position < parser->document->source_size
                && !is_value_delimiter(parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
        return add_node(parser, GAME_JSON_NULL, start, parser->position);
    }

    if (character == '-') ++parser->position;
    if (parser->position >= parser->document->source_size) {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }
    if (parser->document->source[parser->position] == '0') {
        ++parser->position;
        if (parser->position < parser->document->source_size
                && isdigit((unsigned char)parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
    } else if (parser->document->source[parser->position] >= '1'
            && parser->document->source[parser->position] <= '9') {
        do {
            ++parser->position;
        } while (parser->position < parser->document->source_size
                && isdigit((unsigned char)parser->document->source[parser->position]));
    } else {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }

    if (parser->position < parser->document->source_size
            && parser->document->source[parser->position] == '.') {
        ++parser->position;
        if (parser->position >= parser->document->source_size
                || !isdigit((unsigned char)parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
        do {
            ++parser->position;
        } while (parser->position < parser->document->source_size
                && isdigit((unsigned char)parser->document->source[parser->position]));
    }

    if (parser->position < parser->document->source_size
            && (parser->document->source[parser->position] == 'e'
                || parser->document->source[parser->position] == 'E')) {
        ++parser->position;
        if (parser->position < parser->document->source_size
                && (parser->document->source[parser->position] == '+'
                    || parser->document->source[parser->position] == '-')) {
            ++parser->position;
        }
        if (parser->position >= parser->document->source_size
                || !isdigit((unsigned char)parser->document->source[parser->position])) {
            parser->failed = true;
            return GAME_JSON_NO_NODE;
        }
        do {
            ++parser->position;
        } while (parser->position < parser->document->source_size
                && isdigit((unsigned char)parser->document->source[parser->position]));
    }

    if (parser->position < parser->document->source_size
            && !is_value_delimiter(parser->document->source[parser->position])) {
        parser->failed = true;
        return GAME_JSON_NO_NODE;
    }
    return add_node(parser, GAME_JSON_NUMBER, start, parser->position);
}

bool game_json_parse(const char *source, size_t source_size, GameJsonDocument *document)
{
    if (source == NULL || document == NULL || source_size > UINT32_MAX) return false;
    char *owned_source = malloc(source_size + 1);
    if (owned_source == NULL) return false;
    memcpy(owned_source, source, source_size);
    owned_source[source_size] = '\0';
    return game_json_parse_owned(owned_source, source_size, document);
}

bool game_json_parse_owned(char *source, size_t source_size, GameJsonDocument *document)
{
    if (source == NULL || document == NULL || source_size > UINT32_MAX) {
        free(source);
        return false;
    }
    memset(document, 0, sizeof(*document));
    document->root = GAME_JSON_NO_NODE;
    document->source = source;
    document->source[source_size] = '\0';
    document->source_size = source_size;
    JsonParser parser = {document, 0, 0, false};
    document->root = parse_value(&parser);
    skip_whitespace(&parser);
    if (parser.failed || document->root == GAME_JSON_NO_NODE || parser.position != source_size) {
        game_json_dispose(document);
        return false;
    }
    return true;
}

void game_json_dispose(GameJsonDocument *document)
{
    if (document == NULL) return;
    free(document->source);
    free(document->nodes);
    memset(document, 0, sizeof(*document));
    document->root = GAME_JSON_NO_NODE;
}

GameJsonRef game_json_root(const GameJsonDocument *document)
{
    GameJsonRef root = {document, document != NULL ? document->root : GAME_JSON_NO_NODE};
    return root;
}

static bool valid_ref(GameJsonRef value)
{
    return value.document != NULL && value.node < value.document->node_count;
}

GameJsonRef game_json_object_get(GameJsonRef object, const char *key)
{
    GameJsonRef missing = {object.document, GAME_JSON_NO_NODE};
    if (!valid_ref(object) || key == NULL
            || object.document->nodes[object.node].type != GAME_JSON_OBJECT) return missing;
    uint32_t child = object.document->nodes[object.node].first_child;
    while (child != GAME_JSON_NO_NODE) {
        const uint32_t key_node = child;
        const uint32_t value_node = object.document->nodes[key_node].next_sibling;
        if (value_node == GAME_JSON_NO_NODE) return missing;
        GameJsonRef key_ref = {object.document, key_node};
        if (game_json_string_equals(key_ref, key)) {
            GameJsonRef result = {object.document, value_node};
            return result;
        }
        child = object.document->nodes[value_node].next_sibling;
    }
    return missing;
}

GameJsonRef game_json_array_get(GameJsonRef array, size_t index)
{
    GameJsonRef missing = {array.document, GAME_JSON_NO_NODE};
    if (!valid_ref(array) || array.document->nodes[array.node].type != GAME_JSON_ARRAY) return missing;
    uint32_t child = array.document->nodes[array.node].first_child;
    while (child != GAME_JSON_NO_NODE && index > 0) {
        child = array.document->nodes[child].next_sibling;
        --index;
    }
    GameJsonRef result = {array.document, child};
    return result;
}

size_t game_json_array_size(GameJsonRef array)
{
    if (!valid_ref(array) || array.document->nodes[array.node].type != GAME_JSON_ARRAY) return 0;
    return array.document->nodes[array.node].child_count;
}

static bool append_utf8(char *destination, size_t capacity, size_t *length, uint32_t codepoint)
{
    unsigned char bytes[4];
    size_t count;
    if (codepoint <= 0x7f) {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7ff) {
        bytes[0] = 0xc0 | (unsigned char)(codepoint >> 6);
        bytes[1] = 0x80 | (unsigned char)(codepoint & 0x3f);
        count = 2;
    } else if (codepoint <= 0xffff) {
        bytes[0] = 0xe0 | (unsigned char)(codepoint >> 12);
        bytes[1] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3f);
        bytes[2] = 0x80 | (unsigned char)(codepoint & 0x3f);
        count = 3;
    } else if (codepoint <= 0x10ffff) {
        bytes[0] = 0xf0 | (unsigned char)(codepoint >> 18);
        bytes[1] = 0x80 | (unsigned char)((codepoint >> 12) & 0x3f);
        bytes[2] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3f);
        bytes[3] = 0x80 | (unsigned char)(codepoint & 0x3f);
        count = 4;
    } else {
        return false;
    }
    if (*length + count >= capacity) return false;
    memcpy(destination + *length, bytes, count);
    *length += count;
    return true;
}

static bool read_hex_quad(const char *source, uint32_t *result)
{
    uint32_t value = 0;
    for (int digit = 0; digit < 4; ++digit) {
        const unsigned char character = (unsigned char)source[digit];
        value <<= 4;
        if (character >= '0' && character <= '9') value |= character - '0';
        else if (character >= 'a' && character <= 'f') value |= character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') value |= character - 'A' + 10;
        else return false;
    }
    *result = value;
    return true;
}

bool game_json_string_copy(GameJsonRef value, char *destination, size_t capacity)
{
    if (!valid_ref(value) || destination == NULL || capacity == 0
            || value.document->nodes[value.node].type != GAME_JSON_STRING) return false;
    const GameJsonNode *node = &value.document->nodes[value.node];
    size_t output_length = 0;
    for (size_t position = node->start; position < node->end; ++position) {
        unsigned char character = (unsigned char)value.document->source[position];
        if (character != '\\') {
            if (output_length + 1 >= capacity) return false;
            destination[output_length++] = (char)character;
            continue;
        }
        if (++position >= node->end) return false;
        character = (unsigned char)value.document->source[position];
        switch (character) {
        case '"': character = '"'; break;
        case '\\': character = '\\'; break;
        case '/': character = '/'; break;
        case 'b': character = '\b'; break;
        case 'f': character = '\f'; break;
        case 'n': character = '\n'; break;
        case 'r': character = '\r'; break;
        case 't': character = '\t'; break;
        case 'u': {
            if (position + 4 >= node->end) return false;
            uint32_t codepoint;
            if (!read_hex_quad(value.document->source + position + 1, &codepoint)) return false;
            position += 4;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (position + 6 >= node->end || value.document->source[position + 1] != '\\'
                        || value.document->source[position + 2] != 'u') return false;
                uint32_t low_surrogate;
                if (!read_hex_quad(value.document->source + position + 3, &low_surrogate)
                        || low_surrogate < 0xdc00 || low_surrogate > 0xdfff) return false;
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low_surrogate - 0xdc00);
                position += 6;
            }
            if (!append_utf8(destination, capacity, &output_length, codepoint)) return false;
            continue;
        }
        default: return false;
        }
        if (output_length + 1 >= capacity) return false;
        destination[output_length++] = (char)character;
    }
    destination[output_length] = '\0';
    return true;
}

bool game_json_string_equals(GameJsonRef value, const char *text)
{
    if (!valid_ref(value) || text == NULL
            || value.document->nodes[value.node].type != GAME_JSON_STRING) return false;
    const GameJsonNode *node = &value.document->nodes[value.node];
    size_t text_position = 0;
    for (size_t position = node->start; position < node->end; ++position) {
        unsigned char character = (unsigned char)value.document->source[position];
        if (character == '\\') {
            if (++position >= node->end) return false;
            character = (unsigned char)value.document->source[position];
            if (character == 'u') return false;
            switch (character) {
            case '"': case '\\': case '/': break;
            case 'b': character = '\b'; break;
            case 'f': character = '\f'; break;
            case 'n': character = '\n'; break;
            case 'r': character = '\r'; break;
            case 't': character = '\t'; break;
            default: return false;
            }
        }
        if (text[text_position] == '\0' || (unsigned char)text[text_position++] != character) return false;
    }
    return text[text_position] == '\0';
}

static bool numeric_text(GameJsonRef value, char *buffer, size_t capacity)
{
    if (!valid_ref(value) || value.document->nodes[value.node].type != GAME_JSON_NUMBER) return false;
    const GameJsonNode *node = &value.document->nodes[value.node];
    const size_t length = node->end - node->start;
    if (length == 0 || length >= capacity) return false;
    memcpy(buffer, value.document->source + node->start, length);
    buffer[length] = '\0';
    return true;
}

bool game_json_integer(GameJsonRef value, int64_t *result)
{
    char buffer[64];
    if (result == NULL || !numeric_text(value, buffer, sizeof(buffer))) return false;
    errno = 0;
    char *end = NULL;
    const long long number = strtoll(buffer, &end, 10);
    if (errno != 0 || end == buffer || *end != '\0') return false;
    *result = (int64_t)number;
    return true;
}

bool game_json_number(GameJsonRef value, double *result)
{
    char buffer[64];
    if (result == NULL || !numeric_text(value, buffer, sizeof(buffer))) return false;
    errno = 0;
    char *end = NULL;
    const double number = strtod(buffer, &end);
    if (errno != 0 || end == buffer || *end != '\0') return false;
    *result = number;
    return true;
}

bool game_json_boolean(GameJsonRef value, bool *result)
{
    if (!valid_ref(value) || result == NULL
            || value.document->nodes[value.node].type != GAME_JSON_BOOLEAN) return false;
    const GameJsonNode *node = &value.document->nodes[value.node];
    *result = value.document->source[node->start] == 't';
    return true;
}

bool game_json_is_null(GameJsonRef value)
{
    return valid_ref(value) && value.document->nodes[value.node].type == GAME_JSON_NULL;
}