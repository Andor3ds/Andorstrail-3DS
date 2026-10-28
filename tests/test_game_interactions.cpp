#include "game_map.hpp"
#include "game_dialogue.hpp"
#include "game_combat.hpp"
#include "game_core.h"
#include "game_items.hpp"
#include "game_resources.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
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

static const SpawnArea *find_spawn_area(
    const TranslatedDocument &document,
    const std::string &spawn_group
)
{
    for (const SpawnArea &area : document.spawn_areas) {
        if (area.active_for_new_game && area.spawn_group == spawn_group) return &area;
    }
    return NULL;
}

static GridPosition walkable_spawn_position(
    const Document &map,
    const SpawnArea &area,
    int spawn_index
)
{
    int walkable_count = 0;
    for (int y = area.tile_rect.y; y < area.tile_rect.y + area.tile_rect.height; ++y) {
        for (int x = area.tile_rect.x; x < area.tile_rect.x + area.tile_rect.width; ++x) {
            if (!is_walkable(map, x, y)) continue;
            if (walkable_count++ == spawn_index) return {x, y};
        }
    }
    assert(false && "spawn area does not have enough walkable actor positions");
    return {0, 0};
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
    Document crossglen_hall;
    assert(load_tmx("romfs/maps/home.tmx", "home", true, home, &error));
    assert(load_tmx("romfs/maps/crossglen.tmx", "crossglen", false, crossglen, &error));
    assert(load_tmx("romfs/maps/crossglen_hall.tmx", "crossglen_hall", false,
        crossglen_hall, &error));
    TranslatedDocument translated_home;
    TranslatedDocument translated_crossglen;
    TranslatedDocument translated_crossglen_hall;
    assert(translate_document(home, translated_home, &error));
    assert(translate_document(crossglen, translated_crossglen, &error));
    assert(translate_document(crossglen_hall, translated_crossglen_hall, &error));

    MapTransition transition;
    assert(resolve_map_transition(translated_home, translated_crossglen,
        "entrance", {5, 9}, transition));
    assert(transition.target_map == "crossglen");
    assert(transition.target_place == "home");
    assert(transition.destination.x == 5 && transition.destination.y == 5);
    assert(!resolve_map_transition(translated_home, translated_crossglen,
        "entrance", {4, 9}, transition));

    const Event *hall_entrance = find_event(
        translated_crossglen, EventKind::MapChange, "hall"
    );
    assert(hall_entrance != NULL && hall_entrance->target_map == "crossglen_hall");
    assert(resolve_map_transition(translated_crossglen, translated_crossglen_hall,
        hall_entrance->name, {hall_entrance->tile_rect.x, hall_entrance->tile_rect.y},
        transition));
    assert(transition.target_map == "crossglen_hall");
    assert(transition.target_place == "entrance");

    const Event *gate = find_event(translated_home, EventKind::Key, "andor:1");
    assert(gate != NULL && gate->has_requirement);
    assert(gate->requirement.type == "questProgress");
    assert(gate->requirement.id == "andor" && gate->requirement.value == 1);
    assert(!gate->requirement.negate);
}

static void test_original_actor_spawn_areas()
{
    std::string error;
    Document home;
    Document crossglen;
    Document crossglen_hall;
    assert(load_tmx("romfs/maps/home.tmx", "home", true, home, &error));
    assert(load_tmx("romfs/maps/crossglen.tmx", "crossglen", true, crossglen, &error));
    assert(load_tmx("romfs/maps/crossglen_hall.tmx", "crossglen_hall", true,
        crossglen_hall, &error));
    TranslatedDocument translated_home;
    TranslatedDocument translated_crossglen;
    TranslatedDocument translated_crossglen_hall;
    assert(translate_document(home, translated_home, &error));
    assert(translate_document(crossglen, translated_crossglen, &error));
    assert(translate_document(crossglen_hall, translated_crossglen_hall, &error));

    const SpawnArea *mikhail = find_spawn_area(translated_home, "mikhail");
    assert(mikhail != NULL && mikhail->quantity == 1);
    const GridPosition mikhail_position = walkable_spawn_position(home, *mikhail, 0);
    assert(mikhail_position.x == 9 && mikhail_position.y == 4);

    const SpawnArea *mara = find_spawn_area(translated_crossglen_hall, "Mara");
    assert(mara != NULL && mara->quantity == 1);
    const GridPosition mara_position = walkable_spawn_position(crossglen_hall, *mara, 0);
    assert(mara_position.x == 5 && mara_position.y == 6);

    const SpawnArea *rats = find_spawn_area(translated_crossglen, "trainingrat");
    assert(rats != NULL && rats->quantity == 2);
    const GridPosition first_rat = walkable_spawn_position(crossglen, *rats, 0);
    const GridPosition second_rat = walkable_spawn_position(crossglen, *rats, 1);
    assert(first_rat.x == 10 && first_rat.y == 3);
    assert(second_rat.x == 14 && second_rat.y == 3);
    assert(is_walkable(crossglen, first_rat.x, first_rat.y));
    assert(is_walkable(crossglen, second_rat.x, second_rat.y));
}

