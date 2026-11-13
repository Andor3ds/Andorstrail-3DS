#include "game_map.hpp"
#include "game_dialogue.hpp"
#include "game_combat.hpp"
#include "game_core.h"
#include "game_items.hpp"
#include "game_quest_log.hpp"
#include "game_skills.hpp"
#include "game_resources.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

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
    assert(rats != NULL && rats->active_for_new_game && rats->quantity == 2);
    const GridPosition first_rat = walkable_spawn_position(crossglen, *rats, 0);
    const GridPosition second_rat = walkable_spawn_position(crossglen, *rats, 1);
    assert(first_rat.x == 10 && first_rat.y == 3);
    assert(second_rat.x == 14 && second_rat.y == 3);
    assert(is_walkable(crossglen, first_rat.x, first_rat.y));
    assert(is_walkable(crossglen, second_rat.x, second_rat.y));
}

static void test_original_actor_icons(GameResources *resources)
{
    const char *monster_ids[] = {"mikhail", "mara", "tiny_rat"};
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
    andortrail::dialogue::Conversation terminal_conversation(resources, &core);
    assert(terminal_conversation.start("F", error));
    assert(!terminal_conversation.active());
    assert(terminal_conversation.terminal_action()
        == andortrail::dialogue::Conversation::TerminalActionAttack);
    terminal_conversation.close();
    assert(terminal_conversation.terminal_action()
        == andortrail::dialogue::Conversation::TerminalActionNone);

    andortrail::dialogue::Conversation attack_conversation(resources, &core);
    if (!attack_conversation.start("anona_fight", error)) {
        std::cerr << "Attack conversation failed to start: " << error << "\n";
        assert(false);
    }
    assert(attack_conversation.active());
    assert(attack_conversation.replies().size() == 1);
    assert(attack_conversation.choose(0, error));
    assert(!attack_conversation.active());
    assert(attack_conversation.terminal_action()
        == andortrail::dialogue::Conversation::TerminalActionAttack);

    andortrail::dialogue::Conversation routed_attack(resources, &core);
    if (!routed_attack.start("algangror_fight_5", error)) {
        std::cerr << "Routed attack conversation failed to start: " << error << "\n";
        assert(false);
    }
    assert(routed_attack.active());
    assert(routed_attack.choose(0, error));
    assert(!routed_attack.active());
    assert(routed_attack.terminal_action()
        == andortrail::dialogue::Conversation::TerminalActionAttack);

    andortrail::dialogue::Conversation spawn_script(resources, &core);
    assert(spawn_script.start("aidem_base_fight", error));
    const std::vector<andortrail::dialogue::SpawnAreaEffect> spawn_effects =
        spawn_script.take_spawn_area_effects();
    assert(spawn_effects.size() == 5);
    assert(spawn_effects[0].area_id == "aidem_base_thieves");
    assert(!spawn_effects[0].active && !spawn_effects[0].spawn_all
        && spawn_effects[0].remove_all);
    for (std::size_t index = 1; index < spawn_effects.size(); ++index) {
        assert(spawn_effects[index].active && spawn_effects[index].spawn_all
            && !spawn_effects[index].remove_all);
    }

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

static void test_original_quest_log(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    assert(game_core_set_quest_progress(&core, "andor", 1));
    assert(game_core_set_quest_progress(&core, "mikhail_bread", 100));
    assert(game_core_set_quest_progress(&core, "mikhail_rats", 10));
    assert(game_core_set_quest_progress(&core, "mikhail_rat1_health", 1));
    assert(game_core_set_quest_progress(&core, "andor_ending", 1));

    std::vector<andortrail::quest_log::Entry> entries;
    std::string error;
    assert(andortrail::quest_log::active_quests(resources, &core, entries, error));
    assert(error.empty());
    assert(entries.size() == 2);
    assert(entries[0].id == "andor");
    assert(entries[0].name == "Search for Andor");
    assert(entries[0].objective.find("Mikhail says") != std::string::npos);
    assert(entries[1].id == "mikhail_rats");
    assert(entries[1].name == "Rats!");
    assert(entries[1].objective.find("kill the rats") != std::string::npos);

    assert(game_core_set_quest_progress(&core, "mikhail_rats", 100));
    assert(game_core_set_quest_progress(&core, "andor", 914));
    assert(andortrail::quest_log::active_quests(resources, &core, entries, error));
    assert(entries.empty());
}

static void test_original_visible_skill_catalog()
{
    std::size_t count = 0;
    const andortrail::skills::Definition *definitions =
        andortrail::skills::visible_definitions(count);
    assert(definitions != NULL);
    assert(count == 32);
    assert(std::string(definitions[0].id) == "weaponChance");
    assert(std::string(definitions[0].name) == "Weapon Accuracy");
    assert(std::string(definitions[0].description) == "Increased attack chance");
    assert(std::string(definitions[count - 1].id) == "specializationWeaponShield");
    for (std::size_t index = 0; index < count; ++index) {
        assert(definitions[index].id[0] != '\0');
        assert(definitions[index].name[0] != '\0');
        assert(definitions[index].description[0] != '\0');
    }

    GameCore combat_core;
    game_core_init(&combat_core);
    assert(game_core_set_skill_level(&combat_core, "weaponChance", 2));
    assert(game_core_set_skill_level(&combat_core, "weaponDmg", 3));
    assert(game_core_set_skill_level(&combat_core, "dodge", 2));
    assert(game_core_set_skill_level(&combat_core, "barkSkin", 4));
    const andortrail::skills::CombatBonuses bonuses =
        andortrail::skills::combat_bonuses(&combat_core);
    assert(bonuses.attack_chance == 24);
    assert(bonuses.maximum_damage == 6);
    assert(bonuses.block_chance == 18);
    assert(bonuses.damage_resistance == 4);

    GameCore core;
    game_core_init(&core);
    core.skill_points = 3;
    const andortrail::skills::Definition &weapon_chance = definitions[0];
    assert(andortrail::skills::can_level_up(&core, weapon_chance));
    assert(andortrail::skills::level_up(&core, weapon_chance));
    assert(game_core_skill_level(&core, "weaponChance") == 1);
    assert(core.skill_points == 2);

    const andortrail::skills::Definition &cleave = definitions[10];
    assert(!andortrail::skills::can_level_up(&core, cleave));
    assert(game_core_set_skill_level(&core, "weaponDmg", 1));
    assert(andortrail::skills::can_level_up(&core, cleave));
    assert(andortrail::skills::level_up(&core, cleave));
    assert(game_core_skill_level(&core, "cleave") == 1);
    assert(!andortrail::skills::can_level_up(&core, cleave));
    assert(core.skill_points == 1);

    const andortrail::skills::Definition &speed = definitions[7];
    core.level = 14;
    assert(!andortrail::skills::can_level_up(&core, speed));
    core.level = 15;
    assert(andortrail::skills::can_level_up(&core, speed));
    assert(andortrail::skills::level_up(&core, speed));
    assert(game_core_skill_level(&core, "speed") == 1);
    assert(core.skill_points == 0);
    assert(!andortrail::skills::can_level_up(&core, speed));

    core.skill_points = 1;
    assert(game_core_set_skill_level(&core, "speed", 2));
    assert(!andortrail::skills::can_level_up(&core, speed));
    assert(core.skill_points == 1);

    const andortrail::skills::Definition &bark_skin = definitions[4];
    core.level = 9;
    core.block_chance = 15;
    assert(!andortrail::skills::can_level_up(&core, bark_skin));
    core.level = 10;
    assert(andortrail::skills::can_level_up(&core, bark_skin));
    assert(andortrail::skills::level_up(&core, bark_skin));
    assert(game_core_skill_level(&core, "barkSkin") == 1);
    assert(core.skill_points == 0);
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
        assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
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
        assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
            "trainingrat", "mikhail_rat1", core.attack_damage, result, error));
    }
    assert(result.enemy_defeated && result.enemy_health == 0);
    assert(result.experience_gained == 3 && core.total_experience == 4);
    assert(game_core_inventory_count(&core, "tail_trainingrat") == 1);

    result.enemy_defeated = false;
    for (int attempt = 0; attempt < 12 && !result.enemy_defeated; ++attempt) {
        assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
            "trainingrat", "mikhail_rat2", core.attack_damage, result, error));
    }
    assert(result.enemy_defeated && result.experience_gained == 3);
    assert(core.total_experience == 7);
    assert(game_core_inventory_count(&core, "tail_trainingrat") == 2);
    assert(core.health > 0);
    assert(!andortrail::combat::attack_monster(resources, &core, "tiny_rat",
        "trainingrat", "mikhail_rat2", 1, result, error));
    assert(game_core_inventory_count(&core, "gold") <= 4);
    assert(std::remove(save_path) == 0);
}

