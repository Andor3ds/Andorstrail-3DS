#include "game_core.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_id(const char *id)
{
    if (id == NULL || id[0] == '\0' || strlen(id) >= GAME_CORE_ID_CAPACITY) return false;
    for (const unsigned char *character = (const unsigned char *)id;
            *character != '\0'; ++character) {
        if (!isalnum(*character) && *character != '_' && *character != '-' && *character != ':') {
            return false;
        }
    }
    return true;
}

static GameCoreValue *find_value(
    GameCoreValue *values,
    size_t count,
    const char *id
)
{
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(values[index].id, id) == 0) return &values[index];
    }
    return NULL;
}

static bool set_value(
    GameCoreValue *values,
    size_t *count,
    const char *id,
    int value
)
{
    if (!valid_id(id) || value < 0) return false;
    GameCoreValue *entry = find_value(values, *count, id);
    if (value == 0) {
        if (entry == NULL) return true;
        const size_t index = (size_t)(entry - values);
        values[index] = values[*count - 1];
        --*count;
        return true;
    }
    if (entry != NULL) {
        entry->value = value;
        return true;
    }
    if (*count >= GAME_CORE_MAX_VALUES) return false;
    entry = &values[(*count)++];
    strcpy(entry->id, id);
    entry->value = value;
    return true;
}

static int get_value(const GameCoreValue *values, size_t count, const char *id)
{
    if (id == NULL) return 0;
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(values[index].id, id) == 0) return values[index].value;
    }
    return 0;
}

static bool line_finished(const char *line, int parsed_length)
{
    if (parsed_length < 0) return false;
    const char *remaining = line + parsed_length;
    while (*remaining != '\0' && isspace((unsigned char)*remaining)) ++remaining;
    return *remaining == '\0';
}

void game_core_init(GameCore *core)
{
    if (core == NULL) return;
    memset(core, 0, sizeof(*core));
    core->health = GAME_CORE_MAX_HEALTH;
    core->max_health = GAME_CORE_MAX_HEALTH;
    core->level = 1;
    core->total_experience = 1;
    core->attack_chance = 60;
    core->attack_damage = 1;
    core->critical_multiplier = 1.0f;
    core->random_state = 0x9e3779b9u;
}

void game_core_update(GameCore *core)
{
    (void)core;
}

void game_core_shutdown(GameCore *core)
{
    (void)core;
}

int game_core_quest_progress(const GameCore *core, const char *id)
{
    if (core == NULL || !valid_id(id)) return 0;
    return get_value(core->quests, core->quest_count, id);
}

bool game_core_set_quest_progress(GameCore *core, const char *id, int progress)
{
    if (core == NULL) return false;
    return set_value(core->quests, &core->quest_count, id, progress);
}

int game_core_skill_level(const GameCore *core, const char *id)
{
    if (core == NULL || !valid_id(id)) return 0;
    return get_value(core->skills, core->skill_count, id);
}

bool game_core_set_skill_level(GameCore *core, const char *id, int level)
{
    if (core == NULL) return false;
    return set_value(core->skills, &core->skill_count, id, level);
}

int game_core_inventory_count(const GameCore *core, const char *id)
{
    if (core == NULL || !valid_id(id)) return 0;
    if (strcmp(id, "gold") == 0) return core->gold;
    return get_value(core->inventory, core->inventory_count, id);
}

bool game_core_add_item(GameCore *core, const char *id, int quantity)
{
    if (core == NULL || !valid_id(id) || quantity <= 0) return false;
    if (strcmp(id, "gold") == 0) {
        if (core->gold > INT_MAX - quantity) return false;
        core->gold += quantity;
        return true;
    }
    const int current = get_value(core->inventory, core->inventory_count, id);
    if (current > INT_MAX - quantity) return false;
    return set_value(core->inventory, &core->inventory_count, id, current + quantity);
}

bool game_core_remove_item(GameCore *core, const char *id, int quantity)
{
    if (core == NULL || !valid_id(id) || quantity <= 0) return false;
    if (strcmp(id, "gold") == 0) {
        if (core->gold < quantity) return false;
        core->gold -= quantity;
        return true;
    }
    const int current = get_value(core->inventory, core->inventory_count, id);
    if (current < quantity) return false;
    return set_value(core->inventory, &core->inventory_count, id, current - quantity);
}

static const char *const EQUIPMENT_SLOT_NAMES[GAME_CORE_EQUIPMENT_SLOTS] = {
    "weapon", "shield", "head", "body", "hand", "feet", "neck",
    "leftring", "rightring"
};

static bool valid_equipment_slot(GameCoreEquipmentSlot slot)
{
    return slot >= GAME_CORE_EQUIP_WEAPON && slot < GAME_CORE_EQUIPMENT_SLOTS;
}

bool game_core_set_equipped_item(GameCore *core, GameCoreEquipmentSlot slot,
    const char *item_id)
{
    if (core == NULL || !valid_equipment_slot(slot)) return false;
    if (item_id == NULL || item_id[0] == '\0') {
        core->equipment[slot][0] = '\0';
        return true;
    }
    if (!valid_id(item_id)) return false;
    strcpy(core->equipment[slot], item_id);
    return true;
}

const char *game_core_equipped_item(const GameCore *core, GameCoreEquipmentSlot slot)
{
    if (core == NULL || !valid_equipment_slot(slot)) return NULL;
    return core->equipment[slot][0] == '\0' ? NULL : core->equipment[slot];
}