static void test_original_actor_icons(GameResources *resources)
{
    const char *monster_ids[] = {"mikhail", "mara", "tobby_trainingrat"};
    const char *expected_icons[] = {
        "monsters_mage2:0",
        "monsters_men:7",
        "monsters_rats:0",
    };
    for (std::size_t index = 0; index < sizeof(monster_ids) / sizeof(monster_ids[0]); ++index) {
        GameJsonDocument document;
        const GameJsonRef monster = open_record(resources, GAME_RESOURCE_MONSTER,
            monster_ids[index], &document);
        char icon_id[64];
        assert(game_json_string_copy(
            game_json_object_get(monster, "iconID"), icon_id, sizeof(icon_id)
        ));
        assert(std::string(icon_id) == expected_icons[index]);
        game_json_dispose(&document);
    }
}

static void test_home_rest_and_container(GameResources *resources)
{
    std::string error;
    Document home;
    assert(load_tmx("romfs/maps/home.tmx", "home", true, home, &error));
    TranslatedDocument translated;
    assert(translate_document(home, translated, &error));

    const SpawnArea *mikhail_spawn = NULL;
    for (const SpawnArea &area : translated.spawn_areas) {
        if (area.active_for_new_game && area.spawn_group == "mikhail") {
            mikhail_spawn = &area;
            break;
        }
    }
    assert(mikhail_spawn != NULL);
    assert(mikhail_spawn->tile_rect.x == 9 && mikhail_spawn->tile_rect.y == 4);
    assert(is_walkable(home, mikhail_spawn->tile_rect.x - 1, mikhail_spawn->tile_rect.y));

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

static void test_mikhail_quests(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    std::string error;
    GameCore starter_core;
    game_core_init(&starter_core);
    assert(andortrail::items::apply_drop_list(resources, &starter_core, "startitems", error));
    assert(game_core_inventory_count(&starter_core, "gold") == 12);
    assert(game_core_inventory_count(&starter_core, "club1") == 1);
    assert(game_core_inventory_count(&starter_core, "shirt1") == 1);
    assert(game_core_inventory_count(&starter_core, "ring_mikhail") == 1);
    assert(game_core_inventory_count(&starter_core, "apple_green") == 2);

    andortrail::dialogue::Conversation conversation(resources, &core);
    const auto choose = [&conversation, &error](std::size_t index) {
        if (conversation.choose(index, error)) return true;
        std::cerr << "Mikhail reply failed: " << error << "\n";
        return false;
    };

    assert(conversation.start("mikhail_start_select", error));
    assert(conversation.active());
    assert(!conversation.phrase_id().empty());
    assert(conversation.message().find("Oh good, you are awake.") != std::string::npos);
    assert(!conversation.replies().empty());
    assert(conversation.choose(0, error));
    assert(conversation.phrase_id() == "mikhail_visited");
    assert(game_core_quest_progress(&core, "andor") == 1);

    conversation.close();
    assert(andortrail::items::apply_drop_list(resources, &core, "startitems2", error));
    assert(game_core_inventory_count(&core, "gold") == 12);
    assert(game_core_inventory_count(&core, "kids_shirt") == 1);

    assert(conversation.start("mikhail_bread_select", error));
    assert(conversation.phrase_id() == "mikhail_bread_start");
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(conversation.replies().size() == 1);
    assert(conversation.choose(0, error));

    assert(conversation.start("mikhail_bread_continue", error));
    assert(conversation.replies().size() == 1);
    assert(conversation.replies()[0].text == "No, not yet.");
    assert(conversation.choose(0, error));

    assert(!andortrail::items::buy_item(resources, &core, "bread", 3, error));
    assert(andortrail::items::buy_item(resources, &core, "bread", 1, error));
    assert(game_core_inventory_count(&core, "gold") == 6);
    assert(game_core_inventory_count(&core, "bread") == 1);
    assert(conversation.start("mikhail_bread_continue", error));
    assert(conversation.replies().size() == 2);
    assert(conversation.replies()[0].text == "Yes, here you go.");
    assert(choose(0));
    assert(conversation.phrase_id() == "mikhail_bread_complete");
    assert(game_core_inventory_count(&core, "bread") == 0);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 100);

    assert(conversation.start("mikhail_rats_select", error));
    assert(conversation.phrase_id() == "mikhail_rats_start");
    assert(game_core_quest_progress(&core, "mikhail_rats") == 10);
    assert(conversation.replies().size() == 1);
    assert(conversation.replies()[0].text == "OK, I'll go check out in our garden.");
    assert(conversation.choose(0, error));

    assert(andortrail::items::apply_drop_list(resources, &core, "trainingrat", error));
    assert(andortrail::items::apply_drop_list(resources, &core, "trainingrat", error));
    assert(conversation.start("mikhail_rats_continue", error));
    assert(conversation.replies().size() == 2);
    assert(conversation.replies()[0].text == "Yes, I have dealt with the rats now.");
    assert(conversation.choose(0, error));
    assert(game_core_inventory_count(&core, "tail_trainingrat") == 0);
    assert(game_core_quest_progress(&core, "mikhail_rats") == 100);
    assert(game_core_inventory_count(&core, "kids_shield") == 1);
    assert(game_core_inventory_count(&core, "gold") >= 56);
}