static void test_training_rat_skill_damage(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = 1000;
    assert(game_core_set_skill_level(&core, "weaponDmg", 3));

    bool seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 90) continue;
        if (game_core_random(&probe, 7) == 0) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);

    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
        "trainingrat", "skill_effect_rat", 1, result, error));
    assert(result.player_hit);
    assert(result.damage_dealt > 1 && result.damage_dealt <= 7);
}

static void test_critical_hits(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;
    core.critical_skill = 10;
    assert(game_core_set_skill_level(&core, "moreCriticals", 1));
    assert(game_core_set_skill_level(&core, "betterCriticals", 1));

    bool seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 99) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);

    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
        "trainingrat", "critical_rat", 1, result, error));
    assert(result.player_hit && !result.player_critical_hit && result.damage_dealt == 1);

    core.critical_multiplier = 2.0f;
    seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 99) continue;
        if (game_core_random(&probe, 100) >= 10) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);
    assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
        "trainingrat", "critical_rat_with_stats", 1, result, error));
    assert(result.player_hit && result.player_critical_hit && result.damage_dealt == 2);

    game_core_init(&core);
    core.attack_chance = INT_MAX;
    core.critical_skill = 10;
    core.critical_multiplier = 2.0f;
    seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 99) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);
    assert(andortrail::combat::attack_monster(resources, &core, "lost_spirit",
        "trainingrat", "critical_immune_spirit", 1, result, error));
    assert(result.player_hit && !result.player_critical_hit && result.damage_dealt == 0);

    game_core_init(&core);
    core.attack_chance = INT_MAX;
    seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 99
                || game_core_random(&probe, 100) >= 81
                || game_core_random(&probe, 100) >= 9) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);
    assert(andortrail::combat::attack_monster(resources, &core, "young_cave_snake",
        "snake", "critical_snake", 1, result, error));
    assert(result.player_hit && result.enemy_critical_hit && result.damage_taken == 4);

    game_core_init(&core);
    core.attack_chance = INT_MAX;
    assert(andortrail::combat::attack_monster(resources, &core, "young_cave_snake",
        "snake", "high_attack_chance_snake", 1, result, error));
    assert(result.enemy_max_health == 8);

    game_core_init(&core);
    core.attack_chance = INT_MAX;
    const bool commander_attack_succeeded = andortrail::combat::attack_monster(
        resources, &core, "erwyn_commander", "snake", "default_cost_commander",
        1, result, error);
    if (!commander_attack_succeeded) std::cerr << error << '\n';
    assert(commander_attack_succeeded);
    assert(result.enemy_max_health == 90);

    game_core_init(&core);
    core.attack_chance = INT_MAX;
    assert(andortrail::combat::attack_monster(resources, &core, "apparition",
        "catacombguard", "default_chance_apparition", 1, result, error));
    assert(result.enemy_max_health == 17);
}

static void test_training_rat_reward_skills(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;
    core.max_health = 30;
    core.health = 26;
    assert(game_core_set_skill_level(&core, "moreExp", 10));
    assert(game_core_set_skill_level(&core, "eater", 3));

    bool seed_found = false;
    for (uint32_t seed = 1; seed < 10000; ++seed) {
        GameCore probe;
        game_core_init(&probe);
        probe.random_state = seed;
        if (game_core_random(&probe, 100) >= 99) continue;
        core.random_state = seed;
        seed_found = true;
        break;
    }
    assert(seed_found);

    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster(resources, &core, "tiny_rat",
        "trainingrat", "skill_reward_rat", 100, result, error));
    assert(result.enemy_defeated && result.player_hit);
    assert(result.experience_gained == 6);
    assert(core.total_experience == 7);
    assert(result.health_restored == 3 && core.health == 29);
}

