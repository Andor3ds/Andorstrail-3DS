#include "game_core.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_invalid_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 1\ngold 12\nitem bread -1\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_version_two_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 2\ngold 7\nhealth 8\nrandom 2654435769\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_version_ten_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 10\ngold 9\nhealth 25\nmax_health 25\nlevel 1\n"
        "experience 1\nattack_chance 60\nattack_damage 1\nblock_chance 0\n"
        "critical_skill 0\ncritical_multiplier 1\nskill_points 0\n"
        "random 2654435769\nenemy_spawn_sequence 0\n"
        "map_enemy home old_npc merchant 0 1 1 10 5 1\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_version_three_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 3\ngold 9\nhealth 25\nmax_health 25\nlevel 1\n"
        "experience 1\nattack_chance 60\nattack_damage 1\nblock_chance 0\n"
        "skill_points 0\nrandom 2654435769\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_version_four_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 4\ngold 9\nhealth 25\nmax_health 25\nlevel 1\n"
        "experience 1\nattack_chance 60\nattack_damage 1\nblock_chance 0\n"
        "skill_points 0\nrandom 2654435769\nskill weaponChance 2\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_version_five_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 5\ngold 9\nhealth 25\nmax_health 25\n"
        "level 1\nexperience 1\nattack_chance 60\nattack_damage 1\n"
        "block_chance 0\ncritical_skill 0\ncritical_multiplier 1\n"
        "skill_points 0\nrandom 2654435769\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void write_duplicate_skill_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 4\nskill weaponChance 1\n"
        "skill weaponChance 2\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void test_scoped_map_encounter_reset(void)
{
    GameCore core;
    game_core_init(&core);
    assert(game_core_set_map_state(&core, "stale_map", 100, true));
    assert(game_core_set_map_state(&core, "other_map", 200, true));

    GameCoreSavedEnemy temporary = {0};
    strcpy(temporary.instance_id, "temporary_enemy");
    strcpy(temporary.monster_id, "rat");
    temporary.health = 2;
    GameCoreSavedEnemy unique = {0};
    strcpy(unique.instance_id, "unique_enemy");
    strcpy(unique.monster_id, "boss");
    unique.health = 5;
    unique.unique = true;
    GameCoreSavedEnemy stale_enemies[] = {temporary, unique};
    assert(game_core_set_map_enemies(&core, "stale_map", stale_enemies, 2));

    GameCoreSavedEnemy other = {0};
    strcpy(other.instance_id, "other_enemy");
    strcpy(other.monster_id, "rat");
    other.health = 2;
    assert(game_core_set_map_enemies(&core, "other_map", &other, 1));

    GameCoreCondition condition = {0};
    strcpy(condition.id, "burn");
    condition.magnitude = 1;
    condition.duration = 4;
    assert(game_core_set_enemy_conditions(&core, "stale_map",
        "temporary_enemy", &condition, 1));
    assert(game_core_set_enemy_conditions(&core, "stale_map",
        "unique_enemy", &condition, 1));
    assert(game_core_set_enemy_conditions(&core, "other_map",
        "other_enemy", &condition, 1));

    assert(game_core_reset_map_encounters_for_map(&core, "stale_map"));
    uint64_t visit_time = 0;
    bool visited = false;
    assert(game_core_get_map_state(&core, "stale_map", &visit_time, &visited));
    assert(visit_time == 0 && visited);
    assert(game_core_get_map_state(&core, "other_map", &visit_time, &visited));
    assert(visit_time == 200 && visited);
    GameCoreSavedEnemy retained[2] = {};
    assert(game_core_get_map_enemies(&core, "stale_map", retained, 2) == 1);
    assert(strcmp(retained[0].instance_id, "unique_enemy") == 0);
    assert(game_core_get_map_enemies(&core, "other_map", retained, 2) == 1);
    assert(strcmp(retained[0].instance_id, "other_enemy") == 0);
    assert(game_core_get_enemy_conditions(&core, "stale_map",
        "temporary_enemy", NULL, 0) == 0);
    assert(game_core_get_enemy_conditions(&core, "stale_map",
        "unique_enemy", NULL, 0) == 1);
    assert(game_core_get_enemy_conditions(&core, "other_map",
        "other_enemy", NULL, 0) == 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_scoped_map_encounter_reset();
    const char *save_path = argv[1];
    const size_t path_length = strlen(save_path);
    char *invalid_path = malloc(path_length + sizeof(".invalid"));
    assert(invalid_path != NULL);
    memcpy(invalid_path, save_path, path_length);
    memcpy(invalid_path + path_length, ".invalid", sizeof(".invalid"));
    remove(save_path);
    remove(invalid_path);

    GameCore core;
    game_core_init(&core);
    assert(core.level == 1);
    assert(core.total_experience == 1);
    assert(core.max_health == 25 && core.health == 25);
    assert(core.attack_chance == 60 && core.attack_damage == 1 && core.block_chance == 0);
    assert(core.critical_skill == 0 && core.critical_multiplier == 1.0f);
    assert(game_core_experience_for_next_level(1) == 55);
    assert(game_core_experience_in_current_level(&core) == 1);
    int expected_total_experience = core.total_experience;
    assert(!game_core_can_level_up(&core));
    assert(game_core_add_experience(&core, 54));
    expected_total_experience += 54;
    assert(game_core_experience_in_current_level(&core) == 55);
    assert(game_core_can_level_up(&core));
    assert(game_core_level_up(&core, GAME_CORE_LEVEL_UP_HEALTH));
    assert(core.level == 2 && core.max_health == 30 && core.health == 30);
    assert(!game_core_can_level_up(&core));
    const int level_two_experience = game_core_experience_for_next_level(2);
    assert(game_core_add_experience(&core, level_two_experience));
    expected_total_experience += level_two_experience;
    assert(game_core_level_up(&core, GAME_CORE_LEVEL_UP_ATTACK_CHANCE));
    assert(core.level == 3 && core.attack_chance == 65);
    const int level_three_experience = game_core_experience_for_next_level(3);
    assert(game_core_add_experience(&core, level_three_experience));
    expected_total_experience += level_three_experience;
    assert(game_core_level_up(&core, GAME_CORE_LEVEL_UP_BLOCK_CHANCE));
    assert(core.level == 4 && core.block_chance == 3 && core.skill_points == 1);
    const int level_four_experience = game_core_experience_for_next_level(4);
    assert(game_core_add_experience(&core, level_four_experience));
    expected_total_experience += level_four_experience;
    assert(game_core_level_up(&core, GAME_CORE_LEVEL_UP_ATTACK_DAMAGE));
    assert(core.level == 5 && core.attack_damage == 2);

    GameCore fortitude_core;
    game_core_init(&fortitude_core);
    fortitude_core.total_experience += game_core_experience_for_next_level(1);
    fortitude_core.health = 10;
    assert(game_core_set_skill_level(&fortitude_core, "fortitude", 3));
    assert(game_core_level_up(&fortitude_core, GAME_CORE_LEVEL_UP_ATTACK_CHANCE));
    assert(fortitude_core.level == 2);
    assert(fortitude_core.health == 13 && fortitude_core.max_health == 25);

    fortitude_core.total_experience += game_core_experience_for_next_level(2);
    fortitude_core.health = fortitude_core.max_health - 1;
    assert(game_core_level_up(&fortitude_core, GAME_CORE_LEVEL_UP_ATTACK_CHANCE));
    assert(fortitude_core.health == fortitude_core.max_health);

    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_NOT_FOUND);
    assert(game_core_set_quest_progress(&core, "mikhail_bread", 10));
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_set_skill_level(&core, "weaponChance", 2));
    assert(game_core_skill_level(&core, "weaponChance") == 2);
    assert(!game_core_set_skill_level(&core, "invalid id", 1));
    assert(!game_core_set_skill_level(&core, "weaponChance", -1));
    assert(game_core_set_equipped_item(&core, GAME_CORE_EQUIP_WEAPON, "training_sword"));
    assert(!game_core_set_equipped_item(&core, GAME_CORE_EQUIP_WEAPON, "invalid item"));
    assert(game_core_set_quick_item(&core, 1, "bread"));
    assert(!game_core_set_quick_item(&core, GAME_CORE_QUICK_SLOTS, "bread"));
    assert(!game_core_set_quick_item(&core, 0, "invalid item"));
    assert(game_core_add_condition(&core, "poison_weak", 1, 2, true, false));
    assert(game_core_add_condition(&core, "poison_weak", 2, 2, true, false));
    assert(core.condition_count == 1 && core.conditions[0].magnitude == 3);
    assert(game_core_add_condition(&core, "poison_weak", -99, 99, false, true));
    assert(core.condition_count == 1 && core.conditions[0].immunity);
    GameCoreCondition enemy_condition = {0};
    strcpy(enemy_condition.id, "poison_weak");
    enemy_condition.magnitude = 2;
    enemy_condition.duration = 3;
    assert(game_core_set_enemy_conditions(&core, "home", "mikhail_rat1",
        &enemy_condition, 1));
    assert(game_core_get_enemy_conditions(&core, "home", "mikhail_rat1",
        NULL, 0) == 1);
    assert(!game_core_set_enemy_conditions(&core, "home", "mikhail_rat1",
        &enemy_condition, GAME_CORE_MAX_CONDITIONS + 1));
    assert(game_core_set_enemy_conditions(&core, "home", "mikhail_rat1",
        NULL, 0));
    assert(game_core_add_item(&core, "bread", 2));
    assert(game_core_add_item(&core, "bread", 1));
    assert(game_core_inventory_count(&core, "bread") == 3);
    assert(!game_core_remove_item(&core, "bread", 4));
    assert(game_core_remove_item(&core, "bread", 1));
    assert(game_core_inventory_count(&core, "bread") == 2);
    assert(!game_core_set_quest_progress(&core, "invalid id", 1));
    assert(!game_core_add_item(&core, "bread", 0));
    assert(game_core_damage_player(&core, 3));
    assert(core.health == core.max_health - 3);
    const uint32_t next_random = game_core_random(&core, 1000);
    assert(next_random < 1000);
    GameCore expected_state = core;
    const uint32_t random_after_load = game_core_random(&expected_state, 1000);
    core.gold = 24;
    core.critical_skill = 21;
    core.critical_multiplier = 1.75f;
    core.enemy_spawn_sequence = 53;
    assert(game_core_set_enemy_conditions(&core, "home", "mikhail_rat1",
        &enemy_condition, 1));
    assert(game_core_set_map_state(&core, "home", 123456789, true));
    assert(game_core_set_spawn_area_state(&core, "home",
        "aidem_base_thieves", false, true, false));
    assert(game_core_set_spawn_area_state(&core, "home",
        "aidem_base_alaric_aggressive_spawn", true, false, true));
    assert(!game_core_set_spawn_area_active(&core, "invalid map",
        "spawn_area", true));
    assert(game_core_set_alignment(&core, "bandits", -3));
    assert(game_core_add_alignment(&core, "bandits", 1));
    assert(game_core_set_alignment(&core, "neutral", 0));
    assert(game_core_add_alignment(&core, "wrap", INT_MAX));
    assert(game_core_add_alignment(&core, "wrap", 1));
    assert(game_core_alignment(&core, "wrap") == INT_MIN);
    assert(!game_core_set_alignment(&core, "invalid id", 1));
    GameCoreSavedEnemy saved_enemy = {0};
    strcpy(saved_enemy.instance_id, "enc_home_0");
    strcpy(saved_enemy.monster_id, "trainingrat");
    saved_enemy.spawn_area_index = 0;
    saved_enemy.x = 7;
    saved_enemy.y = 9;
    saved_enemy.health = 4;
    saved_enemy.action_points = 3;
    GameCoreSavedEnemy unique_enemy = {0};
    strcpy(unique_enemy.instance_id, "named_unique");
    strcpy(unique_enemy.monster_id, "named_unique_type");
    unique_enemy.spawn_area_index = 1;
    unique_enemy.x = 12;
    unique_enemy.y = 13;
    unique_enemy.health = 8;
    unique_enemy.action_points = 5;
    unique_enemy.unique = true;
    unique_enemy.force_aggressive = true;
    GameCoreSavedEnemy saved_enemies[] = {saved_enemy, unique_enemy};
    assert(game_core_set_map_enemies(&core, "home", saved_enemies, 2));
    assert(game_core_set_enemy_conditions(&core, "home", "enc_home_0",
        &enemy_condition, 1));
    assert(game_core_set_current_map(&core, "home", 7, 9));
    assert(game_core_save(&core, save_path));

    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 24);
    assert(core.health == core.max_health - 3);
    assert(core.level == 5 && core.total_experience == expected_total_experience);
    assert(core.max_health == 30 && core.attack_chance == 65);
    assert(core.attack_damage == 2 && core.block_chance == 3 && core.skill_points == 1);
    assert(core.critical_skill == 21 && core.critical_multiplier == 1.75f);
    assert(core.enemy_spawn_sequence == 53);
    assert(game_core_alignment(&core, "bandits") == -2);
    assert(game_core_alignment(&core, "neutral") == 0);
    assert(game_core_alignment(&core, "wrap") == INT_MIN);
    uint64_t last_visit_time_ms = 0;
    bool map_visited = false;
    assert(game_core_get_map_state(&core, "home", &last_visit_time_ms,
        &map_visited));
    assert(last_visit_time_ms == 123456789 && map_visited);
    bool spawn_area_active = true;
    bool remove_all_spawn_area_monsters = false;
    bool spawn_all_in_area = false;
    assert(game_core_get_spawn_area_state(&core, "home",
        "aidem_base_thieves", &spawn_area_active,
        &remove_all_spawn_area_monsters, &spawn_all_in_area));
    assert(!spawn_area_active && remove_all_spawn_area_monsters
        && !spawn_all_in_area);
    assert(game_core_get_spawn_area_state(&core, "home",
        "aidem_base_alaric_aggressive_spawn", &spawn_area_active,
        &remove_all_spawn_area_monsters, &spawn_all_in_area));
    assert(spawn_area_active && !remove_all_spawn_area_monsters
        && spawn_all_in_area);
    assert(!game_core_get_spawn_area_active(&core, "home",
        "missing_spawn_area", &spawn_area_active));
    GameCoreSavedEnemy loaded_enemies[2] = {};
    assert(game_core_get_map_enemies(&core, "home", loaded_enemies, 2) == 2);
    GameCoreSavedEnemy loaded_enemy = loaded_enemies[0];
    if (strcmp(loaded_enemy.instance_id, "enc_home_0") != 0) {
        loaded_enemy = loaded_enemies[1];
    }
    assert(strcmp(loaded_enemy.instance_id, "enc_home_0") == 0);
    assert(strcmp(loaded_enemy.monster_id, "trainingrat") == 0);
    assert(loaded_enemy.spawn_area_index == 0 && loaded_enemy.x == 7
        && loaded_enemy.y == 9 && loaded_enemy.health == 4
        && loaded_enemy.action_points == 3);
    assert(strcmp(core.current_map_id, "home") == 0);
    assert(core.current_map_x == 7 && core.current_map_y == 9);
    assert(game_core_reset_map_encounters(&core));
    assert(game_core_get_map_state(&core, "home", &last_visit_time_ms,
        &map_visited));
    assert(last_visit_time_ms == 0 && map_visited);
    GameCoreSavedEnemy retained_unique = {0};
    assert(game_core_get_map_enemies(&core, "home", &retained_unique, 1) == 1);
    assert(strcmp(retained_unique.instance_id, "named_unique") == 0);
    assert(retained_unique.unique);
    assert(retained_unique.force_aggressive);
    assert(game_core_get_enemy_conditions(&core, "home", "enc_home_0",
        NULL, 0) == 0);
    assert(game_core_quick_item(&core, 0) == NULL);
    assert(strcmp(game_core_quick_item(&core, 1), "bread") == 0);
    assert(game_core_quick_item(&core, GAME_CORE_QUICK_SLOTS) == NULL);
    GameCoreCondition loaded_enemy_condition = {0};
    assert(game_core_get_enemy_conditions(&core, "home", "mikhail_rat1",
        &loaded_enemy_condition, 1) == 1);
    assert(strcmp(loaded_enemy_condition.id, "poison_weak") == 0);
    assert(loaded_enemy_condition.magnitude == 2
        && loaded_enemy_condition.duration == 3);
    assert(core.random_state != 0);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_skill_level(&core, "weaponChance") == 2);
    assert(game_core_inventory_count(&core, "bread") == 2);
    assert(strcmp(game_core_equipped_item(&core, GAME_CORE_EQUIP_WEAPON),
        "training_sword") == 0);
    assert(core.condition_count == 1 && core.conditions[0].immunity);
    assert(game_core_random(&core, 1000) == random_after_load);

    core.gold = 31;
    assert(game_core_set_current_map(&core, "crossglen", 3, 4));
    assert(game_core_save(&core, save_path));
    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 31);
    assert(strcmp(core.current_map_id, "crossglen") == 0);
    assert(core.current_map_x == 3 && core.current_map_y == 4);

    char backup_path[256];
    assert(snprintf(backup_path, sizeof(backup_path), "%s.bak", save_path)
        < (int)sizeof(backup_path));
    assert(rename(save_path, backup_path) == 0);
    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 31);
    core.gold = 32;
    assert(game_core_save(&core, save_path));
    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 32);

    write_invalid_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_ERROR);
    assert(core.gold == 32);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_inventory_count(&core, "bread") == 2);

    write_version_two_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 7 && core.health == 8 && core.max_health == 25);
    assert(core.level == 1 && core.total_experience == 1);
    assert(game_core_skill_level(&core, "weaponChance") == 0);

    write_version_three_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 9 && core.skill_points == 0);
    assert(game_core_skill_level(&core, "weaponChance") == 0);
    assert(core.critical_skill == 0 && core.critical_multiplier == 1.0f);

    write_version_five_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 9 && core.condition_count == 0);
    assert(game_core_equipped_item(&core, GAME_CORE_EQUIP_WEAPON) == NULL);

    write_version_four_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 9 && game_core_skill_level(&core, "weaponChance") == 2);
    assert(core.critical_skill == 0 && core.critical_multiplier == 1.0f);

    write_duplicate_skill_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_ERROR);
    assert(core.gold == 9);
    assert(game_core_skill_level(&core, "weaponChance") == 2);

    write_version_ten_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 9 && game_core_alignment(&core, "bandits") == 0);
    GameCoreSavedEnemy legacy_enemy = {};
    assert(game_core_get_map_enemies(&core, "home", &legacy_enemy, 1) == 1);
    assert(!legacy_enemy.force_aggressive);

    assert(game_core_damage_player(&core, GAME_CORE_MAX_HEALTH));
    assert(core.health == 0);
    game_core_restore_player(&core);
    assert(core.health == core.max_health);

    assert(remove(save_path) == 0);
    remove(backup_path);
    assert(remove(invalid_path) == 0);
    free(invalid_path);
    puts("game core state tests passed");
    return 0;
}
