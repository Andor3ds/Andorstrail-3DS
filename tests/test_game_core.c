#include "game_core.h"

#include <assert.h>
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

static void write_version_three_save(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("ANDORTRAIL_SAVE 3\ngold 9\nhealth 25\nmax_health 25\nlevel 1\n"
        "experience 1\nattack_chance 60\nattack_damage 1\nblock_chance 0\n"
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

int main(int argc, char **argv)
{
    assert(argc == 2);
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
    assert(game_core_save(&core, save_path));

    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 24);
    assert(core.health == core.max_health - 3);
    assert(core.level == 5 && core.total_experience == expected_total_experience);
    assert(core.max_health == 30 && core.attack_chance == 65);
    assert(core.attack_damage == 2 && core.block_chance == 3 && core.skill_points == 1);
    assert(core.random_state != 0);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_skill_level(&core, "weaponChance") == 2);
    assert(game_core_inventory_count(&core, "bread") == 2);
    assert(game_core_random(&core, 1000) == random_after_load);

    write_invalid_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_ERROR);
    assert(core.gold == 24);
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

    write_duplicate_skill_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_ERROR);
    assert(core.gold == 9);
    assert(game_core_skill_level(&core, "weaponChance") == 0);

    assert(game_core_damage_player(&core, GAME_CORE_MAX_HEALTH));
    assert(core.health == 0);
    game_core_restore_player(&core);
    assert(core.health == core.max_health);

    assert(remove(save_path) == 0);
    assert(remove(invalid_path) == 0);
    free(invalid_path);
    puts("game core state tests passed");
    return 0;
}