static void test_monster_condition_experience_bonus(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;

    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster(resources, &core,
        "venomous_cave_snake", "snake", "condition_bonus_snake",
        100, result, error));
    assert(result.enemy_defeated && result.experience_gained == 79);
}

static void test_separate_combat_turn_actions(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;
    const int initial_health = core.health;
    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster_once(resources, &core,
        "tiny_rat", "trainingrat", "single_action_rat",
        100, result, error));
    assert(result.enemy_defeated && result.player_hit);
    assert(result.damage_taken == 0 && core.health == initial_health);

    core.random_state = 1;
    andortrail::combat::MonsterAttackResult monster_result;
    assert(andortrail::combat::monster_attack_player(resources, &core,
        "venomous_cave_snake", monster_result, error));
    if (monster_result.hit) {
        assert(monster_result.damage_taken == 2 || monster_result.damage_taken == 4);
        assert(core.health == initial_health - monster_result.damage_taken);
        assert(monster_result.player_knocked_out == (core.health == 0));
    } else {
        assert(monster_result.damage_taken == 0 && core.health == initial_health);
    }
}

static void test_evasion_does_not_change_combat_hit_chance(GameResources *resources)
{
    GameCore standard;
    game_core_init(&standard);
    standard.random_state = 34;
    standard.block_chance = 0;

    GameCore evasive;
    game_core_init(&evasive);
    evasive.random_state = 34;
    evasive.block_chance = 0;
    assert(game_core_set_skill_level(&evasive, "evasion", 1));

    std::vector<andortrail::combat::ConditionState> standard_conditions;
    std::vector<andortrail::combat::ConditionState> evasive_conditions;
    andortrail::combat::MonsterAttackResult standard_result;
    andortrail::combat::MonsterAttackResult evasive_result;
    std::string error;
    assert(andortrail::combat::monster_attack_player(resources, &standard,
        "venomous_cave_snake", standard_result, error, &standard_conditions));
    assert(andortrail::combat::monster_attack_player(resources, &evasive,
        "venomous_cave_snake", evasive_result, error, &evasive_conditions));
    assert(standard_result.hit == evasive_result.hit);
    assert(standard_result.damage_taken == evasive_result.damage_taken);
    assert(standard_result.critical_hit == evasive_result.critical_hit);
    assert(standard.random_state == evasive.random_state);
}

static void test_equipped_combat_stats(GameResources *resources)
{
    GameCore weapon_cost_core;
    game_core_init(&weapon_cost_core);
    assert(game_core_set_equipped_item(&weapon_cost_core,
        GAME_CORE_EQUIP_WEAPON, "dagger1"));
    andortrail::combat::PlayerCombatStats stats;
    std::string error;
    assert(andortrail::combat::player_combat_stats(resources,
        &weapon_cost_core, stats, error));
    assert(stats.attack_cost == 4);

    assert(game_core_set_equipped_item(&weapon_cost_core,
        GAME_CORE_EQUIP_WEAPON, "club1"));
    assert(andortrail::combat::player_combat_stats(resources,
        &weapon_cost_core, stats, error));
    assert(stats.attack_cost == 5);

    assert(game_core_set_equipped_item(&weapon_cost_core,
        GAME_CORE_EQUIP_NECK, "jewel_fallhaven"));
    assert(andortrail::combat::player_combat_stats(resources,
        &weapon_cost_core, stats, error));
    assert(stats.attack_cost == 4);

    GameCore core;
    game_core_init(&core);
    core.block_chance = 9;
    assert(game_core_set_equipped_item(&core, GAME_CORE_EQUIP_WEAPON, "clmr_dl"));
    assert(andortrail::combat::player_combat_stats(resources, &core, stats, error));
    assert(stats.attack_cost == 6);
    assert(stats.attack_chance == 68);
    assert(stats.minimum_damage == 3 && stats.maximum_damage == 6);
    assert(stats.critical_skill == 1 && stats.critical_multiplier == 1.5);
    assert(stats.block_chance == 7);

    assert(game_core_set_skill_level(&core, "weaponChance", 1));
    assert(game_core_set_skill_level(&core, "weaponDmg", 1));
    assert(andortrail::combat::player_combat_stats(resources, &core, stats, error));
    assert(stats.attack_chance == 80);
    assert(stats.minimum_damage == 3 && stats.maximum_damage == 9);

    GameCore proficiency_core;
    game_core_init(&proficiency_core);
    assert(game_core_add_item(&proficiency_core, "hunters_sword", 1));
    assert(andortrail::items::equip_item(resources, &proficiency_core,
        "hunters_sword", error));
    assert(andortrail::combat::player_combat_stats(resources,
        &proficiency_core, stats, error));
    const int base_proficiency_chance = stats.attack_chance;
    assert(game_core_set_skill_level(&proficiency_core,
        "weaponProficiency1hsword", 1));
    assert(andortrail::combat::player_combat_stats(resources,
        &proficiency_core, stats, error));
    assert(stats.attack_chance == base_proficiency_chance + 7);

    GameCore fighting_style_core;
    game_core_init(&fighting_style_core);
    assert(game_core_add_item(&fighting_style_core, "brightportflamesword", 1));
    assert(andortrail::items::equip_item(resources, &fighting_style_core,
        "brightportflamesword", error));
    assert(andortrail::combat::player_combat_stats(resources,
        &fighting_style_core, stats, error));
    assert(stats.minimum_damage == 7 && stats.maximum_damage == 13);
    assert(game_core_set_skill_level(&fighting_style_core, "fightstyle2hand", 1));
    assert(andortrail::combat::player_combat_stats(resources,
        &fighting_style_core, stats, error));
    assert(stats.minimum_damage == 9 && stats.maximum_damage == 18);

    GameCore dual_wield_core;
    game_core_init(&dual_wield_core);
    assert(game_core_add_item(&dual_wield_core, "club1", 1));
    assert(game_core_add_item(&dual_wield_core, "dagger1", 1));
    assert(andortrail::items::equip_item(resources, &dual_wield_core,
        "club1", error));
    assert(andortrail::items::equip_item(resources, &dual_wield_core,
        "dagger1", GAME_CORE_EQUIP_SHIELD, error));
    assert(std::string(game_core_equipped_item(&dual_wield_core,
        GAME_CORE_EQUIP_SHIELD)) == "dagger1");
    assert(game_core_inventory_count(&dual_wield_core, "dagger1") == 0);
    assert(andortrail::combat::player_combat_stats(resources,
        &dual_wield_core, stats, error));
    assert(stats.attack_cost == 9);
    assert(stats.attack_chance == dual_wield_core.attack_chance + 15);
    assert(stats.minimum_damage == 1 && stats.maximum_damage == 2);

    assert(game_core_set_skill_level(&dual_wield_core, "fightstyleDualWield", 1));
    assert(andortrail::combat::player_combat_stats(resources,
        &dual_wield_core, stats, error));
    assert(stats.attack_cost == 7);
    assert(stats.attack_chance == dual_wield_core.attack_chance + 20);
    assert(stats.minimum_damage == 1 && stats.maximum_damage == 3);

    assert(game_core_set_skill_level(&dual_wield_core, "fightstyleDualWield", 2));
    assert(andortrail::combat::player_combat_stats(resources,
        &dual_wield_core, stats, error));
    assert(stats.attack_cost == 5);
    assert(stats.attack_chance == dual_wield_core.attack_chance + 30);
    assert(stats.minimum_damage == 2 && stats.maximum_damage == 4);

    assert(game_core_set_skill_level(&dual_wield_core,
        "fightstyleDualWield", 0));
    assert(game_core_set_skill_level(&dual_wield_core,
        "weaponProficiencyDagger", 1));
    assert(andortrail::combat::player_combat_stats(resources,
        &dual_wield_core, stats, error));
    assert(stats.attack_chance == dual_wield_core.attack_chance + 16);
    assert(stats.damage_resistance == 0);

    GameCore difficulty_core;
    game_core_init(&difficulty_core);
    int difficulty = -1;
    assert(andortrail::combat::monster_difficulty(resources, &difficulty_core,
        "tiny_rat", difficulty, error));
    assert(difficulty >= 0 && difficulty <= 100);
    assert(!andortrail::combat::monster_difficulty(resources, &difficulty_core,
        "missing_monster", difficulty, error));
    assert(!error.empty());
}

