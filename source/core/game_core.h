#ifndef ANDORSTRAIL_GAME_CORE_H
#define ANDORSTRAIL_GAME_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_CORE_MAX_VALUES 64
#define GAME_CORE_ID_CAPACITY 64
#define GAME_CORE_MAX_HEALTH 10

typedef struct {
    char id[GAME_CORE_ID_CAPACITY];
    int value;
} GameCoreValue;

typedef struct {
    int gold;
    int health;
    uint32_t random_state;
    GameCoreValue quests[GAME_CORE_MAX_VALUES];
    size_t quest_count;
    GameCoreValue inventory[GAME_CORE_MAX_VALUES];
    size_t inventory_count;
} GameCore;

typedef enum {
    GAME_CORE_LOAD_OK,
    GAME_CORE_LOAD_NOT_FOUND,
    GAME_CORE_LOAD_ERROR
} GameCoreLoadResult;

#ifdef __cplusplus
extern "C" {
#endif

void game_core_init(GameCore *core);
void game_core_update(GameCore *core);
void game_core_shutdown(GameCore *core);
int game_core_quest_progress(const GameCore *core, const char *id);
bool game_core_set_quest_progress(GameCore *core, const char *id, int progress);
int game_core_inventory_count(const GameCore *core, const char *id);
bool game_core_add_item(GameCore *core, const char *id, int quantity);
bool game_core_remove_item(GameCore *core, const char *id, int quantity);
bool game_core_damage_player(GameCore *core, int damage);
void game_core_restore_player(GameCore *core);
uint32_t game_core_random(GameCore *core, uint32_t upper_bound);
bool game_core_save(const GameCore *core, const char *path);
GameCoreLoadResult game_core_load(GameCore *core, const char *path);

#ifdef __cplusplus
}
#endif

#endif