const char *game_core_quick_item(const GameCore *core, size_t slot)
{
    if (core == NULL || slot >= GAME_CORE_QUICK_SLOTS) return NULL;
    return core->quick_items[slot][0] == '\0' ? NULL : core->quick_items[slot];
}

bool game_core_set_quick_item(GameCore *core, size_t slot, const char *item_id)
{
    if (core == NULL || slot >= GAME_CORE_QUICK_SLOTS || item_id == NULL
            || (item_id[0] != '\0' && !valid_id(item_id))) return false;
    strcpy(core->quick_items[slot], item_id);
    return true;
}

bool game_core_remove_condition(GameCore *core, const char *id)
{
    if (core == NULL || !valid_id(id)
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) return false;
    size_t index = 0;
    while (index < core->condition_count) {
        if (strcmp(core->conditions[index].id, id) == 0) {
            core->conditions[index] = core->conditions[--core->condition_count];
        } else {
            ++index;
        }
    }
    return true;
}

bool game_core_remove_active_condition(GameCore *core, const char *id)
{
    if (core == NULL || !valid_id(id)
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) return false;
    size_t index = 0;
    while (index < core->condition_count) {
        if (strcmp(core->conditions[index].id, id) == 0
                && !core->conditions[index].immunity) {
            core->conditions[index] = core->conditions[--core->condition_count];
        } else {
            ++index;
        }
    }
    return true;
}

bool game_core_remove_condition_immunity(GameCore *core, const char *id,
    int duration)
{
    if (core == NULL || !valid_id(id) || duration <= 0
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) return false;
    size_t index = 0;
    while (index < core->condition_count) {
        const GameCoreCondition *condition = &core->conditions[index];
        if (condition->immunity && condition->duration == duration
                && strcmp(condition->id, id) == 0) {
            core->conditions[index] = core->conditions[--core->condition_count];
        } else {
            ++index;
        }
    }
    return true;
}

bool game_core_add_condition(GameCore *core, const char *id, int magnitude,
    int duration, bool stacking, bool immunity)
{
    if (core == NULL || !valid_id(id)
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) return false;
    if (immunity || magnitude == -99) {
        if (duration <= 0) return game_core_remove_active_condition(core, id);
        bool has_active_condition = false;
        int existing_immunity_duration = 0;
        for (size_t index = 0; index < core->condition_count; ++index) {
            const GameCoreCondition *condition = &core->conditions[index];
            if (strcmp(condition->id, id) != 0) continue;
            if (condition->immunity) {
                if (condition->duration > existing_immunity_duration) {
                    existing_immunity_duration = condition->duration;
                }
            } else {
                has_active_condition = true;
            }
        }
        if (existing_immunity_duration >= duration) {
            return game_core_remove_active_condition(core, id);
        }
        if (!has_active_condition
                && core->condition_count >= GAME_CORE_MAX_CONDITIONS
                && existing_immunity_duration == 0) return false;
        if (!game_core_remove_active_condition(core, id)) return false;
        for (size_t index = 0; index < core->condition_count; ++index) {
            GameCoreCondition *condition = &core->conditions[index];
            if (condition->immunity && strcmp(condition->id, id) == 0) {
                condition->magnitude = -99;
                condition->duration = duration;
                return true;
            }
        }
        if (core->condition_count >= GAME_CORE_MAX_CONDITIONS) return false;
        GameCoreCondition *condition = &core->conditions[core->condition_count++];
        strcpy(condition->id, id);
        condition->magnitude = -99;
        condition->duration = duration;
        condition->immunity = true;
        return true;
    }
    if (magnitude <= 0 || duration <= 0) return false;
    for (size_t index = 0; index < core->condition_count; ++index) {
        GameCoreCondition *condition = &core->conditions[index];
        if (strcmp(condition->id, id) == 0 && condition->immunity) return true;
        if (strcmp(condition->id, id) != 0 || condition->immunity) continue;
        if (stacking) {
            if (condition->duration != duration) continue;
            if (condition->magnitude > INT_MAX - magnitude) return false;
            condition->magnitude += magnitude;
            return true;
        }
        if (condition->magnitude > magnitude
                || (condition->magnitude == magnitude
                    && condition->duration >= duration)) return true;
        condition->magnitude = magnitude;
        condition->duration = duration;
        return true;
    }
    if (core->condition_count >= GAME_CORE_MAX_CONDITIONS) return false;
    GameCoreCondition *condition = &core->conditions[core->condition_count++];
    strcpy(condition->id, id);
    condition->magnitude = magnitude;
    condition->duration = duration;
    condition->immunity = false;
    return true;
}