static void test_inventory_actions(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    std::string error;
    assert(game_core_add_item(&core, "club1", 1));
    assert(game_core_add_item(&core, "ring_mikhail", 1));
    assert(game_core_add_item(&core, "apple_green", 1));

    andortrail::items::ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    assert(andortrail::items::item_action(resources, "club1", action,
        slot, two_handed, error));
    assert(action == andortrail::items::ACTION_EQUIP);
    assert(slot == GAME_CORE_EQUIP_WEAPON && !two_handed);
    assert(andortrail::items::equip_item(resources, &core, "club1", error));
    assert(game_core_inventory_count(&core, "club1") == 0);
    assert(std::string(game_core_equipped_item(&core, GAME_CORE_EQUIP_WEAPON))
        == "club1");

    assert(andortrail::items::equip_item(resources, &core, "ring_mikhail", error));
    assert(std::string(game_core_equipped_item(&core, GAME_CORE_EQUIP_LEFT_RING))
        == "ring_mikhail");
    andortrail::combat::PlayerCombatStats stats;
    assert(andortrail::combat::player_combat_stats(resources, &core, stats, error));
    assert(stats.attack_chance == core.attack_chance + 20);
    assert(andortrail::items::unequip_item(
        resources, &core, GAME_CORE_EQUIP_LEFT_RING, error));
    assert(game_core_inventory_count(&core, "ring_mikhail") == 1);

    int action_points = 10;
    int health_change = 0;
    assert(andortrail::combat::use_item(resources, &core, "apple_green",
        action_points, 10, health_change, error));
    assert(action_points == 10 && health_change == 0);
    assert(game_core_inventory_count(&core, "apple_green") == 0);
    assert(core.condition_count == 1);
    assert(std::string(core.conditions[0].id) == "food");
    assert(core.conditions[0].magnitude == 1 && core.conditions[0].duration == 8);
    core.health -= 2;
    action_points = 4;
    assert(andortrail::combat::finish_combat_round(resources, &core,
        action_points, 10, true, error));
    assert(core.health == core.max_health - 1);
    assert(action_points == 4);
    assert(core.condition_count == 1 && core.conditions[0].duration == 7);
    core.health = core.max_health - 2;
    assert(andortrail::combat::finish_combat_round(resources, &core,
        action_points, 10, true, error, true));
    assert(core.health == core.max_health - 2);
    assert(action_points == 4);
    assert(core.condition_count == 1 && core.conditions[0].duration == 7);

    GameCore ordered_conditions;
    game_core_init(&ordered_conditions);
    assert(game_core_add_condition(&ordered_conditions, "food", 1, 5,
        false, false));
    assert(game_core_add_condition(&ordered_conditions, "fire", 1, 5,
        false, false));
    action_points = 0;
    assert(andortrail::combat::finish_combat_round(resources,
        &ordered_conditions, action_points, 10, true, error));
    assert(ordered_conditions.health == ordered_conditions.max_health);

    GameCore combat_exit_core;
    game_core_init(&combat_exit_core);
    assert(game_core_set_skill_level(&combat_exit_core, "regeneration", 1));
    combat_exit_core.health -= 2;
    action_points = 10;
    assert(andortrail::combat::finish_combat_round(resources,
        &combat_exit_core, action_points, 10, true, error, false, false));
    assert(combat_exit_core.health == combat_exit_core.max_health - 2);

    GameCore player_round_core;
    game_core_init(&player_round_core);
    assert(game_core_set_skill_level(&player_round_core, "regeneration", 1));
    player_round_core.health -= 2;
    action_points = 10;
    assert(andortrail::combat::finish_combat_round(resources,
        &player_round_core, action_points, 10, false, error));
    assert(player_round_core.health == player_round_core.max_health - 1);

    GameCore immunity_core;
    game_core_init(&immunity_core);
    assert(game_core_add_condition(&immunity_core, "nausea", 1, 5, false, false));
    assert(game_core_add_item(&immunity_core, "elm_mushroom1", 1));
    action_points = 10;
    assert(andortrail::combat::use_item(resources, &immunity_core,
        "elm_mushroom1", action_points, 10, health_change, error));
    bool nausea_immunity = false;
    for (std::size_t index = 0; index < immunity_core.condition_count; ++index) {
        const GameCoreCondition &condition = immunity_core.conditions[index];
        if (std::string(condition.id) == "nausea" && condition.immunity
                && condition.duration == 75) nausea_immunity = true;
    }
    assert(nausea_immunity);
    assert(game_core_add_condition(&immunity_core, "nausea", -99,
        20, false, true));
    bool original_immunity_kept = false;
    for (std::size_t index = 0; index < immunity_core.condition_count; ++index) {
        const GameCoreCondition &condition = immunity_core.conditions[index];
        if (std::string(condition.id) == "nausea" && condition.immunity
                && condition.duration == 75) original_immunity_kept = true;
    }
    assert(original_immunity_kept);
}

