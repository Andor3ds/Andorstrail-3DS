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
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_NOT_FOUND);
    assert(game_core_set_quest_progress(&core, "mikhail_bread", 10));
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_add_item(&core, "bread", 2));
    assert(game_core_add_item(&core, "bread", 1));
    assert(game_core_inventory_count(&core, "bread") == 3);
    assert(!game_core_remove_item(&core, "bread", 4));
    assert(game_core_remove_item(&core, "bread", 1));
    assert(game_core_inventory_count(&core, "bread") == 2);
    assert(!game_core_set_quest_progress(&core, "invalid id", 1));
    assert(!game_core_add_item(&core, "bread", 0));
    assert(game_core_damage_player(&core, 3));
    assert(core.health == GAME_CORE_MAX_HEALTH - 3);
    const uint32_t next_random = game_core_random(&core, 1000);
    assert(next_random < 1000);
    GameCore expected_state = core;
    const uint32_t random_after_load = game_core_random(&expected_state, 1000);
    core.gold = 24;
    assert(game_core_save(&core, save_path));

    game_core_init(&core);
    assert(game_core_load(&core, save_path) == GAME_CORE_LOAD_OK);
    assert(core.gold == 24);
    assert(core.health == GAME_CORE_MAX_HEALTH - 3);
    assert(core.random_state != 0);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_inventory_count(&core, "bread") == 2);
    assert(game_core_random(&core, 1000) == random_after_load);

    write_invalid_save(invalid_path);
    assert(game_core_load(&core, invalid_path) == GAME_CORE_LOAD_ERROR);
    assert(core.gold == 24);
    assert(game_core_quest_progress(&core, "mikhail_bread") == 10);
    assert(game_core_inventory_count(&core, "bread") == 2);

    assert(game_core_damage_player(&core, GAME_CORE_MAX_HEALTH));
    assert(core.health == 0);
    game_core_restore_player(&core);
    assert(core.health == GAME_CORE_MAX_HEALTH);

    assert(remove(save_path) == 0);
    assert(remove(invalid_path) == 0);
    free(invalid_path);
    puts("game core state tests passed");
    return 0;
}