bool game_core_set_enemy_conditions(GameCore *core, const char *map_id,
    const char *enemy_id, const GameCoreCondition *conditions, size_t count)
{
    if (core == NULL || !valid_id(map_id) || !valid_id(enemy_id)
            || core->enemy_condition_count > GAME_CORE_MAX_ENEMY_CONDITIONS
            || count > GAME_CORE_MAX_CONDITIONS
            || (count > 0 && conditions == NULL)) return false;
    for (size_t index = 0; index < count; ++index) {
        const GameCoreCondition *condition = &conditions[index];
        if (!valid_id(condition->id) || condition->duration <= 0
                || (condition->immunity ? condition->magnitude != -99
                    : condition->magnitude <= 0)) return false;
    }

    size_t retained_count = 0;
    for (size_t index = 0; index < core->enemy_condition_count; ++index) {
        const GameCoreEnemyCondition *entry = &core->enemy_conditions[index];
        if (strcmp(entry->map_id, map_id) != 0
                || strcmp(entry->enemy_id, enemy_id) != 0) ++retained_count;
    }
    if (retained_count > GAME_CORE_MAX_ENEMY_CONDITIONS - count) return false;

    size_t index = 0;
    while (index < core->enemy_condition_count) {
        GameCoreEnemyCondition *entry = &core->enemy_conditions[index];
        if (strcmp(entry->map_id, map_id) == 0
                && strcmp(entry->enemy_id, enemy_id) == 0) {
            core->enemy_conditions[index] =
                core->enemy_conditions[--core->enemy_condition_count];
        } else {
            ++index;
        }
    }
    for (index = 0; index < count; ++index) {
        GameCoreEnemyCondition *entry =
            &core->enemy_conditions[core->enemy_condition_count++];
        strcpy(entry->map_id, map_id);
        strcpy(entry->enemy_id, enemy_id);
        entry->condition = conditions[index];
    }
    return true;
}

size_t game_core_get_enemy_conditions(const GameCore *core, const char *map_id,
    const char *enemy_id, GameCoreCondition *conditions, size_t capacity)
{
    if (core == NULL || !valid_id(map_id) || !valid_id(enemy_id)
            || core->enemy_condition_count > GAME_CORE_MAX_ENEMY_CONDITIONS) return 0;
    size_t found = 0;
    for (size_t index = 0; index < core->enemy_condition_count; ++index) {
        const GameCoreEnemyCondition *entry = &core->enemy_conditions[index];
        if (strcmp(entry->map_id, map_id) != 0
                || strcmp(entry->enemy_id, enemy_id) != 0) continue;
        if (conditions != NULL && found < capacity) {
            conditions[found] = entry->condition;
        }
        ++found;
    }
    return found;
}

bool game_core_set_map_state(GameCore *core, const char *map_id,
    uint64_t last_visit_time_ms, bool visited)
{
    if (core == NULL || !valid_id(map_id)
            || core->map_state_count > GAME_CORE_MAX_MAP_STATES) return false;
    for (size_t index = 0; index < core->map_state_count; ++index) {
        GameCoreMapState *state = &core->map_states[index];
        if (strcmp(state->id, map_id) != 0) continue;
        state->last_visit_time_ms = last_visit_time_ms;
        state->visited = visited;
        return true;
    }
    if (core->map_state_count >= GAME_CORE_MAX_MAP_STATES) return false;
    GameCoreMapState *state = &core->map_states[core->map_state_count++];
    strcpy(state->id, map_id);
    state->last_visit_time_ms = last_visit_time_ms;
    state->visited = visited;
    return true;
}

bool game_core_get_map_state(const GameCore *core, const char *map_id,
    uint64_t *last_visit_time_ms, bool *visited)
{
    if (core == NULL || !valid_id(map_id)
            || core->map_state_count > GAME_CORE_MAX_MAP_STATES) return false;
    for (size_t index = 0; index < core->map_state_count; ++index) {
        const GameCoreMapState *state = &core->map_states[index];
        if (strcmp(state->id, map_id) != 0) continue;
        if (last_visit_time_ms != NULL) {
            *last_visit_time_ms = state->last_visit_time_ms;
        }
        if (visited != NULL) *visited = state->visited;
        return true;
    }
    return false;
}

bool game_core_set_map_enemies(GameCore *core, const char *map_id,
    const GameCoreSavedEnemy *enemies, size_t count)
{
    if (core == NULL || !valid_id(map_id)
            || core->saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES
            || count > GAME_CORE_MAX_SAVED_ENEMIES
            || (count > 0 && enemies == NULL)) return false;
    for (size_t index = 0; index < count; ++index) {
        const GameCoreSavedEnemy *enemy = &enemies[index];
        if (!valid_id(enemy->instance_id) || !valid_id(enemy->monster_id)
                || enemy->spawn_area_index < 0 || enemy->x < 0 || enemy->y < 0
                || enemy->health <= 0 || enemy->action_points < 0) return false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (strcmp(enemies[previous].instance_id, enemy->instance_id) == 0) {
                return false;
            }
        }
    }

    size_t retained_count = 0;
    for (size_t index = 0; index < core->saved_enemy_count; ++index) {
        if (strcmp(core->saved_enemies[index].map_id, map_id) != 0) {
            ++retained_count;
        }
    }
    if (retained_count > GAME_CORE_MAX_SAVED_ENEMIES - count) return false;

    size_t index = 0;
    while (index < core->saved_enemy_count) {
        if (strcmp(core->saved_enemies[index].map_id, map_id) == 0) {
            core->saved_enemies[index] =
                core->saved_enemies[--core->saved_enemy_count];
        } else {
            ++index;
        }
    }
    for (index = 0; index < count; ++index) {
        GameCoreSavedEnemy *saved =
            &core->saved_enemies[core->saved_enemy_count++];
        *saved = enemies[index];
        strcpy(saved->map_id, map_id);
    }
    return true;
}