static void test_equipment_condition_removal(GameResources *resources)
{
    bool resistance_chance_scaled = false;
    for (std::uint32_t seed = 1; seed <= 10000
            && !resistance_chance_scaled; ++seed) {
        GameCore unprotected;
        game_core_init(&unprotected);
        unprotected.random_state = seed;
        unprotected.block_chance = -1000;

        GameCore resistant;
        game_core_init(&resistant);
        resistant.random_state = seed;
        resistant.block_chance = -1000;
        assert(game_core_set_skill_level(&resistant, "resistanceBlood", 1));

        std::vector<andortrail::combat::ConditionState> unprotected_conditions;
        std::vector<andortrail::combat::ConditionState> resistant_conditions;
        andortrail::combat::MonsterAttackResult unprotected_result;
        andortrail::combat::MonsterAttackResult resistant_result;
        std::string error;
        assert(andortrail::combat::monster_attack_player(resources, &unprotected,
            "venomous_cave_snake", unprotected_result, error,
            &unprotected_conditions));
        assert(andortrail::combat::monster_attack_player(resources, &resistant,
            "venomous_cave_snake", resistant_result, error,
            &resistant_conditions));
        if (!unprotected_result.hit || !resistant_result.hit) continue;

        bool unprotected_poisoned = false;
        bool resistant_poisoned = false;
        for (std::size_t index = 0; index < unprotected.condition_count; ++index) {
            unprotected_poisoned = unprotected_poisoned
                || std::string(unprotected.conditions[index].id) == "poison_weak";
        }
        for (std::size_t index = 0; index < resistant.condition_count; ++index) {
            resistant_poisoned = resistant_poisoned
                || std::string(resistant.conditions[index].id) == "poison_weak";
        }
        resistance_chance_scaled = unprotected_poisoned && !resistant_poisoned;
    }
    assert(resistance_chance_scaled);

    std::uint32_t poison_seed = 0;
    for (std::uint32_t seed = 1; seed <= 1000 && poison_seed == 0; ++seed) {
        GameCore unprotected;
        game_core_init(&unprotected);
        unprotected.random_state = seed;
        unprotected.block_chance = -1000;
        std::vector<andortrail::combat::ConditionState> conditions;
        andortrail::combat::MonsterAttackResult result;
        std::string error;
        assert(andortrail::combat::monster_attack_player(resources, &unprotected,
            "venomous_cave_snake", result, error, &conditions));
        if (!result.hit) continue;
        for (std::size_t index = 0; index < unprotected.condition_count; ++index) {
            if (std::string(unprotected.conditions[index].id) == "poison_weak") {
                poison_seed = seed;
                break;
            }
        }
    }
    assert(poison_seed != 0);

    GameCore protected_core;
    game_core_init(&protected_core);
    protected_core.random_state = poison_seed;
    protected_core.block_chance = -1000;
    assert(game_core_add_condition(&protected_core, "poison_weak",
        1, 5, false, false));
    assert(game_core_add_item(&protected_core, "ring_antipoison", 1));
    std::string error;
    assert(andortrail::items::equip_item(resources, &protected_core,
        "ring_antipoison", error));
    for (std::size_t index = 0; index < protected_core.condition_count; ++index) {
        assert(std::string(protected_core.conditions[index].id) != "poison_weak");
    }
    std::vector<andortrail::combat::ConditionState> conditions;
    andortrail::combat::MonsterAttackResult result;
    assert(andortrail::combat::monster_attack_player(resources, &protected_core,
        "venomous_cave_snake", result, error, &conditions));
    assert(result.hit);
    bool poison_reapplied = false;
    for (std::size_t index = 0; index < protected_core.condition_count; ++index) {
        if (std::string(protected_core.conditions[index].id) == "poison_weak") {
            poison_reapplied = true;
        }
    }
    assert(poison_reapplied);

    GameCore immunity_preserved;
    game_core_init(&immunity_preserved);
    assert(game_core_add_condition(&immunity_preserved, "poison_weak",
        -99, 12, false, true));
    assert(game_core_add_item(&immunity_preserved, "ring_antipoison", 1));
    assert(andortrail::items::equip_item(resources, &immunity_preserved,
        "ring_antipoison", error));
    bool poison_immunity_kept = false;
    for (std::size_t index = 0; index < immunity_preserved.condition_count; ++index) {
        const GameCoreCondition &condition = immunity_preserved.conditions[index];
        if (std::string(condition.id) == "poison_weak" && condition.immunity
                && condition.duration == 12) poison_immunity_kept = true;
    }
    assert(poison_immunity_kept);
}

