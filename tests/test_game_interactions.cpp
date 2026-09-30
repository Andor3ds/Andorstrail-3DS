#include "game_map.hpp"
#include "game_resources.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace andortrail::map;

static const Event *find_event(
    const TranslatedDocument &document,
    EventKind kind,
    const std::string &name
)
{
    for (const Event &event : document.events) {
        if (event.kind == kind && event.name == name) return &event;
    }
    return NULL;
}

static GameJsonRef open_record(
    GameResources *resources,
    GameResourceType type,
    const char *id,
    GameJsonDocument *document
)
{
    size_t matches = 0;
    const GameResourceRecordEntry *record = game_resources_find(type, id, &matches);
    assert(record != NULL && matches != 0);
    GameJsonRef value = {NULL, GAME_JSON_NO_NODE};
    assert(game_resources_open_record(resources, record, document, &value));
    return value;
}

static void test_source_map_transitions()
{
    std::string error;
    Document home;
    Document crossglen;
    assert(load_tmx("romfs/maps/home.tmx", "home", false, home, &error));
    assert(load_tmx("romfs/maps/crossglen.tmx", "crossglen", false, crossglen, &error));
    TranslatedDocument translated_home;
    TranslatedDocument translated_crossglen;
    assert(translate_document(home, translated_home, &error));
    assert(translate_document(crossglen, translated_crossglen, &error));

    MapTransition transition;
    assert(resolve_map_transition(translated_home, translated_crossglen,
        "entrance", {5, 9}, transition));
    assert(transition.target_map == "crossglen");
    assert(transition.target_place == "home");
    assert(transition.destination.x == 5 && transition.destination.y == 5);
    assert(!resolve_map_transition(translated_home, translated_crossglen,
        "entrance", {4, 9}, transition));

    const Event *gate = find_event(translated_home, EventKind::Key, "andor:1");
    assert(gate != NULL && gate->has_requirement);
    assert(gate->requirement.type == "questProgress");
    assert(gate->requirement.id == "andor" && gate->requirement.value == 1);
    assert(!gate->requirement.negate);
}

static void test_home_rest_and_container(GameResources *resources)
{
    std::string error;
    Document home;
    assert(load_tmx("romfs/maps/home.tmx", "home", false, home, &error));
    TranslatedDocument translated;
    assert(translate_document(home, translated, &error));

    const Event *rest = find_event(translated, EventKind::Rest, "rest");
    assert(rest != NULL && rest->tile_rect.x == 1 && rest->tile_rect.y == 4);

    const Event *container = find_event(translated, EventKind::Container, "startitems2");
    assert(container != NULL && container->drop_list_id == "startitems2");
    GameJsonDocument document;
    GameJsonRef drop_list = open_record(resources, GAME_RESOURCE_DROP_LIST,
        container->drop_list_id.c_str(), &document);
    const GameJsonRef items = game_json_object_get(drop_list, "items");
    assert(game_json_array_size(items) > 0);
    assert(game_json_string_equals(
        game_json_object_get(game_json_array_get(items, 0), "itemID"), "gold"
    ));
    game_json_dispose(&document);
}

static void test_original_sign_phrase(GameResources *resources)
{
    std::string error;
    Document wild1;
    assert(load_tmx("romfs/maps/wild1.tmx", "wild1", false, wild1, &error));
    TranslatedDocument translated;
    assert(translate_document(wild1, translated, &error));
    const Event *sign = find_event(translated, EventKind::Sign, "sign_wild1");
    assert(sign != NULL && sign->phrase_id == "sign_wild1");

    GameJsonDocument document;
    GameJsonRef phrase = open_record(resources, GAME_RESOURCE_CONVERSATION,
        sign->phrase_id.c_str(), &document);
    char message[256];
    assert(game_json_string_copy(game_json_object_get(phrase, "message"), message, sizeof(message)));
    assert(std::string(message) == "West: Crossglen\nSouth: Fallhaven\nNorth: Feygard");
    game_json_dispose(&document);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_source_map_transitions();

    GameResources resources;
    assert(game_resources_init(&resources, argv[1]));
    test_home_rest_and_container(&resources);
    test_original_sign_phrase(&resources);
    game_resources_dispose(&resources);
    std::cout << "game interaction data tests passed\n";
    return 0;
}
