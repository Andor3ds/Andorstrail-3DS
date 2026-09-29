#include "game_json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char json[] =
        "{\"id\":\"phrase\\n1\",\"number\":-42,\"enabled\":true,"
        "\"text\":\"snowman: \\u2603, face: \\ud83d\\ude42\",\"items\":[1,2,3],\"nothing\":null}";
    GameJsonDocument document;
    assert(game_json_parse(json, sizeof(json) - 1, &document));
    GameJsonRef root = game_json_root(&document);
    char text[64];
    assert(game_json_string_copy(game_json_object_get(root, "id"), text, sizeof(text)));
    assert(strcmp(text, "phrase\n1") == 0);
    int64_t integer = 0;
    assert(game_json_integer(game_json_object_get(root, "number"), &integer) && integer == -42);
    bool enabled = false;
    assert(game_json_boolean(game_json_object_get(root, "enabled"), &enabled) && enabled);
    assert(game_json_string_copy(game_json_object_get(root, "text"), text, sizeof(text)));
    assert(strcmp(text, "snowman: \xe2\x98\x83, face: \xf0\x9f\x99\x82") == 0);
    assert(game_json_array_size(game_json_object_get(root, "items")) == 3);
    assert(game_json_is_null(game_json_object_get(root, "nothing")));
    game_json_dispose(&document);
    assert(!game_json_parse("{\"broken\":]", 10, &document));
    assert(!game_json_parse("not-json", 8, &document));
    assert(!game_json_parse("01", 2, &document));
    assert(!game_json_parse("1.", 2, &document));
    assert(!game_json_parse("1e+", 3, &document));
    puts("game JSON tests passed");
    return 0;
}