static void test_equipment_granted_conditions(GameResources *resources)
{
    GameCore legacy_core;
    game_core_init(&legacy_core);
    assert(game_core_set_equipped_item(&legacy_core, GAME_CORE_EQUIP_LEFT_RING,
        "ring_shadow0"));
    std::string error;
    assert(andortrail::items::reconcile_equipment_conditions(resources,
        &legacy_core, error));
    int legacy_regeneration_magnitude = 0;
    for (std::size_t index = 0; index < legacy_core.condition_count; ++index) {
        const GameCoreCondition &condition = legacy_core.conditions[index];
        if (std::string(condition.id) == "regen" && !condition.immunity
                && condition.duration == 999) {
            legacy_regeneration_magnitude += condition.magnitude;
        }
    }
    assert(legacy_regeneration_magnitude > 0);
    assert(andortrail::items::reconcile_equipment_conditions(resources,
        &legacy_core, error));
    int reconciled_regeneration_magnitude = 0;
    for (std::size_t index = 0; index < legacy_core.condition_count; ++index) {
        const GameCoreCondition &condition = legacy_core.conditions[index];
        if (std::string(condition.id) == "regen" && !condition.immunity
                && condition.duration == 999) {
            reconciled_regeneration_magnitude += condition.magnitude;
        }
    }
    assert(reconciled_regeneration_magnitude == legacy_regeneration_magnitude);

    GameCore core;
    game_core_init(&core);
    core.health -= 4;
    assert(game_core_add_item(&core, "ring_shadow0", 2));
    assert(andortrail::items::equip_item(resources, &core,
        "ring_shadow0", error));
    assert(andortrail::items::equip_item(resources, &core,
        "ring_shadow0", error));
    bool permanent_regeneration = false;
    for (std::size_t index = 0; index < core.condition_count; ++index) {
        const GameCoreCondition &condition = core.conditions[index];
        if (std::string(condition.id) == "regen" && !condition.immunity
                && condition.duration == 999) {
            permanent_regeneration = true;
        }
    }
    assert(permanent_regeneration);

    int action_points = 10;
    assert(andortrail::combat::finish_combat_round(resources, &core,
        action_points, 10, false, error));
    permanent_regeneration = false;
    for (std::size_t index = 0; index < core.condition_count; ++index) {
        const GameCoreCondition &condition = core.conditions[index];
        if (std::string(condition.id) == "regen" && !condition.immunity
                && condition.duration == 999) {
            permanent_regeneration = true;
        }
    }
    assert(permanent_regeneration);

    GameCore until_sleep_core;
    game_core_init(&until_sleep_core);
    assert(game_core_add_condition(&until_sleep_core, "regen",
        1, 998, false, false));
    action_points = 10;
    assert(andortrail::combat::finish_combat_round(resources,
        &until_sleep_core, action_points, 10, false, error));
    assert(until_sleep_core.condition_count == 1);
    assert(until_sleep_core.conditions[0].duration == 998);

    assert(andortrail::items::unequip_item(resources, &core,
        GAME_CORE_EQUIP_LEFT_RING, error));
    permanent_regeneration = false;
    for (std::size_t index = 0; index < core.condition_count; ++index) {
        const GameCoreCondition &condition = core.conditions[index];
        if (std::string(condition.id) == "regen" && !condition.immunity
                && condition.duration == 999) {
            permanent_regeneration = true;
        }
    }
    assert(permanent_regeneration);
    assert(andortrail::items::unequip_item(resources, &core,
        GAME_CORE_EQUIP_RIGHT_RING, error));
    for (std::size_t index = 0; index < core.condition_count; ++index) {
        assert(std::string(core.conditions[index].id) != "regen");
    }

    GameCore replacement_core;
    game_core_init(&replacement_core);
    assert(game_core_add_item(&replacement_core, "ring_shadow0", 1));
    assert(game_core_add_item(&replacement_core, "ring_shadow1", 1));
    assert(andortrail::items::equip_item(resources, &replacement_core,
        "ring_shadow0", error));
    assert(andortrail::items::equip_item(resources, &replacement_core,
        "ring_shadow1", GAME_CORE_EQUIP_LEFT_RING, error));
    bool permanent_regeneration_penalty = false;
    for (std::size_t index = 0; index < replacement_core.condition_count; ++index) {
        const GameCoreCondition &condition = replacement_core.conditions[index];
        assert(std::string(condition.id) != "regen");
        if (std::string(condition.id) == "regenNeg" && !condition.immunity
                && condition.duration == 999) {
            permanent_regeneration_penalty = true;
        }
    }
    assert(permanent_regeneration_penalty);
    assert(andortrail::items::unequip_item(resources, &replacement_core,
        GAME_CORE_EQUIP_LEFT_RING, error));
    for (std::size_t index = 0; index < replacement_core.condition_count; ++index) {
        assert(std::string(replacement_core.conditions[index].id) != "regenNeg");
    }
}

static void test_original_drop_roll_formats(GameResources *resources)
{
    std::string fractional_drop_list;
    std::string gold_drop_list;
    for (std::size_t record_index = 0; record_index < game_resource_record_count
            && (fractional_drop_list.empty() || gold_drop_list.empty());
            ++record_index) {
        const GameResourceRecordEntry &record = game_resource_records[record_index];
        if (record.type != GAME_RESOURCE_DROP_LIST) continue;
        GameJsonDocument document;
        GameJsonRef drop_list;
        assert(game_resources_open_record(resources, &record, &document,
            &drop_list));
        const GameJsonRef items = game_json_object_get(drop_list, "items");
        for (std::size_t index = 0; index < game_json_array_size(items); ++index) {
            const GameJsonRef item = game_json_array_get(items, index);
            char chance[32];
            char item_id[GAME_CORE_ID_CAPACITY];
            if (!game_json_string_copy(game_json_object_get(item, "chance"),
                    chance, sizeof(chance))
                    || !game_json_string_copy(game_json_object_get(item, "itemID"),
                        item_id, sizeof(item_id))) continue;
            if (fractional_drop_list.empty() && std::string(chance).find('/')
                    != std::string::npos) {
                fractional_drop_list = record.id;
            }
            int64_t minimum = 0;
            if (gold_drop_list.empty() && std::string(item_id) == "gold"
                    && chance[0] != '0'
                    && game_json_integer(game_json_object_get(
                        game_json_object_get(item, "quantity"), "min"),
                        &minimum)
                    && minimum > 0) {
                gold_drop_list = record.id;
            }
        }
        game_json_dispose(&document);
    }

    assert(!fractional_drop_list.empty());
    assert(!gold_drop_list.empty());
    GameCore core;
    game_core_init(&core);
    std::string error;
    assert(andortrail::items::apply_drop_list(resources, &core,
        fractional_drop_list, error));

    game_core_init(&core);
    assert(game_core_set_skill_level(&core, "coinfinder", 100000));
    assert(andortrail::items::apply_drop_list(resources, &core,
        gold_drop_list, error));
    assert(game_core_inventory_count(&core, "gold") > 0);
}