size_t game_core_get_map_enemies(const GameCore *core, const char *map_id,
    GameCoreSavedEnemy *enemies, size_t capacity)
{
    if (core == NULL || !valid_id(map_id)
            || core->saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES) return 0;
    size_t found = 0;
    for (size_t index = 0; index < core->saved_enemy_count; ++index) {
        const GameCoreSavedEnemy *saved = &core->saved_enemies[index];
        if (strcmp(saved->map_id, map_id) != 0) continue;
        if (enemies != NULL && found < capacity) enemies[found] = *saved;
        ++found;
    }
    return found;
}

bool game_core_reset_map_encounters(GameCore *core)
{
    if (core == NULL || core->map_state_count > GAME_CORE_MAX_MAP_STATES
            || core->saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES
            || core->enemy_condition_count > GAME_CORE_MAX_ENEMY_CONDITIONS) {
        return false;
    }
    for (size_t index = 0; index < core->map_state_count; ++index) {
        core->map_states[index].last_visit_time_ms = 0;
    }
    size_t enemy_index = 0;
    while (enemy_index < core->saved_enemy_count) {
        const GameCoreSavedEnemy *enemy = &core->saved_enemies[enemy_index];
        if (enemy->unique) {
            ++enemy_index;
            continue;
        }
        size_t condition_index = 0;
        while (condition_index < core->enemy_condition_count) {
            const GameCoreEnemyCondition *condition =
                &core->enemy_conditions[condition_index];
            if (strcmp(condition->map_id, enemy->map_id) != 0
                    || strcmp(condition->enemy_id, enemy->instance_id) != 0) {
                ++condition_index;
                continue;
            }
            core->enemy_conditions[condition_index] =
                core->enemy_conditions[--core->enemy_condition_count];
        }
        core->saved_enemies[enemy_index] =
            core->saved_enemies[--core->saved_enemy_count];
    }
    return true;
}

bool game_core_reset_map_encounters_for_map(GameCore *core, const char *map_id)
{
    if (core == NULL || !valid_id(map_id)
            || core->map_state_count > GAME_CORE_MAX_MAP_STATES
            || core->saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES
            || core->enemy_condition_count > GAME_CORE_MAX_ENEMY_CONDITIONS) {
        return false;
    }

    for (size_t index = 0; index < core->map_state_count; ++index) {
        if (strcmp(core->map_states[index].id, map_id) == 0) {
            core->map_states[index].last_visit_time_ms = 0;
            break;
        }
    }

    size_t enemy_index = 0;
    while (enemy_index < core->saved_enemy_count) {
        const GameCoreSavedEnemy *enemy = &core->saved_enemies[enemy_index];
        if (strcmp(enemy->map_id, map_id) != 0 || enemy->unique) {
            ++enemy_index;
            continue;
        }
        size_t condition_index = 0;
        while (condition_index < core->enemy_condition_count) {
            const GameCoreEnemyCondition *condition =
                &core->enemy_conditions[condition_index];
            if (strcmp(condition->map_id, map_id) != 0
                    || strcmp(condition->enemy_id, enemy->instance_id) != 0) {
                ++condition_index;
                continue;
            }
            core->enemy_conditions[condition_index] =
                core->enemy_conditions[--core->enemy_condition_count];
        }
        core->saved_enemies[enemy_index] =
            core->saved_enemies[--core->saved_enemy_count];
    }
    return true;
}

bool game_core_set_current_map(GameCore *core, const char *map_id,
    int x, int y)
{
    if (core == NULL || !valid_id(map_id) || x < 0 || y < 0) return false;
    strcpy(core->current_map_id, map_id);
    core->current_map_x = x;
    core->current_map_y = y;
    return true;
}

bool game_core_damage_player(GameCore *core, int damage)
{
    if (core == NULL || damage <= 0 || core->health <= 0) return false;
    core->health = damage >= core->health ? 0 : core->health - damage;
    return true;
}

void game_core_restore_player(GameCore *core)
{
    if (core != NULL) core->health = core->max_health;
}

int game_core_experience_for_next_level(int level)
{
    if (level < 1 || level > GAME_CORE_MAX_LEVEL) return 0;
    const double exponent = 2.0 + (double)level / 400.0;
    const double experience = 55.0 * pow((double)level, exponent);
    if (experience >= INT_MAX) return INT_MAX;
    return (int)experience;
}

static int experience_to_reach_level(int level)
{
    int total = 0;
    for (int current = 1; current < level; ++current) {
        const int required = game_core_experience_for_next_level(current);
        if (required == 0 || total > INT_MAX - required) return INT_MAX;
        total += required;
    }
    return total;
}

int game_core_experience_in_current_level(const GameCore *core)
{
    if (core == NULL || core->level < 1 || core->level > GAME_CORE_MAX_LEVEL) return 0;
    const int level_start = experience_to_reach_level(core->level);
    if (level_start == INT_MAX || core->total_experience <= level_start) return 0;
    return core->total_experience - level_start;
}

bool game_core_add_experience(GameCore *core, int experience)
{
    if (core == NULL || experience <= 0 || core->total_experience > INT_MAX - experience) {
        return false;
    }
    core->total_experience += experience;
    return true;
}

bool game_core_can_level_up(const GameCore *core)
{
    if (core == NULL || core->level < 1 || core->level >= GAME_CORE_MAX_LEVEL) return false;
    const int level_start = experience_to_reach_level(core->level);
    const int required = game_core_experience_for_next_level(core->level);
    return level_start != INT_MAX && required > 0
        && core->total_experience >= level_start
        && core->total_experience - level_start >= required;
}

