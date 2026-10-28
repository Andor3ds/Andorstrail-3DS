#include "game_combat.hpp"

#include "game_items.hpp"

#include <cmath>
#include <climits>
#include <cstdint>
#include <memory>
#include <new>
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

bool read_optional_number(GameJsonRef object, const char *key, double default_value,
    double &destination)
{
    const GameJsonRef value = game_json_object_get(object, key);
    if (value.node == GAME_JSON_NO_NODE || game_json_is_null(value)) {
        destination = default_value;
        return true;
    }
    return game_json_number(value, &destination) && std::isfinite(destination)
        && destination >= 0.0;
}

bool read_optional_integer(GameJsonRef object, const char *key, int default_value,
    int &destination)
{
    const GameJsonRef value = game_json_object_get(object, key);
    if (value.node == GAME_JSON_NO_NODE || game_json_is_null(value)) {
        destination = default_value;
        return true;
    }
    return read_integer(value, destination);
}

bool calculate_experience(GameJsonRef monster, int maximum_health, int damage_minimum,
    int damage_maximum, int attack_chance, int &experience)
{
    int maximum_action_points = 10;
    int attack_cost = 0;
    int critical_skill = 0;
    int block_chance = 0;
    double critical_multiplier = 1.0;
    double damage_resistance = 0.0;
    if (!read_optional_integer(monster, "maxAP", 10, maximum_action_points)
            || !read_optional_integer(monster, "attackCost", 0, attack_cost)
            || !read_optional_integer(monster, "criticalSkill", 0, critical_skill)
            || !read_optional_integer(monster, "blockChance", 0, block_chance)
            || !read_optional_number(monster, "criticalMultiplier", 1.0, critical_multiplier)
            || !read_optional_number(monster, "damageResistance", 0.0, damage_resistance)
            || maximum_action_points <= 0 || attack_cost <= 0 || attack_chance > 100) {
        return false;
    }

    const double attacks_per_round = std::floor(
        static_cast<double>(maximum_action_points) / attack_cost
    );
    const double average_attack_health = attacks_per_round * attack_chance / 100.0
        * (damage_minimum + damage_maximum) / 2.0
        * (1.0 + critical_skill / 100.0 * critical_multiplier);
    const double average_defense_health = maximum_health * (1.0 + block_chance / 100.0)
        + 9.0 * damage_resistance;
    const double calculated = std::ceil((average_attack_health * 3.0
        + average_defense_health) * 0.7);
    if (!std::isfinite(calculated) || calculated < 0.0 || calculated > INT_MAX) return false;
    experience = static_cast<int>(calculated);
    return true;
}

int attack_hit_chance(int attack_chance, int block_chance)
{
    const float two_divided_by_pi = 2.0f / 3.14159265358979323846f;
    const float adjusted = (static_cast<float>(attack_chance)
        - static_cast<float>(block_chance) - 50.0f) / 40.0f;
    return static_cast<int>(50.0f * (1.0f + two_divided_by_pi * std::atan(adjusted)));
}

int rolled_damage(GameCore *core, int minimum, int maximum)
{
    if (maximum == minimum) return maximum;
    const uint32_t range = static_cast<uint32_t>(
        static_cast<int64_t>(maximum) - minimum + 1
    );
    return minimum + static_cast<int>(game_core_random(core, range));
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
    int block_chance = 0;
    int damage_resistance = 0;
    int experience = 0;
    const GameJsonRef damage = game_json_object_get(monster, "attackDamage");
    const bool valid_stats =
        read_integer(game_json_object_get(monster, "maxHP"), maximum_health)
        && read_integer(game_json_object_get(damage, "min"), damage_minimum)
        && read_integer(game_json_object_get(damage, "max"), damage_maximum)
        && read_integer(game_json_object_get(monster, "attackChance"), attack_chance)
        && read_optional_integer(monster, "blockChance", 0, block_chance)
        && read_optional_integer(monster, "damageResistance", 0, damage_resistance)
        && calculate_experience(monster, maximum_health, damage_minimum,
            damage_maximum, attack_chance, experience);
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

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "combat state could not be allocated";
        return false;
    }
    *previous_state = *core;
    const bool player_hit = game_core_random(core, 100)
        < static_cast<std::uint32_t>(attack_hit_chance(core->attack_chance, block_chance));
    int damage_dealt = 0;
    if (player_hit) {
        damage_dealt = player_damage - damage_resistance;
        if (damage_dealt < 0) damage_dealt = 0;
        enemy_health = damage_dealt >= enemy_health ? 0 : enemy_health - damage_dealt;
    }
    if (!game_core_set_quest_progress(core, health_id.c_str(), enemy_health)) {
        *core = *previous_state;
        error = "monster health could not be saved: " + instance_id;
        return false;
    }

    result.enemy_health = enemy_health;
    result.enemy_max_health = maximum_health;
    result.damage_dealt = damage_dealt;
    result.damage_taken = 0;
    result.experience_gained = 0;
    result.player_hit = player_hit;
    result.enemy_defeated = enemy_health == 0;
    result.player_knocked_out = false;
    if (result.enemy_defeated) {
        if (!game_core_set_quest_progress(core, defeated_id.c_str(), 1)
                || !items::apply_drop_list(resources, core, drop_list_id, error)
                || !game_core_add_experience(core, experience)) {
            *core = *previous_state;
            if (error.empty()) error = "monster defeat could not be recorded: " + instance_id;
            return false;
        }
        result.experience_gained = experience;
        return true;
    }

    const bool enemy_hits = game_core_random(core, 100)
        < static_cast<std::uint32_t>(attack_hit_chance(attack_chance, core->block_chance));
    if (!enemy_hits) return true;
    const int damage_taken = rolled_damage(core, damage_minimum, damage_maximum);
    if (damage_taken > 0 && !game_core_damage_player(core, damage_taken)) {
        *core = *previous_state;
        error = "player damage could not be applied";
        return false;
    }
    result.damage_taken = damage_taken;
    result.player_knocked_out = core->health == 0;
    return true;
}

}
}
