#ifndef ANDORSTRAIL_GAME_CORE_H
#define ANDORSTRAIL_GAME_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_CORE_MAX_VALUES 64
#define GAME_CORE_ID_CAPACITY 64
#define GAME_CORE_MAX_HEALTH 25
#define GAME_CORE_MAX_LEVEL 100000

typedef enum {
    GAME_CORE_LEVEL_UP_HEALTH,
    GAME_CORE_LEVEL_UP_ATTACK_CHANCE,
    GAME_CORE_LEVEL_UP_ATTACK_DAMAGE,
    GAME_CORE_LEVEL_UP_BLOCK_CHANCE
} GameCoreLevelUpChoice;

typedef struct {
    char id[GAME_CORE_ID_CAPACITY];
    int value;
} GameCoreValue;

typedef struct {
    int gold;
    int health;
    int max_health;
    int level;
    int total_experience;
    int attack_chance;
    int attack_damage;
    int block_chance;
    int skill_points;
    uint32_t random_state;
    GameCoreValue quests[GAME_CORE_MAX_VALUES];
    size_t quest_count;
    GameCoreValue inventory[GAME_CORE_MAX_VALUES];
    size_t inventory_count;
    GameCoreValue skills[GAME_CORE_MAX_VALUES];
    size_t skill_count;
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
int game_core_skill_level(const GameCore *core, const char *id);
bool game_core_set_skill_level(GameCore *core, const char *id, int level);
int game_core_inventory_count(const GameCore *core, const char *id);
bool game_core_add_item(GameCore *core, const char *id, int quantity);
bool game_core_remove_item(GameCore *core, const char *id, int quantity);
bool game_core_damage_player(GameCore *core, int damage);
void game_core_restore_player(GameCore *core);
int game_core_experience_for_next_level(int level);
int game_core_experience_in_current_level(const GameCore *core);
bool game_core_add_experience(GameCore *core, int experience);
bool game_core_can_level_up(const GameCore *core);
bool game_core_level_up(GameCore *core, GameCoreLevelUpChoice choice);
uint32_t game_core_random(GameCore *core, uint32_t upper_bound);
bool game_core_save(const GameCore *core, const char *path);
GameCoreLoadResult game_core_load(GameCore *core, const char *path);

#ifdef __cplusplus
}
#endif

#endif