static void test_training_rat_combat(GameResources *resources, const char *save_path)
{
    assert(save_path != NULL);
    assert(std::remove(save_path) == 0 || errno == ENOENT);
    GameCore core;
    game_core_init(&core);
    assert(game_core_set_quest_progress(&core, "mikhail_rats", 10));
    andortrail::combat::AttackResult result;
    std::string error;

    const int health_before_attack = core.health;
    int total_damage_taken = 0;
    for (int attempt = 0; attempt < 12; ++attempt) {
        assert(andortrail::combat::attack_monster(resources, &core, "tobby_trainingrat",
            "trainingrat", "mikhail_rat1", core.attack_damage, result, error));
        total_damage_taken += result.damage_taken;
        if (result.enemy_health == 1) break;
        assert(!result.enemy_defeated);
    }
    assert(!result.enemy_defeated && result.enemy_health == 1 && result.player_hit);
    assert(result.experience_gained == 0 && core.total_experience == 1);
    assert(core.health == health_before_attack - total_damage_taken);
    assert(game_core_save(&core, save_path));
    const int saved_player_health = core.health;
    const uint32_t saved_random_state = core.random_state;

    GameCore loaded_core;
    game_core_init(&loaded_core);
    assert(game_core_load(&loaded_core, save_path) == GAME_CORE_LOAD_OK);
    assert(game_core_quest_progress(&loaded_core, "mikhail_rat1_health") == 1);
    assert(loaded_core.health == saved_player_health);
    assert(loaded_core.random_state == saved_random_state);
    assert(loaded_core.total_experience == 1);
    core = loaded_core;

    result.enemy_defeated = false;
    for (int attempt = 0; attempt < 12 && !result.enemy_defeated; ++attempt) {
        assert(andortrail::combat::attack_monster(resources, &core, "tobby_trainingrat",
            "trainingrat", "mikhail_rat1", core.attack_damage, result, error));
    }
    assert(result.enemy_defeated && result.enemy_health == 0);
    assert(result.experience_gained == 3 && core.total_experience == 4);
    assert(game_core_inventory_count(&core, "tail_trainingrat") == 1);

    result.enemy_defeated = false;
    for (int attempt = 0; attempt < 12 && !result.enemy_defeated; ++attempt) {
        assert(andortrail::combat::attack_monster(resources, &core, "tobby_trainingrat",
            "trainingrat", "mikhail_rat2", core.attack_damage, result, error));
    }
    assert(result.enemy_defeated && result.experience_gained == 3);
    assert(core.total_experience == 7);
    assert(game_core_inventory_count(&core, "tail_trainingrat") == 2);
    assert(core.health > 0);
    assert(!andortrail::combat::attack_monster(resources, &core, "tobby_trainingrat",
        "trainingrat", "mikhail_rat2", 1, result, error));
    assert(game_core_inventory_count(&core, "gold") <= 4);
    assert(std::remove(save_path) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    test_source_map_transitions();

    GameResources resources;
    assert(game_resources_init(&resources, argv[1]));
    std::string item_name;
    std::string item_error;
    assert(andortrail::items::item_name(&resources, "bread", item_name, item_error));
    assert(item_name == "Bread");
    std::string icon_sheet;
    unsigned int icon_index = 0;
    assert(andortrail::items::item_icon(
        &resources, "club1", icon_sheet, icon_index, item_error
    ));
    assert(icon_sheet == "items_weapons" && icon_index == 42);
    assert(andortrail::items::item_icon(
        &resources, "shirt1", icon_sheet, icon_index, item_error
    ));
    assert(icon_sheet == "items_armours" && icon_index == 14);
    assert(andortrail::items::item_icon(
        &resources, "apple_green", icon_sheet, icon_index, item_error
    ));
    assert(icon_sheet == "items_consumables" && icon_index == 2);
    assert(andortrail::items::item_icon(
        &resources, "ring_mikhail", icon_sheet, icon_index, item_error
    ));
    assert(icon_sheet == "items_jewelry" && icon_index < 14);
    assert(!andortrail::items::item_name(
        &resources, "missing_item", item_name, item_error
    ));
    assert(!item_error.empty());
    test_original_actor_spawn_areas();
    test_original_actor_icons(&resources);
    test_home_rest_and_container(&resources);
    test_original_sign_phrase(&resources);
    test_mikhail_quests(&resources);
    test_training_rat_combat(&resources, argv[2]);
    game_resources_dispose(&resources);
    std::cout << "game interaction data tests passed\n";
    return 0;
}
