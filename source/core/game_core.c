#include "game_core.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
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

bool game_core_damage_player(GameCore *core, int damage)
{
    if (core == NULL || damage <= 0 || core->health <= 0) return false;
    core->health = damage >= core->health ? 0 : core->health - damage;
    return true;
}

void game_core_restore_player(GameCore *core)
{
    if (core != NULL) core->health = GAME_CORE_MAX_HEALTH;
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
            || core->health < 0 || core->health > GAME_CORE_MAX_HEALTH
            || core->random_state == 0
            || core->quest_count > GAME_CORE_MAX_VALUES
            || core->inventory_count > GAME_CORE_MAX_VALUES) return false;
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
    bool valid = fprintf(file, "ANDORTRAIL_SAVE 2\ngold %d\nhealth %d\nrandom %" PRIu32 "\n",
        core->gold, core->health, core->random_state) >= 0;
    for (size_t index = 0; valid && index < core->quest_count; ++index) {
        valid = valid_id(core->quests[index].id) && core->quests[index].value > 0
            && fprintf(file, "quest %s %d\n", core->quests[index].id,
                core->quests[index].value) >= 0;
    }
    for (size_t index = 0; valid && index < core->inventory_count; ++index) {
        valid = valid_id(core->inventory[index].id) && core->inventory[index].value > 0
            && fprintf(file, "item %s %d\n", core->inventory[index].id,
                core->inventory[index].value) >= 0;
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

    GameCore loaded;
    game_core_init(&loaded);
    char line[160];
    bool valid = fgets(line, sizeof(line), file) != NULL;
    bool version_two = false;
    if (valid && strcmp(line, "ANDORTRAIL_SAVE 2\n") == 0) {
        version_two = true;
    } else if (!valid || strcmp(line, "ANDORTRAIL_SAVE 1\n") != 0) {
        valid = false;
    }
    bool gold_seen = false;
    bool health_seen = !version_two;
    bool random_seen = !version_two;
    while (valid && fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        if (length == 0) continue;

        int value = 0;
        int parsed_length = -1;
        char id[GAME_CORE_ID_CAPACITY];
        if (sscanf(line, "gold %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (gold_seen || value < 0) valid = false;
            else {
                loaded.gold = value;
                gold_seen = true;
            }
        } else if (sscanf(line, "health %d%n", &value, &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (!version_two || health_seen || value < 0 || value > GAME_CORE_MAX_HEALTH) {
                valid = false;
            } else {
                loaded.health = value;
                health_seen = true;
            }
        } else if (sscanf(line, "random %" SCNu32 "%n", &loaded.random_state,
                    &parsed_length) == 1
                && line_finished(line, parsed_length)) {
            if (!version_two || random_seen || loaded.random_state == 0) valid = false;
            else random_seen = true;
        } else if (sscanf(line, "quest %63s %d%n", id, &value, &parsed_length) == 2
                && line_finished(line, parsed_length) && valid_id(id) && value > 0) {
            if (find_value(loaded.quests, loaded.quest_count, id) != NULL
                    || !set_value(loaded.quests, &loaded.quest_count, id, value)) valid = false;
        } else if (sscanf(line, "item %63s %d%n", id, &value, &parsed_length) == 2
                && line_finished(line, parsed_length) && valid_id(id)
                && strcmp(id, "gold") != 0 && value > 0) {
            if (find_value(loaded.inventory, loaded.inventory_count, id) != NULL
                    || !set_value(loaded.inventory, &loaded.inventory_count, id, value)) valid = false;
        } else {
            valid = false;
        }
    }
    if (ferror(file) || !gold_seen || !health_seen || !random_seen) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid) return GAME_CORE_LOAD_ERROR;

    *core = loaded;
    return GAME_CORE_LOAD_OK;
}