static void test_weapon_condition_effects(GameResources *resources)
{
    andortrail::items::ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    std::string error;
    assert(andortrail::items::item_action(resources, "brightportflamesword",
        action, slot, two_handed, error));
    assert(action == andortrail::items::ACTION_EQUIP && two_handed);

    bool applied_to_enemy = false;
    bool applied_to_player = false;
    for (std::uint32_t seed = 1; seed <= 256
            && (!applied_to_enemy || !applied_to_player); ++seed) {
        GameCore core;
        game_core_init(&core);
        core.random_state = seed;
        core.attack_chance = INT_MAX;
        assert(game_core_add_item(&core, "brightportflamesword", 1));
        assert(andortrail::items::equip_item(resources, &core,
            "brightportflamesword", error));
        std::vector<andortrail::combat::ConditionState> enemy_conditions;
        andortrail::combat::AttackResult result;
        const std::string instance_id = "flame_effect_" + std::to_string(seed);
        assert(andortrail::combat::attack_monster_once(resources, &core,
            "venomous_cave_snake", "", instance_id, core.attack_damage,
            result, error, &enemy_conditions));
        for (std::size_t index = 0; index < core.condition_count; ++index) {
            if (std::string(core.conditions[index].id) == "brightportflame") {
                applied_to_player = true;
            }
        }
        for (const andortrail::combat::ConditionState &condition : enemy_conditions) {
            if (condition.id == "brightportflame" && !condition.immunity) {
                applied_to_enemy = true;
                break;
            }
        }
    }
    assert(applied_to_enemy);
    assert(applied_to_player);
}

static void test_attack_skill_roll_after_loot(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.random_state = 4;
    core.attack_chance = INT_MAX;
    core.attack_damage = 1000;
    assert(game_core_set_skill_level(&core, "concussion", 1));

    std::vector<andortrail::combat::ConditionState> enemy_conditions;
    andortrail::combat::AttackResult result;
    std::string error;
    assert(andortrail::combat::attack_monster_once(resources, &core,
        "tiny_rat", "trainingrat", "skill_order_rat", 1000, result, error,
        &enemy_conditions));
    assert(result.player_hit && result.enemy_defeated);
    assert(game_core_inventory_count(&core, "gold") == 2);
    bool concussion_applied = false;
    for (const andortrail::combat::ConditionState &condition : enemy_conditions) {
        concussion_applied = concussion_applied
            || condition.id == "concussion";
    }
    assert(concussion_applied);
}

static void test_enemy_condition_round_and_death(GameResources *resources)
{
    {
        GameCore core;
        game_core_init(&core);
        int health = 1;
        int action_points = 10;
        std::vector<andortrail::combat::ConditionState> conditions;
        conditions.push_back({"brightportflame", 1, 1, false});
        andortrail::combat::AttackResult result;
        std::string error;
        assert(andortrail::combat::finish_enemy_combat_round(resources, &core,
            "tiny_rat", "", "condition_full_round_rat", 2, health, 10,
            action_points, conditions, result, error, true));
        assert(health == 1 && !result.enemy_defeated);
        assert(conditions.size() == 1 && conditions[0].duration == 1);
    }

    bool condition_damage_applied = false;
    std::string error;
    for (std::uint32_t seed = 1; seed <= 64 && !condition_damage_applied; ++seed) {
        GameCore core;
        game_core_init(&core);
        core.random_state = seed;
        const std::string instance_id = "condition_round_rat_" + std::to_string(seed);
        int health = 1;
        int action_points = 10;
        std::vector<andortrail::combat::ConditionState> conditions;
        conditions.push_back({"brightportflame", 1, 1, false});
        andortrail::combat::AttackResult result;
        assert(andortrail::combat::finish_enemy_combat_round(resources, &core,
            "tiny_rat", "", instance_id, 2, health, 10, action_points,
            conditions, result, error));
        if (result.enemy_defeated) {
            condition_damage_applied = true;
            assert(result.experience_gained > 0);
            assert(game_core_quest_progress(&core,
                (instance_id + "_defeated").c_str()) == 1);
        } else {
            assert(health == 1);
            assert(conditions.empty());
        }
    }
    assert(condition_damage_applied);
}