bool game_core_level_up(GameCore *core, GameCoreLevelUpChoice choice)
{
    if (core == NULL || !game_core_can_level_up(core)) return false;
    if (core->level == GAME_CORE_MAX_LEVEL) return false;
    const int next_level = core->level + 1;
    const bool awards_skill_point = next_level >= 4 && (next_level - 4) % 4 == 0;
    if (awards_skill_point && core->skill_points == INT_MAX) return false;

    switch (choice) {
    case GAME_CORE_LEVEL_UP_HEALTH:
        if (core->max_health > INT_MAX - 5 || core->health > INT_MAX - 5) return false;
        core->max_health += 5;
        core->health += 5;
        break;
    case GAME_CORE_LEVEL_UP_ATTACK_CHANCE:
        if (core->attack_chance > INT_MAX - 5) return false;
        core->attack_chance += 5;
        break;
    case GAME_CORE_LEVEL_UP_ATTACK_DAMAGE:
        if (core->attack_damage == INT_MAX) return false;
        ++core->attack_damage;
        break;
    case GAME_CORE_LEVEL_UP_BLOCK_CHANCE:
        if (core->block_chance > INT_MAX - 3) return false;
        core->block_chance += 3;
        break;
    default:
        return false;
    }

    ++core->level;
    if (core->skill_count <= GAME_CORE_MAX_VALUES) {
        const int fortitude_level = game_core_skill_level(core, "fortitude");
        const int health_to_restore = core->max_health - core->health;
        const int fortitude_health = fortitude_level < health_to_restore
            ? fortitude_level : health_to_restore;
        if (fortitude_health > 0) core->health += fortitude_health;
    }
    if (awards_skill_point) ++core->skill_points;
    return true;
}

uint32_t game_core_random(GameCore *core, uint32_t upper_bound)
{
    if (core == NULL || upper_bound == 0) return 0;
    uint32_t value = core->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    if (value == 0) value = 0x9e3779b9u;
    core->random_state = value;
    return value % upper_bound;
}

bool game_core_save(const GameCore *core, const char *path)
{
    if (core == NULL || path == NULL || path[0] == '\0' || core->gold < 0
            || core->health < 0 || core->health > core->max_health
            || core->max_health < GAME_CORE_MAX_HEALTH
            || core->level < 1 || core->level > GAME_CORE_MAX_LEVEL
            || core->total_experience < 1 || core->attack_chance < 0
            || core->attack_damage < 1 || core->block_chance < 0
            || core->critical_skill < 0 || !isfinite(core->critical_multiplier)
            || core->critical_multiplier < 0.0f || core->skill_points < 0
            || core->random_state == 0
            || core->quest_count > GAME_CORE_MAX_VALUES
            || core->condition_count > GAME_CORE_MAX_CONDITIONS
            || core->enemy_condition_count > GAME_CORE_MAX_ENEMY_CONDITIONS
            || core->map_state_count > GAME_CORE_MAX_MAP_STATES
            || core->saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES
            || core->inventory_count > GAME_CORE_MAX_VALUES
            || core->skill_count > GAME_CORE_MAX_VALUES) return false;
    if (core->current_map_id[0] != '\0'
            && (!valid_id(core->current_map_id) || core->current_map_x < 0
                || core->current_map_y < 0)) return false;
    const size_t path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;
    char *temporary_path = malloc(path_length + sizeof(".tmp"));
    if (temporary_path == NULL) return false;
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));

    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        free(temporary_path);
        return false;
    }
    bool valid = fprintf(file,
        "ANDORTRAIL_SAVE 10\ngold %d\nhealth %d\nmax_health %d\nlevel %d\n"
        "experience %d\nattack_chance %d\nattack_damage %d\nblock_chance %d\n"
        "critical_skill %d\ncritical_multiplier %.9g\n"
        "skill_points %d\nrandom %" PRIu32 "\nenemy_spawn_sequence %" PRIu32 "\n",
        core->gold, core->health, core->max_health, core->level, core->total_experience,
        core->attack_chance, core->attack_damage, core->block_chance,
        core->critical_skill, (double)core->critical_multiplier, core->skill_points,
        core->random_state, core->enemy_spawn_sequence) >= 0;
    if (valid && core->current_map_id[0] != '\0') {
        valid = fprintf(file, "current_map %s %d %d\n",
            core->current_map_id, core->current_map_x, core->current_map_y) >= 0;
    }
    for (size_t index = 0; valid && index < core->quest_count; ++index) {
        valid = valid_id(core->quests[index].id) && core->quests[index].value > 0
            && fprintf(file, "quest %s %d\n", core->quests[index].id,
                core->quests[index].value) >= 0;
    }
    for (size_t index = 0; valid && index < GAME_CORE_EQUIPMENT_SLOTS; ++index) {
        if (core->equipment[index][0] == '\0') continue;
        valid = valid_id(core->equipment[index])
            && fprintf(file, "equip %s %s\n", EQUIPMENT_SLOT_NAMES[index],
                core->equipment[index]) >= 0;
    }
    for (size_t index = 0; valid && index < GAME_CORE_QUICK_SLOTS; ++index) {
        if (core->quick_items[index][0] == '\0') continue;
        valid = valid_id(core->quick_items[index])
            && fprintf(file, "quickslot %zu %s\n", index,
                core->quick_items[index]) >= 0;
    }
    for (size_t index = 0; valid && index < core->condition_count; ++index) {
        const GameCoreCondition *condition = &core->conditions[index];
        valid = valid_id(condition->id) && condition->duration > 0
            && (condition->immunity ? condition->magnitude == -99
                : condition->magnitude > 0)
            && fprintf(file, "condition %s %d %d %d\n", condition->id,
                condition->magnitude, condition->duration,
                condition->immunity ? 1 : 0) >= 0;
    }
    for (size_t index = 0; valid && index < core->enemy_condition_count; ++index) {
        const GameCoreEnemyCondition *entry = &core->enemy_conditions[index];
        const GameCoreCondition *condition = &entry->condition;
        valid = valid_id(entry->map_id) && valid_id(entry->enemy_id)
            && valid_id(condition->id) && condition->duration > 0
            && (condition->immunity ? condition->magnitude == -99
                : condition->magnitude > 0)
            && fprintf(file, "enemy_condition %s %s %s %d %d %d\n",
                entry->map_id, entry->enemy_id, condition->id,
                condition->magnitude, condition->duration,
                condition->immunity ? 1 : 0) >= 0;
    }
    for (size_t index = 0; valid && index < core->map_state_count; ++index) {
        const GameCoreMapState *state = &core->map_states[index];
        valid = valid_id(state->id)
            && fprintf(file, "map_state %s %" PRIu64 " %d\n", state->id,
                state->last_visit_time_ms, state->visited ? 1 : 0) >= 0;
    }
    for (size_t index = 0; valid && index < core->saved_enemy_count; ++index) {
        const GameCoreSavedEnemy *enemy = &core->saved_enemies[index];
        valid = valid_id(enemy->map_id) && valid_id(enemy->instance_id)
            && valid_id(enemy->monster_id) && enemy->spawn_area_index >= 0
            && enemy->x >= 0 && enemy->y >= 0 && enemy->health > 0
            && enemy->action_points >= 0
            && fprintf(file, "map_enemy %s %s %s %d %d %d %d %d %d\n",
                enemy->map_id, enemy->instance_id, enemy->monster_id,
                enemy->spawn_area_index, enemy->x, enemy->y, enemy->health,
                enemy->action_points, enemy->unique ? 1 : 0) >= 0;
    }
    for (size_t index = 0; valid && index < core->inventory_count; ++index) {
        valid = valid_id(core->inventory[index].id) && core->inventory[index].value > 0
            && fprintf(file, "item %s %d\n", core->inventory[index].id,
                core->inventory[index].value) >= 0;
    }
    for (size_t index = 0; valid && index < core->skill_count; ++index) {
        valid = valid_id(core->skills[index].id) && core->skills[index].value > 0
            && fprintf(file, "skill %s %d\n", core->skills[index].id,
                core->skills[index].value) >= 0;
    }
    if (fclose(file) != 0) valid = false;
    if (valid && rename(temporary_path, path) != 0) valid = false;
    if (!valid) remove(temporary_path);
    free(temporary_path);
    return valid;
}

