#include "game_resource_index.h"
#include "game_resources.h"

#include <assert.h>
#include <stdio.h>

static GameJsonRef open_record(
    GameResources *resources,
    GameResourceType type,
    const char *id,
    GameJsonDocument *document
)
{
    size_t matches = 0;
    const GameResourceRecordEntry *record = game_resources_find(type, id, &matches);
    assert(record != NULL && matches > 0);
    GameJsonRef value = {NULL, GAME_JSON_NO_NODE};
    assert(game_resources_open_record(resources, record, document, &value));
    return value;
}

static void test_entire_index(GameResources *resources)
{
    for (size_t document_index = 0; document_index < game_resource_document_count;
            ++document_index) {
        GameJsonDocument document;
        assert(game_resources_open_document(resources, document_index, &document));
        const GameJsonRef records = game_json_root(&document);
        const size_t record_count = game_json_array_size(records);

        for (size_t record_index = 0; record_index < record_count; ++record_index) {
            char id[256];
            const GameJsonRef value = game_json_array_get(records, record_index);
            assert(game_json_string_copy(
                game_json_object_get(value, "id"), id, sizeof(id)
            ));

            size_t matching_records = 0;
            const GameResourceRecordEntry *first = game_resources_find(
                game_resource_documents[document_index].type, id, &matching_records
            );
            assert(first != NULL && matching_records > 0);

            bool indexed_record_found = false;
            for (size_t match = 0; match < matching_records; ++match) {
                const GameResourceRecordEntry *indexed = first + match;
                if (indexed->document_index == document_index
                        && indexed->record_index == record_index) {
                    indexed_record_found = true;
                    break;
                }
            }
            assert(indexed_record_found);
        }

        game_json_dispose(&document);
    }
}

static void test_original_records(GameResources *resources)
{
    GameJsonDocument document;
    GameJsonRef item = open_record(resources, GAME_RESOURCE_ITEM, "club1", &document);
    assert(game_json_string_equals(game_json_object_get(item, "name"), "Wooden club"));
    assert(game_json_string_equals(game_json_object_get(item, "category"), "club"));
    int64_t value = 0;
    assert(game_json_integer(game_json_object_get(item, "baseMarketCost"), &value));
    assert(value == 7);
    game_json_dispose(&document);

    GameJsonRef monster = open_record(resources, GAME_RESOURCE_MONSTER, "mikhail", &document);
    assert(game_json_string_equals(game_json_object_get(monster, "name"), "Mikhail"));
    assert(game_json_string_equals(game_json_object_get(monster, "spawnGroup"), "mikhail"));
    game_json_dispose(&document);

    GameJsonRef quest = open_record(resources, GAME_RESOURCE_QUEST, "andor", &document);
    assert(game_json_string_equals(game_json_object_get(quest, "name"), "Search for Andor"));
    assert(game_json_array_size(game_json_object_get(quest, "stages")) > 1);
    game_json_dispose(&document);

    GameJsonRef conversation = open_record(
        resources, GAME_RESOURCE_CONVERSATION, "agthor", &document
    );
    assert(game_json_array_size(game_json_object_get(conversation, "replies")) > 0);
    game_json_dispose(&document);

    GameJsonRef condition = open_record(resources, GAME_RESOURCE_CONDITION, "crushed", &document);
    assert(game_json_string_equals(game_json_object_get(condition, "name"), "Crushed"));
    game_json_dispose(&document);

    GameJsonRef drop_list = open_record(
        resources, GAME_RESOURCE_DROP_LIST, "startitems", &document
    );
    assert(game_json_array_size(game_json_object_get(drop_list, "items")) > 1);
    game_json_dispose(&document);

    GameJsonRef category = open_record(
        resources, GAME_RESOURCE_ITEM_CATEGORY, "dagger", &document
    );
    assert(game_json_string_equals(game_json_object_get(category, "actionType"), "equip"));
    game_json_dispose(&document);

    GameJsonRef filter = open_record(
        resources, GAME_RESOURCE_ITEM_FILTER, "gael_meat", &document
    );
    assert(game_json_array_size(game_json_object_get(filter, "include")) > 0);
    game_json_dispose(&document);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(game_resource_document_count == 442);
    assert(game_resource_record_count == 19605);

    GameResources resources;
    assert(game_resources_init(&resources, argv[1]));
    test_entire_index(&resources);
    test_original_records(&resources);
    assert(game_resources_find(GAME_RESOURCE_ITEM, "not_an_item", NULL) == NULL);
    assert(game_resources_find(GAME_RESOURCE_ITEM, NULL, NULL) == NULL);

    GameJsonDocument document;
    assert(!game_resources_open_document(
        &resources, game_resource_document_count, &document
    ));
    game_resources_dispose(&resources);
    puts("game resource tests passed");
    return 0;
}