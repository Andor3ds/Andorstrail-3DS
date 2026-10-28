#include "game_combat.hpp"

#include "game_items.hpp"

#include <climits>
#include <cstdint>
#include <string>

namespace andortrail {
namespace combat {
namespace {

bool read_integer(GameJsonRef value, int &destination)
{
    int64_t parsed = 0;
    if (!game_json_integer(value, &parsed) || parsed < 0 || parsed > INT_MAX) return false;
    destination = static_cast<int>(parsed);
    return true;
}

}

bool attack_monster(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error
)
{
    error.clear();
    if (resources == NULL || core == NULL || monster_id.empty() || drop_list_id.empty()
            || instance_id.empty() || player_damage <= 0) {
        error = "invalid combat configuration";
        return false;
    }
    if (core->health <= 0) {
        error = "player cannot attack while knocked out";
        return false;
    }

    const std::string health_id = instance_id + "_health";
    const std::string defeated_id = instance_id + "_defeated";
    if (game_core_quest_progress(core, defeated_id.c_str()) > 0) {
        error = "monster is already defeated: " + instance_id;
        return false;
    }

    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_MONSTER, monster_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "monster record not found: " + monster_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef monster;
    if (!game_resources_open_record(resources, record, &document, &monster)) {
        error = "monster record could not be read: " + monster_id;
        return false;
    }
    int maximum_health = 0;
    int damage_minimum = 0;
    int damage_maximum = 0;
    int attack_chance = 0;
    const GameJsonRef damage = game_json_object_get(monster, "attackDamage");
    const bool valid_stats =
        read_integer(game_json_object_get(monster, "maxHP"), maximum_health)
        && read_integer(game_json_object_get(damage, "min"), damage_minimum)
        && read_integer(game_json_object_get(damage, "max"), damage_maximum)
        && read_integer(game_json_object_get(monster, "attackChance"), attack_chance);
    game_json_dispose(&document);
    if (!valid_stats || maximum_health <= 0 || damage_maximum < damage_minimum
            || attack_chance > 100) {
        error = "monster combat data is invalid: " + monster_id;
        return false;
    }

    int enemy_health = game_core_quest_progress(core, health_id.c_str());
    if (enemy_health == 0) enemy_health = maximum_health;
    if (enemy_health > maximum_health) {
        error = "saved monster health exceeds its maximum: " + instance_id;
        return false;
    }

    const GameCore previous_state = *core;
    enemy_health = player_damage >= enemy_health ? 0 : enemy_health - player_damage;
    if (!game_core_set_quest_progress(core, health_id.c_str(), enemy_health)) {
        error = "monster health could not be saved: " + instance_id;
        return false;
    }

    result.enemy_health = enemy_health;
    result.enemy_max_health = maximum_health;
    result.damage_taken = 0;
    result.enemy_defeated = enemy_health == 0;
    result.player_knocked_out = false;
    if (result.enemy_defeated) {
        if (!game_core_set_quest_progress(core, defeated_id.c_str(), 1)
                || !items::apply_drop_list(resources, core, drop_list_id, error)) {
            *core = previous_state;
            if (error.empty()) error = "monster defeat could not be recorded: " + instance_id;
            return false;
        }
        return true;
    }

    const bool enemy_hits = game_core_random(core, 100)
        < static_cast<std::uint32_t>(attack_chance);
    if (!enemy_hits) return true;
    const int64_t damage_range =
        static_cast<int64_t>(damage_maximum) - damage_minimum + 1;
    const int damage_taken = damage_minimum + static_cast<int>(
        game_core_random(core, static_cast<std::uint32_t>(damage_range))
    );
    if (damage_taken > 0 && !game_core_damage_player(core, damage_taken)) {
        *core = previous_state;
        error = "player damage could not be applied";
        return false;
    }
    result.damage_taken = damage_taken;
    result.player_knocked_out = core->health == 0;
    return true;
}

}
}
