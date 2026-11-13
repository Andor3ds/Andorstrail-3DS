#ifndef ANDORSTRAIL_GAME_CORE_H
#define ANDORSTRAIL_GAME_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_CORE_MAX_VALUES 64
#define GAME_CORE_ID_CAPACITY 64
#define GAME_CORE_MAX_CONDITIONS 64
#define GAME_CORE_MAX_ENEMY_CONDITIONS 256
#define GAME_CORE_MAX_MAP_STATES 2048
#define GAME_CORE_MAX_SPAWN_AREA_STATES 1024
#define GAME_CORE_MAX_SAVED_ENEMIES 512
#define GAME_CORE_EQUIPMENT_SLOTS 9
#define GAME_CORE_QUICK_SLOTS 3
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
    char id[GAME_CORE_ID_CAPACITY];
    int magnitude;
    int duration;
    bool immunity;
} GameCoreCondition;

typedef struct {
    char map_id[GAME_CORE_ID_CAPACITY];
    char enemy_id[GAME_CORE_ID_CAPACITY];
    GameCoreCondition condition;
} GameCoreEnemyCondition;

typedef struct {
    char id[GAME_CORE_ID_CAPACITY];
    uint64_t last_visit_time_ms;
    bool visited;
} GameCoreMapState;

typedef struct {
    char map_id[GAME_CORE_ID_CAPACITY];
    char area_id[GAME_CORE_ID_CAPACITY];
    bool active;
    bool remove_all;
    bool spawn_all;
} GameCoreSpawnAreaState;

typedef struct {
    char map_id[GAME_CORE_ID_CAPACITY];
    char instance_id[GAME_CORE_ID_CAPACITY];
    char monster_id[GAME_CORE_ID_CAPACITY];
    int spawn_area_index;
    int x;
    int y;
    int health;
    int action_points;
    bool unique;
    bool force_aggressive;
} GameCoreSavedEnemy;

typedef enum {
    GAME_CORE_EQUIP_WEAPON,
    GAME_CORE_EQUIP_SHIELD,
    GAME_CORE_EQUIP_HEAD,
    GAME_CORE_EQUIP_BODY,
    GAME_CORE_EQUIP_HAND,
    GAME_CORE_EQUIP_FEET,
    GAME_CORE_EQUIP_NECK,
    GAME_CORE_EQUIP_LEFT_RING,
    GAME_CORE_EQUIP_RIGHT_RING
} GameCoreEquipmentSlot;

typedef struct {
    int gold;
    int health;
    int max_health;
    int level;
    int total_experience;
    int attack_chance;
    int attack_damage;
    int block_chance;
    int critical_skill;
    float critical_multiplier;
    int skill_points;
    uint32_t random_state;
    uint32_t enemy_spawn_sequence;
    char current_map_id[GAME_CORE_ID_CAPACITY];
    int current_map_x;
    int current_map_y;
    GameCoreValue quests[GAME_CORE_MAX_VALUES];
    size_t quest_count;
    GameCoreCondition conditions[GAME_CORE_MAX_CONDITIONS];
    size_t condition_count;
    GameCoreEnemyCondition enemy_conditions[GAME_CORE_MAX_ENEMY_CONDITIONS];
    size_t enemy_condition_count;
    GameCoreMapState map_states[GAME_CORE_MAX_MAP_STATES];
    size_t map_state_count;
    GameCoreSpawnAreaState spawn_area_states[GAME_CORE_MAX_SPAWN_AREA_STATES];
    size_t spawn_area_state_count;
    GameCoreSavedEnemy saved_enemies[GAME_CORE_MAX_SAVED_ENEMIES];
    size_t saved_enemy_count;
    char equipment[GAME_CORE_EQUIPMENT_SLOTS][GAME_CORE_ID_CAPACITY];
    char quick_items[GAME_CORE_QUICK_SLOTS][GAME_CORE_ID_CAPACITY];
    GameCoreValue inventory[GAME_CORE_MAX_VALUES];
    size_t inventory_count;
    GameCoreValue skills[GAME_CORE_MAX_VALUES];
    size_t skill_count;
    GameCoreValue alignments[GAME_CORE_MAX_VALUES];
    size_t alignment_count;
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
int game_core_alignment(const GameCore *core, const char *id);
bool game_core_set_alignment(GameCore *core, const char *id, int alignment);
bool game_core_add_alignment(GameCore *core, const char *id, int delta);
int game_core_skill_level(const GameCore *core, const char *id);
bool game_core_set_skill_level(GameCore *core, const char *id, int level);
int game_core_inventory_count(const GameCore *core, const char *id);
bool game_core_add_item(GameCore *core, const char *id, int quantity);
bool game_core_remove_item(GameCore *core, const char *id, int quantity);
bool game_core_set_equipped_item(GameCore *core, GameCoreEquipmentSlot slot,
    const char *item_id);
const char *game_core_equipped_item(const GameCore *core, GameCoreEquipmentSlot slot);
const char *game_core_quick_item(const GameCore *core, size_t slot);
bool game_core_set_quick_item(GameCore *core, size_t slot, const char *item_id);
bool game_core_add_condition(GameCore *core, const char *id, int magnitude,
    int duration, bool stacking, bool immunity);
bool game_core_remove_condition(GameCore *core, const char *id);
bool game_core_remove_active_condition(GameCore *core, const char *id);
bool game_core_remove_condition_immunity(GameCore *core, const char *id,
    int duration);
bool game_core_set_enemy_conditions(GameCore *core, const char *map_id,
    const char *enemy_id, const GameCoreCondition *conditions, size_t count);
size_t game_core_get_enemy_conditions(const GameCore *core, const char *map_id,
    const char *enemy_id, GameCoreCondition *conditions, size_t capacity);
bool game_core_set_map_state(GameCore *core, const char *map_id,
    uint64_t last_visit_time_ms, bool visited);
bool game_core_get_map_state(const GameCore *core, const char *map_id,
    uint64_t *last_visit_time_ms, bool *visited);
bool game_core_set_spawn_area_active(GameCore *core, const char *map_id,
    const char *area_id, bool active);
bool game_core_get_spawn_area_active(const GameCore *core, const char *map_id,
    const char *area_id, bool *active);
bool game_core_set_spawn_area_state(GameCore *core, const char *map_id,
    const char *area_id, bool active, bool remove_all, bool spawn_all);
bool game_core_get_spawn_area_state(const GameCore *core, const char *map_id,
    const char *area_id, bool *active, bool *remove_all, bool *spawn_all);
bool game_core_set_map_enemies(GameCore *core, const char *map_id,
    const GameCoreSavedEnemy *enemies, size_t count);
size_t game_core_get_map_enemies(const GameCore *core, const char *map_id,
    GameCoreSavedEnemy *enemies, size_t capacity);
bool game_core_reset_map_encounters(GameCore *core);
bool game_core_reset_map_encounters_for_map(GameCore *core, const char *map_id);
bool game_core_set_current_map(GameCore *core, const char *map_id,
    int x, int y);
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