GameCoreLoadResult game_core_load(GameCore *core, const char *path)
{
    if (core == NULL || path == NULL || path[0] == '\0') return GAME_CORE_LOAD_ERROR;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return errno == ENOENT ? GAME_CORE_LOAD_NOT_FOUND : GAME_CORE_LOAD_ERROR;

    static GameCore loaded;
    game_core_init(&loaded);
    char line[512];
    bool valid = fgets(line, sizeof(line), file) != NULL;
    int version = 0;
    if (valid && strcmp(line, "ANDORTRAIL_SAVE 1\n") == 0) {
        version = 1;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 2\n") == 0) {
        version = 2;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 3\n") == 0) {
        version = 3;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 4\n") == 0) {
        version = 4;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 5\n") == 0) {
        version = 5;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 6\n") == 0) {
        version = 6;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 7\n") == 0) {
        version = 7;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 8\n") == 0) {
        version = 8;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 9\n") == 0) {
        version = 9;
    } else if (valid && strcmp(line, "ANDORTRAIL_SAVE 10\n") == 0) {
        version = 10;
    } else {
        valid = false;
    }
    bool gold_seen = false;
    bool health_seen = version == 1;
    bool random_seen = version == 1;
    bool max_health_seen = version < 3;
    bool level_seen = version < 3;
    bool experience_seen = version < 3;
    bool attack_chance_seen = version < 3;
    bool attack_damage_seen = version < 3;
    bool block_chance_seen = version < 3;
    bool critical_skill_seen = version < 5;
    bool critical_multiplier_seen = version < 5;
    bool skill_points_seen = version < 3;
    bool enemy_spawn_sequence_seen = version < 9;
    bool current_map_seen = false;
    bool quickslot_seen[GAME_CORE_QUICK_SLOTS] = {false};
    while (valid && fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        if (length == 0) continue;

        int value = 0;
        int magnitude = 0;
        int duration = 0;
        int immunity_value = 0;
        int parsed_length = -1;
        char id[GAME_CORE_ID_CAPACITY];
        char slot_name[GAME_CORE_ID_CAPACITY];
        char item_id[GAME_CORE_ID_CAPACITY];
        char map_id[GAME_CORE_ID_CAPACITY];
        char enemy_id[GAME_CORE_ID_CAPACITY];
        char condition_id[GAME_CORE_ID_CAPACITY];
        size_t quickslot_index = 0;
        uint32_t enemy_spawn_sequence = 0;
        uint64_t last_visit_time_ms = 0;
        int visited_value = 0;
        int spawn_area_index = 0;
        int x = 0;
        int y = 0;
        int health_value = 0;
        int action_points = 0;
        int unique_value = 0;
        if (sscanf(line, "gold %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (gold_seen || value < 0) valid = false;
            else {
                loaded.gold = value;
                gold_seen = true;
            }
        } else if (sscanf(line, "health %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 2 || health_seen || value < 0
                    || (version == 2 && value > 10)
                    || (version >= 3 && value < 0)) {
                valid = false;
            } else {
                loaded.health = value;
                health_seen = true;
            }
        } else if (sscanf(line, "max_health %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || max_health_seen || value < GAME_CORE_MAX_HEALTH) valid = false;
            else {
                loaded.max_health = value;
                max_health_seen = true;
            }
        } else if (sscanf(line, "level %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || level_seen || value < 1 || value > GAME_CORE_MAX_LEVEL) valid = false;
            else {
                loaded.level = value;
                level_seen = true;
            }
        } else if (sscanf(line, "experience %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || experience_seen || value < 1) valid = false;
            else {
                loaded.total_experience = value;
                experience_seen = true;
            }
        } else if (sscanf(line, "attack_chance %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || attack_chance_seen || value < 0) valid = false;
            else {
                loaded.attack_chance = value;
                attack_chance_seen = true;
            }
        } else if (sscanf(line, "attack_damage %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || attack_damage_seen || value < 1) valid = false;
            else {
                loaded.attack_damage = value;
                attack_damage_seen = true;
            }
        } else if (sscanf(line, "block_chance %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || block_chance_seen || value < 0) valid = false;
            else {
                loaded.block_chance = value;
                block_chance_seen = true;
            }
        } else if (sscanf(line, "critical_skill %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 5 || critical_skill_seen || value < 0) valid = false;
            else {
                loaded.critical_skill = value;
                critical_skill_seen = true;
            }
        } else if (sscanf(line, "critical_multiplier %f%n",
                    &loaded.critical_multiplier, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 5 || critical_multiplier_seen
                    || !isfinite(loaded.critical_multiplier)
                    || loaded.critical_multiplier < 0.0f) {
                valid = false;
            } else {
                critical_multiplier_seen = true;
            }
        } else if (sscanf(line, "skill_points %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 3 || skill_points_seen || value < 0) valid = false;
            else {
                loaded.skill_points = value;
                skill_points_seen = true;
            }
        } else if (sscanf(line, "random %" SCNu32 "%n", &loaded.random_state,
                    &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 2 || random_seen || loaded.random_state == 0) valid = false;
            else random_seen = true;
        } else if (sscanf(line, "enemy_spawn_sequence %" SCNu32 "%n",
                    &enemy_spawn_sequence, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (version < 9 || enemy_spawn_sequence_seen) valid = false;
            else {
                loaded.enemy_spawn_sequence = enemy_spawn_sequence;
                enemy_spawn_sequence_seen = true;
            }
        } else if (sscanf(line, "current_map %63s %d %d%n", map_id,
                    &x, &y, &parsed_length) == 3
                && line_finished(line, parsed_length) && version >= 10
                && valid_id(map_id) && x >= 0 && y >= 0 && !current_map_seen) {
            strcpy(loaded.current_map_id, map_id);
            loaded.current_map_x = x;
            loaded.current_map_y = y;
            current_map_seen = true;
        } else if (sscanf(line, "map_state %63s %" SCNu64 " %d%n",
                    map_id, &last_visit_time_ms, &visited_value,
                    &parsed_length) == 3
                && line_finished(line, parsed_length) && version >= 10
                && valid_id(map_id) && (visited_value == 0 || visited_value == 1)
                && loaded.map_state_count < GAME_CORE_MAX_MAP_STATES) {
            if (game_core_get_map_state(&loaded, map_id, NULL, NULL)
                    || !game_core_set_map_state(&loaded, map_id,
                    last_visit_time_ms, visited_value != 0)) valid = false;
        } else if (sscanf(line, "map_enemy %63s %63s %63s %d %d %d %d %d %d%n",
                    map_id, enemy_id, condition_id, &spawn_area_index, &x, &y,
                    &health_value, &action_points, &unique_value,
                    &parsed_length) == 9
                && line_finished(line, parsed_length) && version >= 10
                && valid_id(map_id) && valid_id(enemy_id)
                && valid_id(condition_id) && spawn_area_index >= 0
                && x >= 0 && y >= 0 && health_value > 0 && action_points >= 0
                && (unique_value == 0 || unique_value == 1)
                && loaded.saved_enemy_count < GAME_CORE_MAX_SAVED_ENEMIES) {
            for (size_t index = 0; index < loaded.saved_enemy_count; ++index) {
                if (strcmp(loaded.saved_enemies[index].map_id, map_id) == 0
                        && strcmp(loaded.saved_enemies[index].instance_id,
                            enemy_id) == 0) valid = false;
            }
            if (!valid) continue;
            GameCoreSavedEnemy *saved =
                &loaded.saved_enemies[loaded.saved_enemy_count++];
            strcpy(saved->map_id, map_id);
            strcpy(saved->instance_id, enemy_id);
            strcpy(saved->monster_id, condition_id);
            saved->spawn_area_index = spawn_area_index;
            saved->x = x;
            saved->y = y;
            saved->health = health_value;
            saved->action_points = action_points;
            saved->unique = unique_value != 0;
        } else if (sscanf(line, "quest %63s %d%n", id, &value, &parsed_length) == 2
                && line_finished(line, parsed_length) && valid_id(id) && value > 0) {
            if (find_value(loaded.quests, loaded.quest_count, id) != NULL
                    || !set_value(loaded.quests, &loaded.quest_count, id, value)) valid = false;
        } else if (sscanf(line, "item %63s %d%n", id, &value, &parsed_length) == 2
                && line_finished(line, parsed_length) && valid_id(id)
                && strcmp(id, "gold") != 0 && value > 0) {
            if (find_value(loaded.inventory, loaded.inventory_count, id) != NULL
                    || !set_value(loaded.inventory, &loaded.inventory_count, id, value)) valid = false;
        } else if (sscanf(line, "skill %63s %d%n", id, &value, &parsed_length) == 2
                && line_finished(line, parsed_length) && valid_id(id) && value > 0) {
            if (version < 4
                    || find_value(loaded.skills, loaded.skill_count, id) != NULL
                    || !set_value(loaded.skills, &loaded.skill_count, id, value)) valid = false;
        } else if (sscanf(line, "equip %63s %63s%n", slot_name, item_id,
                    &parsed_length) == 2 && line_finished(line, parsed_length)
                && version >= 6 && valid_id(item_id)) {
            bool slot_found = false;
            for (size_t slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
                if (strcmp(slot_name, EQUIPMENT_SLOT_NAMES[slot]) != 0) continue;
                slot_found = true;
                if (loaded.equipment[slot][0] != '\0') valid = false;
                else strcpy(loaded.equipment[slot], item_id);
                break;
            }
            if (!slot_found) valid = false;
        } else if (sscanf(line, "quickslot %zu %63s%n", &quickslot_index,
                    item_id, &parsed_length) == 2
                && line_finished(line, parsed_length) && version >= 7
                && quickslot_index < GAME_CORE_QUICK_SLOTS
                && valid_id(item_id) && !quickslot_seen[quickslot_index]) {
            strcpy(loaded.quick_items[quickslot_index], item_id);
            quickslot_seen[quickslot_index] = true;
        } else if (sscanf(line, "condition %63s %d %d %d%n", id,
                    &magnitude, &duration, &immunity_value, &parsed_length) == 4
                && line_finished(line, parsed_length) && version >= 6
                && valid_id(id) && magnitude != 0 && duration > 0
                && (immunity_value == 0 || immunity_value == 1)
                && ((immunity_value == 1 && magnitude == -99)
                    || (immunity_value == 0 && magnitude > 0))) {
            bool duplicate = false;
            for (size_t index = 0; index < loaded.condition_count; ++index) {
                const GameCoreCondition *existing = &loaded.conditions[index];
                if (strcmp(existing->id, id) == 0
                        && existing->duration == duration
                        && existing->immunity == (immunity_value != 0)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate || loaded.condition_count >= GAME_CORE_MAX_CONDITIONS) {
                valid = false;
            } else {
                GameCoreCondition *condition =
                    &loaded.conditions[loaded.condition_count++];
                strcpy(condition->id, id);
                condition->magnitude = magnitude;
                condition->duration = duration;
                condition->immunity = immunity_value != 0;
            }
        } else if (sscanf(line, "enemy_condition %63s %63s %63s %d %d %d%n",
                    map_id, enemy_id, condition_id, &magnitude, &duration,
                    &immunity_value, &parsed_length) == 6
                && line_finished(line, parsed_length) && version >= 8
                && valid_id(map_id) && valid_id(enemy_id)
                && valid_id(condition_id) && magnitude != 0 && duration > 0
                && (immunity_value == 0 || immunity_value == 1)
                && ((immunity_value == 1 && magnitude == -99)
                    || (immunity_value == 0 && magnitude > 0))
                && loaded.enemy_condition_count < GAME_CORE_MAX_ENEMY_CONDITIONS) {
            GameCoreEnemyCondition *entry =
                &loaded.enemy_conditions[loaded.enemy_condition_count++];
            strcpy(entry->map_id, map_id);
            strcpy(entry->enemy_id, enemy_id);
            strcpy(entry->condition.id, condition_id);
            entry->condition.magnitude = magnitude;
            entry->condition.duration = duration;
            entry->condition.immunity = immunity_value != 0;
        } else {
            valid = false;
        }
    }
    if (ferror(file) || !gold_seen || !health_seen || !random_seen || !max_health_seen
            || !level_seen || !experience_seen || !attack_chance_seen
            || !attack_damage_seen || !block_chance_seen || !critical_skill_seen
            || !critical_multiplier_seen || !skill_points_seen
            || !enemy_spawn_sequence_seen) valid = false;
    if (valid && version == 2) {
        loaded.max_health = GAME_CORE_MAX_HEALTH;
    }
    if (valid && loaded.health > loaded.max_health) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid) return GAME_CORE_LOAD_ERROR;

    *core = loaded;
    return GAME_CORE_LOAD_OK;
}