static void test_hit_and_kill_stat_effects(GameResources *resources)
{
    GameCore cleave_core;
    game_core_init(&cleave_core);
    cleave_core.attack_chance = INT_MAX;
    assert(game_core_set_skill_level(&cleave_core, "cleave", 1));
    std::string error;
    andortrail::combat::AttackResult cleave_result;
    assert(andortrail::combat::attack_monster_once(resources, &cleave_core,
        "tiny_rat", "", "cleave_test_rat", 100, cleave_result, error));
    assert(cleave_result.enemy_defeated);
    assert(cleave_result.player_action_points_change == 3);

    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;
    core.max_health = 30;
    core.health = 10;
    assert(game_core_add_item(&core, "shield_of_undead", 1));
    assert(andortrail::items::equip_item(resources, &core,
        "shield_of_undead", error));

    andortrail::combat::AttackResult kill_result;
    assert(andortrail::combat::attack_monster_once(resources, &core,
        "tiny_rat", "", "shield_kill_effect_rat", 100, kill_result, error));
    assert(kill_result.enemy_defeated);
    assert(core.health >= 13 && core.health <= 15);

    GameCore ap_core;
    game_core_init(&ap_core);
    ap_core.attack_chance = INT_MAX;
    assert(game_core_add_item(&ap_core, "hunters_sword", 1));
    assert(andortrail::items::equip_item(resources, &ap_core,
        "hunters_sword", error));
    andortrail::combat::AttackResult ap_kill_result;
    assert(andortrail::combat::attack_monster_once(resources, &ap_core,
        "tiny_rat", "", "hunter_sword_kill_ap_rat", 100, ap_kill_result, error));
    assert(ap_kill_result.enemy_defeated);
    assert(ap_kill_result.player_action_points_change >= 1
        && ap_kill_result.player_action_points_change <= 3);

    bool attacker_health_reduced = false;
    for (std::uint32_t seed = 1; seed <= 64 && !attacker_health_reduced; ++seed) {
        GameCore defender;
        game_core_init(&defender);
        defender.random_state = seed;
        defender.max_health = 30;
        defender.health = 30;
        assert(game_core_add_item(&defender, "shield_of_undead", 1));
        assert(andortrail::items::equip_item(resources, &defender,
            "shield_of_undead", error));
        int monster_health = 8;
        std::vector<andortrail::combat::ConditionState> conditions;
        andortrail::combat::MonsterAttackResult attack_result;
        if (!andortrail::combat::monster_attack_player(resources, &defender,
                "young_cave_snake", attack_result, error, &conditions,
                &monster_health, 8)) {
            std::cerr << error << '\n';
            assert(false);
        }
        if (attack_result.hit) {
            assert(monster_health >= 5 && monster_health <= 7);
            attacker_health_reduced = true;
        }
    }
    assert(attacker_health_reduced);

    bool monster_hit_healed = false;
    for (std::uint32_t seed = 1; seed <= 64 && !monster_hit_healed; ++seed) {
        GameCore defender;
        game_core_init(&defender);
        defender.random_state = seed;
        defender.block_chance = -1000;
        int monster_health = 149;
        std::vector<andortrail::combat::ConditionState> conditions;
        andortrail::combat::MonsterAttackResult attack_result;
        assert(andortrail::combat::monster_attack_player(resources, &defender,
            "wight_greater", attack_result, error, &conditions,
            &monster_health, 150));
        if (attack_result.hit) {
            assert(monster_health == 150);
            monster_hit_healed = true;
        }
    }
    assert(monster_hit_healed);
}

static void test_corpse_eater_uses_equipped_max_health(GameResources *resources)
{
    GameCore core;
    game_core_init(&core);
    core.attack_chance = INT_MAX;
    core.health = core.max_health;
    assert(game_core_set_skill_level(&core, "eater", 1));
    assert(game_core_add_item(&core, "ring_life", 1));
    std::string error;
    assert(andortrail::items::equip_item(resources, &core, "ring_life", error));

    andortrail::combat::PlayerCombatStats stats;
    assert(andortrail::combat::player_combat_stats(resources, &core, stats, error));
    assert(stats.maximum_health > core.max_health);

    andortrail::combat::AttackResult result;
    assert(andortrail::combat::attack_monster_once(resources, &core,
        "tiny_rat", "", "corpse_eater_equipped_health_rat", 1000000,
        result, error));
    assert(result.enemy_defeated);
    assert(result.health_restored == 1);
    assert(core.health == stats.maximum_health - 4);
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
    andortrail::items::InventoryProperties inventory_properties;
    assert(andortrail::items::item_inventory_properties(&resources, "club1",
        inventory_properties, item_error));
    assert(inventory_properties.group == andortrail::items::INVENTORY_GROUP_WEAPON);
    assert(inventory_properties.type_order == 110);
    assert(andortrail::items::item_inventory_properties(&resources, "shirt1",
        inventory_properties, item_error));
    assert(inventory_properties.group == andortrail::items::INVENTORY_GROUP_ARMOR);
    assert(andortrail::items::item_inventory_properties(&resources, "ring_mikhail",
        inventory_properties, item_error));
    assert(inventory_properties.group == andortrail::items::INVENTORY_GROUP_JEWELRY);
    assert(andortrail::items::item_inventory_properties(&resources, "bread",
        inventory_properties, item_error));
    assert(inventory_properties.group == andortrail::items::INVENTORY_GROUP_FOOD);
    assert(inventory_properties.type_order == 310);
    assert(inventory_properties.market_cost == 6);
    assert(andortrail::items::item_inventory_properties(&resources, "rat_tail",
        inventory_properties, item_error));
    assert(inventory_properties.group == andortrail::items::INVENTORY_GROUP_OTHER);
    assert(!andortrail::items::item_inventory_properties(&resources,
        "missing_item", inventory_properties, item_error));
    assert(!item_error.empty());
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
    test_equipped_combat_stats(&resources);
    test_inventory_actions(&resources);
    test_equipment_condition_removal(&resources);
    test_equipment_granted_conditions(&resources);
    test_original_drop_roll_formats(&resources);
    test_weapon_condition_effects(&resources);
    test_attack_skill_roll_after_loot(&resources);
    test_enemy_condition_round_and_death(&resources);
    test_hit_and_kill_stat_effects(&resources);
    test_corpse_eater_uses_equipped_max_health(&resources);
    test_original_actor_spawn_areas();
    test_original_actor_icons(&resources);
    test_home_rest_and_container(&resources);
    test_original_sign_phrase(&resources);
    test_mikhail_quests(&resources);
    test_original_quest_log(&resources);
    test_original_visible_skill_catalog();
    test_training_rat_skill_damage(&resources);
    test_critical_hits(&resources);
    test_training_rat_reward_skills(&resources);
    test_monster_condition_experience_bonus(&resources);
    test_separate_combat_turn_actions(&resources);
    test_evasion_does_not_change_combat_hit_chance(&resources);
    test_training_rat_combat(&resources, argv[2]);
    game_resources_dispose(&resources);
    std::cout << "game interaction data tests passed\n";
    return 0;
}
