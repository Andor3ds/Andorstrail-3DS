#include "game_combat.hpp"

#include "game_items.hpp"
#include "game_skills.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <cstdlib>

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

bool parse_condition_chance(const char *text, int &maximum, int &current)
{
    if (text == NULL || text[0] == '\0') return false;
    const char *separator = std::strchr(text, '/');
    char *end = NULL;
    if (separator == NULL) {
        const long parsed = std::strtol(text, &end, 10);
        if (end == text || *end != '\0' || parsed < 0 || parsed > 100) {
            return false;
        }
        if (parsed == 100) {
            maximum = 1;
            current = 1;
        } else {
            maximum = 100;
            current = static_cast<int>(parsed);
        }
    } else {
        const long parsed_current = std::strtol(text, &end, 10);
        if (end != separator || parsed_current < 0 || parsed_current > INT_MAX) {
            return false;
        }
        const long parsed_maximum = std::strtol(separator + 1, &end, 10);
        if (end == separator + 1 || *end != '\0'
                || parsed_maximum <= 0 || parsed_maximum > INT_MAX
                || parsed_current > parsed_maximum) {
            return false;
        }
        maximum = static_cast<int>(parsed_maximum);
        current = static_cast<int>(parsed_current);
    }
    return maximum > 0 && current <= maximum;
}

bool roll_condition_chance(GameCore *core, int maximum, int current,
    std::int64_t bias, bool &success)
{
    const std::int64_t total = static_cast<std::int64_t>(maximum) * 100;
    if (maximum <= 0 || current < 0 || current > maximum
            || total > UINT32_MAX) {
        return false;
    }
    const std::int64_t threshold = std::max<std::int64_t>(0,
        std::min(total, static_cast<std::int64_t>(current) * 100 + bias));
    success = game_core_random(core, static_cast<std::uint32_t>(total))
        < static_cast<std::uint32_t>(threshold);
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

bool read_optional_signed_integer(GameJsonRef object, const char *key,
    int default_value, int &destination)
{
    const GameJsonRef value = game_json_object_get(object, key);
    if (value.node == GAME_JSON_NO_NODE || game_json_is_null(value)) {
        destination = default_value;
        return true;
    }
    int64_t parsed = 0;
    if (!game_json_integer(value, &parsed) || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

void add_saturated(int &value, int delta)
{
    const int64_t sum = static_cast<int64_t>(value) + delta;
    value = sum > INT_MAX ? INT_MAX : sum < INT_MIN ? INT_MIN
        : static_cast<int>(sum);
}

void add_scaled(int &value, int amount, int multiplier)
{
    const int64_t sum = static_cast<int64_t>(value)
        + static_cast<int64_t>(amount) * multiplier;
    value = sum > INT_MAX ? INT_MAX : sum < INT_MIN ? INT_MIN
        : static_cast<int>(sum);
}

struct AbilityBonuses {
    int max_health;
    int max_action_points;
    int move_cost;
    int attack_cost;
    int use_item_cost;
    int reequip_cost;
    int attack_chance;
    int minimum_damage;
    int maximum_damage;
    int critical_skill;
    double critical_multiplier;
    bool has_critical_multiplier;
    int block_chance;
    int damage_resistance;
    int non_weapon_damage_modifier;
    bool has_non_weapon_damage_modifier;
};

bool read_ability_bonuses(GameJsonRef object, AbilityBonuses &bonuses)
{
    if (object.node == GAME_JSON_NO_NODE || game_json_is_null(object)) return true;
    const GameJsonRef attack_damage = game_json_object_get(object, "increaseAttackDamage");
    const GameJsonRef critical_multiplier =
        game_json_object_get(object, "setCriticalMultiplier");
    const GameJsonRef non_weapon_damage_modifier =
        game_json_object_get(object, "setNonWeaponDamageModifier");
    bonuses.has_critical_multiplier =
        critical_multiplier.node != GAME_JSON_NO_NODE
        && !game_json_is_null(critical_multiplier);
    bonuses.has_non_weapon_damage_modifier =
        non_weapon_damage_modifier.node != GAME_JSON_NO_NODE
        && !game_json_is_null(non_weapon_damage_modifier);
    return read_optional_signed_integer(object, "increaseMaxHP", 0, bonuses.max_health)
        && read_optional_signed_integer(object, "increaseMaxAP", 0, bonuses.max_action_points)
        && read_optional_signed_integer(object, "increaseMoveCost", 0, bonuses.move_cost)
        && read_optional_signed_integer(object, "increaseAttackCost", 0, bonuses.attack_cost)
        && read_optional_signed_integer(object, "increaseUseItemCost", 0, bonuses.use_item_cost)
        && read_optional_signed_integer(object, "increaseReequipCost", 0, bonuses.reequip_cost)
        && read_optional_signed_integer(object, "increaseAttackChance", 0, bonuses.attack_chance)
        && read_optional_signed_integer(attack_damage, "min", 0, bonuses.minimum_damage)
        && read_optional_signed_integer(attack_damage, "max", 0, bonuses.maximum_damage)
        && read_optional_signed_integer(object, "increaseCriticalSkill", 0, bonuses.critical_skill)
        && read_optional_number(object, "setCriticalMultiplier", 0.0,
            bonuses.critical_multiplier)
        && read_optional_signed_integer(object, "increaseBlockChance", 0, bonuses.block_chance)
        && read_optional_signed_integer(object, "increaseDamageResistance", 0,
            bonuses.damage_resistance)
        && read_optional_signed_integer(object, "setNonWeaponDamageModifier", -1,
            bonuses.non_weapon_damage_modifier);
}

bool add_condition_ability(GameResources *resources, const std::string &condition_id,
    int magnitude, AbilityBonuses &bonuses, std::string &error)
{
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONDITION, condition_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "condition record not found: " + condition_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef condition;
    if (!game_resources_open_record(resources, record, &document, &condition)) {
        error = "condition record could not be read: " + condition_id;
        return false;
    }
    AbilityBonuses effect = {};
    const bool valid = read_ability_bonuses(
        game_json_object_get(condition, "abilityEffect"), effect
    );
    game_json_dispose(&document);
    if (!valid) {
        error = "condition ability data is invalid: " + condition_id;
        return false;
    }
    add_scaled(bonuses.max_health, effect.max_health, magnitude);
    add_scaled(bonuses.max_action_points, effect.max_action_points, magnitude);
    add_scaled(bonuses.move_cost, effect.move_cost, magnitude);
    add_scaled(bonuses.attack_cost, effect.attack_cost, magnitude);
    add_scaled(bonuses.use_item_cost, effect.use_item_cost, magnitude);
    add_scaled(bonuses.reequip_cost, effect.reequip_cost, magnitude);
    add_scaled(bonuses.attack_chance, effect.attack_chance, magnitude);
    add_scaled(bonuses.minimum_damage, effect.minimum_damage, magnitude);
    add_scaled(bonuses.maximum_damage, effect.maximum_damage, magnitude);
    add_scaled(bonuses.critical_skill, effect.critical_skill, magnitude);
    if (effect.has_critical_multiplier) {
        bonuses.critical_multiplier = effect.critical_multiplier;
        bonuses.has_critical_multiplier = true;
    }
    add_scaled(bonuses.block_chance, effect.block_chance, magnitude);
    add_scaled(bonuses.damage_resistance, effect.damage_resistance, magnitude);
    return true;
}

bool collect_equipment_immunities(GameResources *resources, const GameCore *core,
    std::vector<std::string> &immunities, std::string &error)
{
    for (std::size_t index = 0; index < core->condition_count; ++index) {
        const GameCoreCondition &condition = core->conditions[index];
        if (condition.immunity) immunities.push_back(condition.id);
    }
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *item_id = core->equipment[slot];
        if (item_id[0] == '\0') continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + item_id;
            return false;
        }
        GameJsonDocument document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &document, &item)) {
            error = std::string("equipped item record could not be read: ") + item_id;
            return false;
        }
        const GameJsonRef added = game_json_object_get(
            game_json_object_get(item, "equipEffect"), "addedConditions"
        );
        bool valid = true;
        for (std::size_t condition_index = 0;
                valid && condition_index < game_json_array_size(added);
                ++condition_index) {
            const GameJsonRef effect = game_json_array_get(added, condition_index);
            char condition_id[GAME_CORE_ID_CAPACITY];
            int magnitude = 1;
            int duration = 0;
            valid = game_json_string_copy(game_json_object_get(effect, "condition"),
                    condition_id, sizeof(condition_id))
                && read_optional_signed_integer(effect, "magnitude", 1, magnitude)
                && read_optional_signed_integer(effect, "duration", 0, duration);
            if (valid && magnitude == -99 && duration != 0) {
                immunities.push_back(condition_id);
            }
        }
        game_json_dispose(&document);
        if (!valid) {
            error = std::string("equipped item condition data is invalid: ") + item_id;
            return false;
        }
    }
    return true;
}

bool calculate_experience(GameJsonRef monster, int maximum_health, int damage_minimum,
    int damage_maximum, int attack_chance, int &experience)
{
    int maximum_action_points = 10;
    int attack_cost = 10;
    int critical_skill = 0;
    int block_chance = 0;
    double critical_multiplier = 0.0;
    double damage_resistance = 0.0;
    if (!read_optional_integer(monster, "maxAP", 10, maximum_action_points)
            || !read_optional_integer(monster, "attackCost", 10, attack_cost)
            || !read_optional_integer(monster, "criticalSkill", 0, critical_skill)
            || !read_optional_integer(monster, "blockChance", 0, block_chance)
            || !read_optional_number(monster, "criticalMultiplier", 0.0, critical_multiplier)
            || !read_optional_number(monster, "damageResistance", 0.0, damage_resistance)
            || maximum_action_points <= 0 || attack_cost <= 0) {
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
    const GameJsonRef hit_effect = game_json_object_get(monster, "hitEffect");
    const GameJsonRef target_conditions = game_json_object_get(
        hit_effect, "conditionsTarget"
    );
    const double condition_bonus = game_json_array_size(target_conditions) > 0
        ? 50.0 : 0.0;
    const double calculated = std::ceil((average_attack_health * 3.0
        + average_defense_health) * 0.7) + condition_bonus;
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

int apply_percentage(int value, int percent)
{
    const int64_t result = static_cast<int64_t>(value)
        + static_cast<int64_t>(value) * percent / 100;
    return result > INT_MAX ? INT_MAX : static_cast<int>(result);
}

int effective_critical_chance(int critical_skill)
{
    if (critical_skill <= 0) return 0;
    const double chance = -5.0 + 2.0 * std::sqrt(5.0 * critical_skill);
    return chance > 0.0 ? static_cast<int>(chance) : 0;
}

bool has_critical_attacks(int critical_skill, double critical_multiplier)
{
    return critical_skill > 0 && critical_multiplier != 0.0
        && critical_multiplier != 1.0;
}

bool immune_to_critical_hits(GameJsonRef monster)
{
    const GameJsonRef monster_class = game_json_object_get(monster, "monsterClass");
    return game_json_string_equals(monster_class, "ghost")
        || game_json_string_equals(monster_class, "construct")
        || game_json_string_equals(monster_class, "demon");
}

int critical_damage(int damage, double critical_multiplier)
{
    const double multiplied = static_cast<double>(damage) * critical_multiplier;
    if (multiplied >= INT_MAX) return INT_MAX;
    return static_cast<int>(multiplied);
}

bool experience_with_skill_bonus(int base_experience, int skill_level, int &experience)
{
    const int64_t percent = static_cast<int64_t>(skill_level) * 10;
    const int64_t bonus = static_cast<int64_t>(base_experience / 100) * percent
        + static_cast<int64_t>(base_experience % 100) * percent / 100;
    const int64_t total = static_cast<int64_t>(base_experience) + bonus;
    if (total > INT_MAX) return false;
    experience = static_cast<int>(total);
    return true;
}

bool read_effect_range(GameJsonRef effect, const char *key, int &minimum,
    int &maximum)
{
    const GameJsonRef range = game_json_object_get(effect, key);
    if (range.node == GAME_JSON_NO_NODE || game_json_is_null(range)) {
        minimum = 0;
        maximum = 0;
        return true;
    }
    return read_optional_signed_integer(range, "min", 0, minimum)
        && read_optional_signed_integer(range, "max", minimum, maximum)
        && maximum >= minimum;
}

int rolled_signed_value(GameCore *core, int minimum, int maximum)
{
    if (minimum == maximum) return minimum;
    const std::uint64_t range = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(maximum) - minimum + 1
    );
    if (range > UINT32_MAX) return minimum;
    return minimum + static_cast<int>(game_core_random(core,
        static_cast<std::uint32_t>(range)));
}

bool apply_condition_effect(GameResources *resources, GameCore *core,
    GameJsonRef effect, std::string &error)
{
    char condition_id[GAME_CORE_ID_CAPACITY];
    if (!game_json_string_copy(game_json_object_get(effect, "condition"),
            condition_id, sizeof(condition_id))) {
        error = "condition effect has an invalid condition id";
        return false;
    }
    int magnitude = 1;
    int duration = 0;
    if (!read_optional_signed_integer(effect, "magnitude", 1, magnitude)
            || !read_optional_signed_integer(effect, "duration", 0, duration)) {
        error = std::string("condition effect is invalid: ") + condition_id;
        return false;
    }
    int chance_maximum = 1;
    int chance_current = 1;
    const GameJsonRef chance_value = game_json_object_get(effect, "chance");
    if (chance_value.node != GAME_JSON_NO_NODE && !game_json_is_null(chance_value)) {
        char chance_text[32];
        if (!game_json_string_copy(chance_value, chance_text, sizeof(chance_text))) {
            error = std::string("condition chance is invalid: ") + condition_id;
            return false;
        }
        if (!parse_condition_chance(chance_text, chance_maximum,
                chance_current)) {
            error = std::string("condition chance is invalid: ") + condition_id;
            return false;
        }
    }

    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONDITION, condition_id, NULL
    );
    if (record == NULL) {
        error = std::string("condition record not found: ") + condition_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef condition;
    if (!game_resources_open_record(resources, record, &document, &condition)) {
        error = std::string("condition record could not be read: ") + condition_id;
        return false;
    }
    int stacking_value = 0;
    char category[32] = {};
    const bool valid = read_optional_integer(condition, "isStacking", 0,
            stacking_value)
        && game_json_string_copy(game_json_object_get(condition, "category"),
            category, sizeof(category));
    game_json_dispose(&document);
    if (!valid) {
        error = std::string("condition data is invalid: ") + condition_id;
        return false;
    }

    std::int64_t chance_bias = 0;
    const std::int64_t chance_value_scaled =
        static_cast<std::int64_t>(chance_current) * 100;
    if (chance_current < chance_maximum) {
        const auto apply_resistance = [&](const char *skill_id, int per_level) {
            const int level = game_core_skill_level(core, skill_id);
            if (level <= 0 || chance_value_scaled == 0) return;
            const std::int64_t reduction_per_level =
                static_cast<std::int64_t>(chance_current) * per_level;
            const std::int64_t available = chance_value_scaled + chance_bias;
            if (reduction_per_level > 0
                    && level >= (available + reduction_per_level - 1)
                        / reduction_per_level) {
                chance_bias = -chance_value_scaled;
            } else {
                chance_bias -= reduction_per_level * level;
            }
        };
        apply_resistance("shadowBless", 5);
        if (std::strcmp(category, "mental") == 0) {
            apply_resistance("resistanceMental", 10);
        } else if (std::strcmp(category, "physical") == 0) {
            apply_resistance("resistancePhysical", 10);
        } else if (std::strcmp(category, "blood") == 0) {
            apply_resistance("resistanceBlood", 10);
        }
        if (std::strcmp(condition_id, "spore_poison") == 0) {
            apply_resistance("sporeImmunity", 100);
        }
    }
    bool chance_succeeded = false;
    if (!roll_condition_chance(core, chance_maximum, chance_current,
            chance_bias, chance_succeeded)) {
        error = std::string("condition chance range is unsupported: ")
            + condition_id;
        return false;
    }
    if (!chance_succeeded) {
        return true;
    }
    if (magnitude > 0) {
        std::vector<std::string> immunities;
        if (!collect_equipment_immunities(resources, core, immunities, error)) {
            return false;
        }
        if (std::find(immunities.begin(), immunities.end(), condition_id)
                != immunities.end()) return true;
    }
    if (magnitude == -99 && duration == 0) {
        return game_core_remove_active_condition(core, condition_id);
    }
    if (magnitude <= 0 && magnitude != -99) {
        error = std::string("condition effect magnitude is invalid: ")
            + condition_id;
        return false;
    }
    if (duration <= 0) {
        error = std::string("condition duration is invalid: ") + condition_id;
        return false;
    }
    return game_core_add_condition(core, condition_id, magnitude, duration,
        stacking_value != 0, magnitude == -99);
}

bool condition_ability_bonuses(GameResources *resources,
    const std::vector<ConditionState> &conditions, AbilityBonuses &bonuses,
    std::string &error)
{
    for (const ConditionState &condition : conditions) {
        if (condition.immunity) continue;
        bool immune = false;
        for (const ConditionState &candidate : conditions) {
            if (candidate.immunity && candidate.id == condition.id) {
                immune = true;
                break;
            }
        }
        if (!immune && !add_condition_ability(resources, condition.id,
                condition.magnitude, bonuses, error)) return false;
    }
    return true;
}

bool apply_enemy_condition_data(GameResources *resources, GameCore *random_core,
    std::vector<ConditionState> &conditions, const char *condition_id,
    int magnitude, int duration, int chance_maximum, int chance_current,
    std::string &error);

bool apply_enemy_condition_effect(GameResources *resources, GameCore *random_core,
    std::vector<ConditionState> &conditions, GameJsonRef effect,
    std::string &error)
{
    char condition_id[GAME_CORE_ID_CAPACITY];
    int magnitude = 1;
    int duration = 0;
    if (!game_json_string_copy(game_json_object_get(effect, "condition"),
            condition_id, sizeof(condition_id))
            || !read_optional_signed_integer(effect, "magnitude", 1, magnitude)
            || !read_optional_signed_integer(effect, "duration", 0, duration)) {
        error = "enemy condition effect is invalid";
        return false;
    }
    int chance_maximum = 1;
    int chance_current = 1;
    const GameJsonRef chance_value = game_json_object_get(effect, "chance");
    if (chance_value.node != GAME_JSON_NO_NODE && !game_json_is_null(chance_value)) {
        char chance_text[32];
        if (!game_json_string_copy(chance_value, chance_text, sizeof(chance_text))) {
            error = std::string("enemy condition chance is invalid: ") + condition_id;
            return false;
        }
        if (!parse_condition_chance(chance_text, chance_maximum,
                chance_current)) {
            error = std::string("enemy condition chance is invalid: ") + condition_id;
            return false;
        }
    }
    return apply_enemy_condition_data(resources, random_core, conditions,
        condition_id, magnitude, duration, chance_maximum, chance_current,
        error);
}

bool apply_enemy_condition_data(GameResources *resources, GameCore *random_core,
    std::vector<ConditionState> &conditions, const char *condition_id,
    int magnitude, int duration, int chance_maximum, int chance_current,
    std::string &error)
{
    bool chance_succeeded = false;
    if (!roll_condition_chance(random_core, chance_maximum, chance_current,
            0, chance_succeeded)) {
        error = std::string("enemy condition chance range is unsupported: ")
            + condition_id;
        return false;
    }
    if (!chance_succeeded) {
        return true;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONDITION, condition_id, NULL
    );
    if (record == NULL) {
        error = std::string("condition record not found: ") + condition_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef condition;
    if (!game_resources_open_record(resources, record, &document, &condition)) {
        error = std::string("condition record could not be read: ") + condition_id;
        return false;
    }
    int stacking = 0;
    const bool valid = read_optional_integer(condition, "isStacking", 0, stacking);
    game_json_dispose(&document);
    if (!valid) {
        error = std::string("condition data is invalid: ") + condition_id;
        return false;
    }

    if (magnitude == -99) {
        for (std::size_t index = 0; index < conditions.size();) {
            if (conditions[index].id == condition_id) {
                conditions.erase(conditions.begin() + index);
            } else {
                ++index;
            }
        }
        if (duration > 0) {
            ConditionState immunity = {condition_id, -99, duration, true};
            conditions.push_back(immunity);
        }
        return true;
    }
    for (std::size_t index = 0; index < conditions.size(); ++index) {
        if (conditions[index].immunity
                && conditions[index].id == condition_id) return true;
    }
    if (magnitude <= 0 || duration <= 0) {
        error = std::string("enemy condition duration or magnitude is invalid: ")
            + condition_id;
        return false;
    }
    if (stacking != 0) {
        for (ConditionState &active : conditions) {
            if (active.id == condition_id && active.duration == duration
                    && !active.immunity) {
                if (active.magnitude > INT_MAX - magnitude) {
                    error = "enemy condition magnitude exceeded supported bounds";
                    return false;
                }
                active.magnitude += magnitude;
                return true;
            }
        }
    } else {
        for (ConditionState &active : conditions) {
            if (active.id != condition_id || active.immunity) continue;
            if (active.magnitude > magnitude
                    || (active.magnitude == magnitude
                        && active.duration >= duration)) return true;
            active.magnitude = magnitude;
            active.duration = duration;
            return true;
        }
    }
    if (conditions.size() >= GAME_CORE_MAX_CONDITIONS) {
        error = "enemy condition limit exceeded";
        return false;
    }
    ConditionState added = {condition_id, magnitude, duration, false};
    conditions.push_back(added);
    return true;
}

bool apply_monster_condition_effects(GameResources *resources, GameCore *core,
    const std::string &monster_id, const char *effect_name,
    const char *condition_field, std::string &error)
{
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
    const GameJsonRef effect = game_json_object_get(monster, effect_name);
    const GameJsonRef conditions = game_json_object_get(effect, condition_field);
    bool success = true;
    for (std::size_t index = 0; success
            && index < game_json_array_size(conditions); ++index) {
        success = apply_condition_effect(resources, core,
            game_json_array_get(conditions, index), error);
    }
    game_json_dispose(&document);
    return success;
}

bool apply_equipment_condition_effects(GameResources *resources, GameCore *core,
    const char *effect_name, const char *condition_field, std::string &error)
{
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *item_id = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (item_id == NULL) continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + item_id;
            return false;
        }
        GameJsonDocument document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &document, &item)) {
            error = std::string("equipped item record could not be read: ") + item_id;
            return false;
        }
        const GameJsonRef effect = game_json_object_get(item, effect_name);
        const GameJsonRef conditions = game_json_object_get(effect, condition_field);
        bool success = true;
        for (std::size_t index = 0; success
                && index < game_json_array_size(conditions); ++index) {
            success = apply_condition_effect(resources, core,
                game_json_array_get(conditions, index), error);
        }
        game_json_dispose(&document);
        if (!success) return false;
    }
    return true;
}

struct EffectActor {
    bool is_player;
    int *health;
    int maximum_health;
    int *action_point_change;
};

bool apply_stat_effect_fields(GameResources *resources, GameCore *core,
    GameJsonRef effect, const char *health_field, const char *action_points_field,
    EffectActor &actor, std::string &error)
{
    int hp_minimum = 0;
    int hp_maximum = 0;
    int ap_minimum = 0;
    int ap_maximum = 0;
    if (!read_effect_range(effect, health_field, hp_minimum, hp_maximum)
            || !read_effect_range(effect, action_points_field, ap_minimum,
                ap_maximum)) {
        error = "combat stat effect has an invalid range";
        return false;
    }

    const int ap_delta = rolled_signed_value(core, ap_minimum, ap_maximum);
    if (ap_delta != 0) {
        if (actor.action_point_change == NULL) {
            error = "combat stat effect has no action-point result target";
            return false;
        }
        add_saturated(*actor.action_point_change, ap_delta);
    }

    const int hp_delta = rolled_signed_value(core, hp_minimum, hp_maximum);
    if (hp_delta != 0 && actor.is_player) {
        const int64_t updated_health = static_cast<std::int64_t>(core->health)
            + hp_delta;
        PlayerCombatStats stats;
        if (!player_combat_stats(resources, core, stats, error)) return false;
        const int bounded_health = static_cast<int>(std::max<std::int64_t>(0,
            std::min<std::int64_t>(stats.maximum_health, updated_health)));
        if (bounded_health < core->health) {
            if (!game_core_damage_player(core, core->health - bounded_health)) {
                error = "combat stat effect could not damage the player";
                return false;
            }
        } else {
            core->health = bounded_health;
        }
    } else if (hp_delta != 0) {
        if (actor.health == NULL || actor.maximum_health <= 0) {
            error = "combat stat effect has no target health";
            return false;
        }
        const int64_t updated_health = static_cast<std::int64_t>(*actor.health)
            + hp_delta;
        *actor.health = static_cast<int>(std::max<std::int64_t>(0,
            std::min<std::int64_t>(actor.maximum_health, updated_health)));
    }

    return true;
}

bool apply_use_stat_effect(GameResources *resources, GameCore *core,
    GameJsonRef effect, EffectActor &source, std::string &error)
{
    return apply_stat_effect_fields(resources, core, effect,
        "increaseCurrentHP", "increaseCurrentAP", source, error);
}

bool apply_received_stat_effect(GameResources *resources, GameCore *core,
    GameJsonRef effect, EffectActor &receiver, EffectActor &attacker,
    std::string &error)
{
    return apply_stat_effect_fields(resources, core, effect,
            "increaseCurrentHP", "increaseCurrentAP", receiver, error)
        && apply_stat_effect_fields(resources, core, effect,
            "increaseAttackerCurrentHP", "increaseAttackerCurrentAP",
            attacker, error);
}

bool apply_record_stat_effect(GameResources *resources, GameCore *core,
    GameResourceType type, const std::string &id, const char *effect_name,
    bool received_effect, EffectActor &source_or_receiver,
    EffectActor &attacker, std::string &error)
{
    const GameResourceRecordEntry *record = game_resources_find(
        type, id.c_str(), NULL
    );
    if (record == NULL) {
        error = "combat effect record not found: " + id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef object;
    if (!game_resources_open_record(resources, record, &document, &object)) {
        error = "combat effect record could not be read: " + id;
        return false;
    }
    const GameJsonRef effect = game_json_object_get(object, effect_name);
    const bool success = received_effect
        ? apply_received_stat_effect(resources, core, effect,
            source_or_receiver, attacker, error)
        : apply_use_stat_effect(resources, core, effect,
            source_or_receiver, error);
    game_json_dispose(&document);
    return success;
}

bool apply_monster_stat_effect(GameResources *resources, GameCore *core,
    const std::string &monster_id, const char *effect_name,
    bool received_effect, EffectActor &source_or_receiver,
    EffectActor &attacker, std::string &error)
{
    return apply_record_stat_effect(resources, core, GAME_RESOURCE_MONSTER,
        monster_id, effect_name, received_effect, source_or_receiver,
        attacker, error);
}

bool apply_equipment_stat_effects(GameResources *resources, GameCore *core,
    const char *effect_name, bool received_effect,
    EffectActor &source_or_receiver, EffectActor &attacker,
    std::string &error)
{
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *item_id = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (item_id == NULL) continue;
        if (!apply_record_stat_effect(resources, core, GAME_RESOURCE_ITEM,
                item_id, effect_name, received_effect, source_or_receiver,
                attacker, error)) return false;
    }
    return true;
}

bool apply_actor_condition_effects(GameResources *resources, GameCore *core,
    std::vector<ConditionState> &enemy_conditions, GameJsonRef effect,
    const char *field, bool applies_to_player, std::string &error)
{
    const GameJsonRef effects = game_json_object_get(effect, field);
    for (std::size_t index = 0; index < game_json_array_size(effects); ++index) {
        const GameJsonRef condition_effect = game_json_array_get(effects, index);
        const bool success = applies_to_player
            ? apply_condition_effect(resources, core, condition_effect, error)
            : apply_enemy_condition_effect(resources, core, enemy_conditions,
                condition_effect, error);
        if (!success) return false;
    }
    return true;
}

bool apply_combat_effect_conditions(GameResources *resources, GameCore *core,
    std::vector<ConditionState> &enemy_conditions, GameJsonRef effect,
    bool source_is_player, std::string &error)
{
    return apply_actor_condition_effects(resources, core, enemy_conditions,
            effect, "conditionsSource", source_is_player, error)
        && apply_actor_condition_effects(resources, core, enemy_conditions,
            effect, "conditionsTarget", !source_is_player, error);
}

bool apply_equipment_combat_effects(GameResources *resources, GameCore *core,
    std::vector<ConditionState> &enemy_conditions, const char *effect_name,
    bool received_effect, EffectActor &player_actor, EffectActor &enemy_actor,
    std::string &error)
{
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *item_id = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (item_id == NULL) continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + item_id;
            return false;
        }
        GameJsonDocument document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &document, &item)) {
            error = std::string("equipped item record could not be read: ")
                + item_id;
            return false;
        }
        const GameJsonRef effect = game_json_object_get(item, effect_name);
        bool success = apply_combat_effect_conditions(resources, core,
            enemy_conditions, effect, true, error);
        if (success) {
            success = received_effect
                ? apply_received_stat_effect(resources, core, effect,
                    player_actor, enemy_actor, error)
                : apply_use_stat_effect(resources, core, effect,
                    player_actor, error);
        }
        game_json_dispose(&document);
        if (!success) return false;
    }
    return true;
}

bool apply_monster_combat_effects(GameResources *resources, GameCore *core,
    std::vector<ConditionState> &enemy_conditions, const std::string &monster_id,
    const char *effect_name, bool received_effect, EffectActor &enemy_actor,
    EffectActor &player_actor, std::string &error)
{
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
    const GameJsonRef effect = game_json_object_get(monster, effect_name);
    bool success = apply_combat_effect_conditions(resources, core,
        enemy_conditions, effect, false, error);
    if (success) {
        success = received_effect
            ? apply_received_stat_effect(resources, core, effect,
                enemy_actor, player_actor, error)
            : apply_use_stat_effect(resources, core, effect,
                enemy_actor, error);
    }
    game_json_dispose(&document);
    return success;
}

struct EquippedCombatItem {
    std::string category;
    std::string inventory_slot;
    std::string size;
    AbilityBonuses bonuses;
    bool present;
    bool has_equip_effect;
};

int percentage_of(int value, int positive_percent, int negative_percent)
{
    const int percent = value > 0 ? positive_percent
        : value < 0 ? negative_percent : 0;
    const double scaled = std::floor(static_cast<double>(value) * percent / 100.0);
    if (scaled >= INT_MAX) return INT_MAX;
    if (scaled <= INT_MIN) return INT_MIN;
    return static_cast<int>(scaled);
}

int percent_per_skill(int level, int percent)
{
    const int64_t result = static_cast<int64_t>(level) * percent;
    return result > INT_MAX ? INT_MAX : static_cast<int>(result);
}

int apply_non_weapon_damage_modifier(int total_damage, int weapon_damage,
    int modifier)
{
    const std::int64_t non_weapon_damage =
        static_cast<std::int64_t>(total_damage) - weapon_damage;
    const double scaled_non_weapon_damage = std::floor(
        static_cast<double>(non_weapon_damage) * modifier / 100.0 + 0.5);
    const double result = static_cast<double>(weapon_damage)
        + scaled_non_weapon_damage;
    return result > INT_MAX ? INT_MAX : result < INT_MIN ? INT_MIN
        : static_cast<int>(result);
}

bool load_equipped_combat_items(GameResources *resources, const GameCore *core,
    EquippedCombatItem (&items)[GAME_CORE_EQUIPMENT_SLOTS], std::string &error)
{
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *item_id = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (item_id == NULL) continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + item_id;
            return false;
        }
        GameJsonDocument item_document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &item_document, &item)) {
            error = std::string("equipped item record could not be read: ") + item_id;
            return false;
        }
        char category_id[GAME_CORE_ID_CAPACITY];
        const GameJsonRef equip_effect = game_json_object_get(item, "equipEffect");
        AbilityBonuses bonuses = {};
        const bool valid_item = game_json_string_copy(
                game_json_object_get(item, "category"),
                category_id, sizeof(category_id))
            && read_ability_bonuses(equip_effect, bonuses);
        game_json_dispose(&item_document);
        if (!valid_item) {
            error = std::string("equipped item data is invalid: ") + item_id;
            return false;
        }
        const GameResourceRecordEntry *category_record = game_resources_find(
            GAME_RESOURCE_ITEM_CATEGORY, category_id, NULL
        );
        if (category_record == NULL) {
            error = std::string("item category record not found: ") + category_id;
            return false;
        }
        GameJsonDocument category_document;
        GameJsonRef category;
        if (!game_resources_open_record(resources, category_record,
                &category_document, &category)) {
            error = std::string("item category record could not be read: ")
                + category_id;
            return false;
        }
        char inventory_slot[32];
        char size[16] = "none";
        const GameJsonRef category_size = game_json_object_get(category, "size");
        const bool valid_category = game_json_string_copy(
                game_json_object_get(category, "inventorySlot"),
                inventory_slot, sizeof(inventory_slot))
            && (category_size.node == GAME_JSON_NO_NODE
                || game_json_is_null(category_size)
                || game_json_string_copy(category_size, size, sizeof(size)));
        game_json_dispose(&category_document);
        if (!valid_category) {
            error = std::string("item category data is invalid: ") + category_id;
            return false;
        }
        items[slot].category = category_id;
        items[slot].inventory_slot = inventory_slot;
        items[slot].size = size;
        items[slot].bonuses = bonuses;
        items[slot].present = true;
        items[slot].has_equip_effect = equip_effect.node != GAME_JSON_NO_NODE
            && !game_json_is_null(equip_effect);
    }
    return true;
}

const char *weapon_proficiency_for_category(const std::string &category)
{
    if (category == "dagger" || category == "ssword") {
        return "weaponProficiencyDagger";
    }
    if (category == "lsword" || category == "bsword" || category == "rapier") {
        return "weaponProficiency1hsword";
    }
    if (category == "2hsword") return "weaponProficiency2hsword";
    if (category == "axe" || category == "axe2h") return "weaponProficiencyAxe";
    if (category == "club" || category == "staff" || category == "mace"
            || category == "scepter" || category == "hammer"
            || category == "hammer2h" || category == "whip") {
        return "weaponProficiencyBlunt";
    }
    if (category == "pole") return "weaponProficiencyPole";
    return NULL;
}

bool equipment_combat_skill_bonuses(GameResources *resources, const GameCore *core,
    AbilityBonuses &bonuses, std::string &error)
{
    EquippedCombatItem equipment[GAME_CORE_EQUIPMENT_SLOTS] = {};
    if (!load_equipped_combat_items(resources, core, equipment, error)) return false;
    const EquippedCombatItem &weapon = equipment[GAME_CORE_EQUIP_WEAPON];
    const EquippedCombatItem &shield = equipment[GAME_CORE_EQUIP_SHIELD];
    const char *proficiency_id = weapon.present
        ? weapon_proficiency_for_category(weapon.category) : NULL;
    const int weapon_level = proficiency_id == NULL ? 0
        : game_core_skill_level(core, proficiency_id);
    if (weapon_level > 0 && weapon.has_equip_effect) {
        add_saturated(bonuses.attack_chance, percentage_of(
            weapon.bonuses.attack_chance,
            percent_per_skill(weapon_level, 30), 0));
        add_saturated(bonuses.block_chance, percentage_of(
            weapon.bonuses.block_chance,
            percent_per_skill(weapon_level, 30), 0));
        add_saturated(bonuses.critical_skill, percentage_of(
            weapon.bonuses.critical_skill,
            percent_per_skill(weapon_level, 10), 0));
    }
    const bool dual_wielding = weapon.present
        && weapon.inventory_slot == "weapon"
        && shield.present && shield.inventory_slot == "weapon";
    if (dual_wielding && shield.has_equip_effect) {
        const int fighting_style = std::min(2,
            game_core_skill_level(core, "fightstyleDualWield"));
        const int efficiency = fighting_style == 2 ? 100
            : fighting_style == 1 ? 50 : 25;
        const char *offhand_proficiency = weapon_proficiency_for_category(
            shield.category);
        const int offhand_level = offhand_proficiency == NULL ? 0
            : game_core_skill_level(core, offhand_proficiency);
        const int proficiency_percent = percent_per_skill(offhand_level, 30)
            * efficiency / 100;
        add_saturated(bonuses.attack_chance, percentage_of(
            shield.bonuses.attack_chance, proficiency_percent, 0));
        add_saturated(bonuses.block_chance, percentage_of(
            shield.bonuses.block_chance, proficiency_percent, 0));
        add_saturated(bonuses.critical_skill, percentage_of(
            shield.bonuses.critical_skill,
            percent_per_skill(offhand_level, 10) * efficiency / 100, 0));

        add_saturated(bonuses.attack_chance, percentage_of(
            shield.bonuses.attack_chance, efficiency, 100));
        add_saturated(bonuses.block_chance, percentage_of(
            shield.bonuses.block_chance, efficiency, 100));
        add_saturated(bonuses.minimum_damage, percentage_of(
            shield.bonuses.minimum_damage, efficiency, 100));
        add_saturated(bonuses.maximum_damage, percentage_of(
            shield.bonuses.maximum_damage, efficiency, 100));
        add_saturated(bonuses.critical_skill, percentage_of(
            shield.bonuses.critical_skill, efficiency, 100));
        add_saturated(bonuses.max_health, percentage_of(
            shield.bonuses.max_health, efficiency, 100));
        add_saturated(bonuses.damage_resistance, percentage_of(
            shield.bonuses.damage_resistance, efficiency, 100));
        add_saturated(bonuses.max_action_points, percentage_of(
            shield.bonuses.max_action_points, efficiency, 100));
        add_saturated(bonuses.move_cost, percentage_of(
            shield.bonuses.move_cost, 100, efficiency));
        add_saturated(bonuses.reequip_cost, percentage_of(
            shield.bonuses.reequip_cost, 100, efficiency));
        add_saturated(bonuses.use_item_cost, percentage_of(
            shield.bonuses.use_item_cost, 100, efficiency));

        const int specialization = game_core_skill_level(core,
            "specializationDualWield");
        const int specialization_percent = percent_per_skill(specialization, 50);
        add_saturated(bonuses.attack_chance, percentage_of(
            weapon.bonuses.attack_chance, specialization_percent, 0));
        add_saturated(bonuses.block_chance, percentage_of(
            weapon.bonuses.block_chance, specialization_percent, 0));
        add_saturated(bonuses.attack_chance, percentage_of(
            shield.bonuses.attack_chance, specialization_percent, 0));
        add_saturated(bonuses.block_chance, percentage_of(
            shield.bonuses.block_chance, specialization_percent, 0));

        if (weapon.has_equip_effect && shield.has_equip_effect) {
            const int main_cost = weapon.bonuses.attack_cost;
            const int offhand_cost = shield.bonuses.attack_cost;
            std::int64_t dual_cost = 0;
            if (fighting_style == 2) {
                dual_cost = std::max(main_cost, offhand_cost);
            } else if (fighting_style == 1) {
                dual_cost = static_cast<std::int64_t>(
                    std::max(main_cost, offhand_cost))
                    + percentage_of(std::min(main_cost, offhand_cost), 50, 0);
            } else {
                dual_cost = static_cast<std::int64_t>(main_cost) + offhand_cost;
            }
            const std::int64_t adjustment = dual_cost - 4 - main_cost;
            const int bounded_adjustment = adjustment > INT_MAX ? INT_MAX
                : adjustment < INT_MIN ? INT_MIN
                : static_cast<int>(adjustment);
            add_saturated(bonuses.attack_cost, bounded_adjustment);

            const double main_critical = weapon.bonuses.has_critical_multiplier
                ? weapon.bonuses.critical_multiplier : 1.0;
            const double offhand_critical = shield.bonuses.has_critical_multiplier
                ? shield.bonuses.critical_multiplier : 1.0;
            const double effective_offhand_critical =
                offhand_critical * efficiency / 100.0;
            bonuses.has_critical_multiplier = true;
            bonuses.critical_multiplier = std::max(main_critical,
                effective_offhand_critical);
        }
    }

    const bool unarmed = (!weapon.present || weapon.size == "none")
        && (!shield.present || shield.size == "none");
    const int unarmed_level = game_core_skill_level(core,
        "weaponProficiencyUnarmed");
    if (unarmed && unarmed_level > 0) {
        add_scaled(bonuses.attack_chance, 20, unarmed_level);
        add_scaled(bonuses.minimum_damage, 2, unarmed_level);
        add_scaled(bonuses.maximum_damage, 2, unarmed_level);
        add_scaled(bonuses.block_chance, 5, unarmed_level);
    }
    if (shield.present && shield.inventory_slot == "shield") {
        add_scaled(bonuses.damage_resistance, 1,
            game_core_skill_level(core, "armorProficiencyShield"));
    }

    bool unarmored = true;
    const GameCoreEquipmentSlot armor_slots[] = {
        GAME_CORE_EQUIP_HEAD, GAME_CORE_EQUIP_BODY,
        GAME_CORE_EQUIP_HAND, GAME_CORE_EQUIP_FEET
    };
    for (std::size_t index = 0;
            index < sizeof(armor_slots) / sizeof(armor_slots[0]); ++index) {
        if (equipment[armor_slots[index]].present
                && equipment[armor_slots[index]].size != "none") {
            unarmored = false;
        }
    }
    const int unarmored_level = game_core_skill_level(core,
        "armorProficiencyUnarmored");
    if (unarmored && unarmored_level > 0) {
        add_scaled(bonuses.block_chance, 10, unarmored_level);
    }

    const int light_armor_level = game_core_skill_level(core,
        "armorProficiencyLight");
    const int heavy_armor_level = game_core_skill_level(core,
        "armorProficiencyHeavy");
    for (std::size_t index = 0;
            index < sizeof(armor_slots) / sizeof(armor_slots[0]); ++index) {
        const EquippedCombatItem &armor = equipment[armor_slots[index]];
        if (!armor.present || !armor.has_equip_effect) continue;
        int level = 0;
        int block_percent = 0;
        int cost_percent = 0;
        if (armor.size == "light" || armor.size == "std") {
            level = light_armor_level;
            block_percent = 30;
        } else if (armor.size == "large") {
            level = heavy_armor_level;
            block_percent = 20;
            cost_percent = 25;
        }
        if (level <= 0) continue;
        const int percent = percent_per_skill(level, block_percent);
        add_saturated(bonuses.block_chance,
            percentage_of(armor.bonuses.block_chance, percent, 0));
        if (cost_percent > 0) {
            const int cost_reduction = percent_per_skill(level, cost_percent);
            add_saturated(bonuses.move_cost, -percentage_of(
                armor.bonuses.move_cost, cost_reduction, 0));
            add_saturated(bonuses.attack_cost, -percentage_of(
                armor.bonuses.attack_cost, cost_reduction, 0));
            add_saturated(bonuses.use_item_cost, -percentage_of(
                armor.bonuses.use_item_cost, cost_reduction, 0));
        }
    }

    if (!weapon.present && !shield.present && unarmored) {
        const int level = game_core_skill_level(core,
            "fightstyleUnarmedUnarmored");
        if (level > 0) {
            add_scaled(bonuses.block_chance, 5, level);
            add_scaled(bonuses.damage_resistance, 1, level);
            add_scaled(bonuses.attack_chance, 12, level);
            add_scaled(bonuses.maximum_damage, 4, level);
            bonuses.has_critical_multiplier = true;
            bonuses.critical_multiplier = 1.0 + 0.25 * level;
        }
    }

    if (weapon.present && weapon.inventory_slot == "weapon"
            && weapon.size == "large" && !shield.present
            && weapon.has_equip_effect) {
        const int style_level = game_core_skill_level(core, "fightstyle2hand");
        const int specialization = game_core_skill_level(core, "specialization2hand");
        add_saturated(bonuses.minimum_damage,
            percentage_of(weapon.bonuses.minimum_damage,
                percent_per_skill(style_level, 30), 0));
        add_saturated(bonuses.maximum_damage,
            percentage_of(weapon.bonuses.maximum_damage,
                percent_per_skill(style_level, 30), 0));
        add_saturated(bonuses.minimum_damage,
            percentage_of(weapon.bonuses.minimum_damage,
                percent_per_skill(specialization, 50), 0));
        add_saturated(bonuses.maximum_damage,
            percentage_of(weapon.bonuses.maximum_damage,
                percent_per_skill(specialization, 50), 0));
        add_saturated(bonuses.attack_chance, percentage_of(
            weapon.bonuses.attack_chance,
            percent_per_skill(specialization, 20), 0));
    }

    if (weapon.present && weapon.inventory_slot == "weapon"
            && shield.present && shield.inventory_slot == "shield"
            && weapon.has_equip_effect && shield.has_equip_effect) {
        const int style_level = game_core_skill_level(core, "fightstyleWeaponShield");
        const int specialization =
            game_core_skill_level(core, "specializationWeaponShield");
        add_saturated(bonuses.attack_chance, percentage_of(
            weapon.bonuses.attack_chance,
            percent_per_skill(style_level, 25), 0));
        add_saturated(bonuses.attack_chance, percentage_of(
            weapon.bonuses.attack_chance,
            percent_per_skill(specialization, 50), 0));
        add_saturated(bonuses.block_chance, percentage_of(
            shield.bonuses.block_chance,
            percent_per_skill(style_level, 25), 0));
        add_saturated(bonuses.minimum_damage, percentage_of(
            weapon.bonuses.minimum_damage,
            percent_per_skill(specialization, 20), 0));
        add_saturated(bonuses.maximum_damage, percentage_of(
            weapon.bonuses.maximum_damage,
            percent_per_skill(specialization, 20), 0));
    }
    return true;
}

}

bool player_combat_stats(GameResources *resources, const GameCore *core,
    PlayerCombatStats &stats, std::string &error)
{
    error.clear();
    if (resources == NULL || core == NULL || core->skill_count > GAME_CORE_MAX_VALUES
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) {
        error = "invalid player combat configuration";
        return false;
    }
    std::vector<std::string> immunity_ids;
    if (!collect_equipment_immunities(resources, core, immunity_ids, error)) {
        return false;
    }

    EquippedCombatItem combat_items[GAME_CORE_EQUIPMENT_SLOTS] = {};
    if (!load_equipped_combat_items(resources, core, combat_items, error)) {
        return false;
    }
    const EquippedCombatItem &main_hand = combat_items[GAME_CORE_EQUIP_WEAPON];
    const EquippedCombatItem &off_hand = combat_items[GAME_CORE_EQUIP_SHIELD];
    const bool dual_wielding = main_hand.present
        && main_hand.inventory_slot == "weapon"
        && off_hand.present && off_hand.inventory_slot == "weapon";
    const bool single_weapon_equipped = !dual_wielding
        && ((main_hand.present && main_hand.inventory_slot == "weapon")
            || (off_hand.present && off_hand.inventory_slot == "weapon"));
    const int dual_wield_skill = dual_wielding ? std::min(2,
        game_core_skill_level(core, "fightstyleDualWield")) : 0;
    const int offhand_efficiency = dual_wield_skill == 2 ? 100
        : dual_wield_skill == 1 ? 50 : 25;
    int main_damage_modifier = main_hand.has_equip_effect
            && main_hand.bonuses.has_non_weapon_damage_modifier
        ? main_hand.bonuses.non_weapon_damage_modifier : -1;
    int offhand_damage_modifier = off_hand.inventory_slot == "weapon"
            && off_hand.has_equip_effect
            && off_hand.bonuses.has_non_weapon_damage_modifier
        ? off_hand.bonuses.non_weapon_damage_modifier : -1;
    int non_weapon_damage_modifier = 100;
    if (main_damage_modifier >= 0 && offhand_damage_modifier >= 0) {
        if (dual_wield_skill == 2) {
            non_weapon_damage_modifier = std::max(main_damage_modifier,
                offhand_damage_modifier);
        } else if (dual_wield_skill == 1) {
            non_weapon_damage_modifier = static_cast<int>(
                (static_cast<std::int64_t>(main_damage_modifier)
                    + offhand_damage_modifier) / 2);
        } else {
            non_weapon_damage_modifier = std::min(main_damage_modifier,
                offhand_damage_modifier);
        }
    } else if (main_damage_modifier <= 0 && offhand_damage_modifier >= 0) {
        non_weapon_damage_modifier = offhand_damage_modifier;
    } else if (offhand_damage_modifier <= 0 && main_damage_modifier >= 0) {
        non_weapon_damage_modifier = main_damage_modifier;
    }

    int weapon_minimum_damage = 0;
    int weapon_maximum_damage = 0;
    if (main_hand.has_equip_effect) {
        weapon_minimum_damage = main_hand.bonuses.minimum_damage;
        weapon_maximum_damage = main_hand.bonuses.maximum_damage;
    }
    if (off_hand.inventory_slot == "weapon" && off_hand.has_equip_effect) {
        const int offhand_damage_percent = dual_wielding
            ? offhand_efficiency : 100;
        add_saturated(weapon_minimum_damage, percentage_of(
            off_hand.bonuses.minimum_damage, offhand_damage_percent, 100));
        add_saturated(weapon_maximum_damage, percentage_of(
            off_hand.bonuses.maximum_damage, offhand_damage_percent, 100));
    }
    AbilityBonuses equipment = {};
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const bool offhand_weapon = slot == GAME_CORE_EQUIP_SHIELD
                && combat_items[slot].present
                && combat_items[slot].inventory_slot == "weapon"
                && dual_wielding;
        const char *item_id = core->equipment[slot];
        if (item_id[0] == '\0') continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + item_id;
            return false;
        }
        GameJsonDocument document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &document, &item)) {
            error = std::string("equipped item record could not be read: ") + item_id;
            return false;
        }
        const GameJsonRef equip_effect = game_json_object_get(item, "equipEffect");
        AbilityBonuses item_bonus = {};
        bool valid = read_ability_bonuses(equip_effect, item_bonus);
        if (valid && !offhand_weapon) {
            add_saturated(equipment.max_health, item_bonus.max_health);
            add_saturated(equipment.max_action_points, item_bonus.max_action_points);
            add_saturated(equipment.move_cost, item_bonus.move_cost);
            add_saturated(equipment.attack_cost, item_bonus.attack_cost);
            add_saturated(equipment.use_item_cost, item_bonus.use_item_cost);
            add_saturated(equipment.reequip_cost, item_bonus.reequip_cost);
            add_saturated(equipment.attack_chance, item_bonus.attack_chance);
            add_saturated(equipment.minimum_damage, item_bonus.minimum_damage);
            add_saturated(equipment.maximum_damage, item_bonus.maximum_damage);
            add_saturated(equipment.critical_skill, item_bonus.critical_skill);
            add_saturated(equipment.block_chance, item_bonus.block_chance);
            add_saturated(equipment.damage_resistance, item_bonus.damage_resistance);

            const GameJsonRef added_conditions = game_json_object_get(
                equip_effect, "addedConditions"
            );
            for (std::size_t index = 0;
                    valid && index < game_json_array_size(added_conditions); ++index) {
                const GameJsonRef effect = game_json_array_get(added_conditions, index);
                char condition_id[GAME_CORE_ID_CAPACITY];
                int magnitude = 1;
                valid = game_json_string_copy(game_json_object_get(effect, "condition"),
                        condition_id, sizeof(condition_id))
                    && read_optional_signed_integer(effect, "magnitude", 1, magnitude);
                if (!valid || magnitude <= 0) continue;
                bool represented_by_condition = false;
                for (std::size_t condition_index = 0;
                        condition_index < core->condition_count; ++condition_index) {
                    const GameCoreCondition &active = core->conditions[condition_index];
                    if (!active.immunity && active.duration == 999
                            && std::strcmp(active.id, condition_id) == 0) {
                        represented_by_condition = true;
                        break;
                    }
                }
                if (represented_by_condition) continue;
                const bool immune = std::find(immunity_ids.begin(),
                    immunity_ids.end(), condition_id) != immunity_ids.end();
                if (!immune) {
                    valid = add_condition_ability(resources, condition_id, magnitude,
                        equipment, error);
                }
            }
        }
        game_json_dispose(&document);
        if (!valid) {
            if (error.empty()) error = std::string("equipped item data is invalid: ") + item_id;
            return false;
        }
    }

    AbilityBonuses conditions = {};
    for (std::size_t index = 0; index < core->condition_count; ++index) {
        const GameCoreCondition &condition = core->conditions[index];
        if (condition.immunity) continue;
        const bool immune = std::find(immunity_ids.begin(), immunity_ids.end(),
            condition.id) != immunity_ids.end();
        if (immune) continue;
        if (!add_condition_ability(resources, condition.id, condition.magnitude,
                conditions, error)) return false;
    }

    AbilityBonuses equipment_skill_bonuses = {};
    if (!equipment_combat_skill_bonuses(resources, core,
            equipment_skill_bonuses, error)) return false;
    const skills::CombatBonuses skill_bonuses = skills::combat_bonuses(core);
    stats.maximum_health = core->max_health;
    add_saturated(stats.maximum_health, equipment.max_health);
    add_saturated(stats.maximum_health, conditions.max_health);
    add_saturated(stats.maximum_health, equipment_skill_bonuses.max_health);
    stats.maximum_action_points = 10 + game_core_skill_level(core, "speed");
    add_saturated(stats.maximum_action_points, equipment.max_action_points);
    add_saturated(stats.maximum_action_points, conditions.max_action_points);
    add_saturated(stats.maximum_action_points,
        equipment_skill_bonuses.max_action_points);
    stats.move_cost = 6;
    add_saturated(stats.move_cost, equipment.move_cost);
    add_saturated(stats.move_cost, conditions.move_cost);
    add_saturated(stats.move_cost, equipment_skill_bonuses.move_cost);
    stats.attack_cost = single_weapon_equipped ? 0 : 4;
    add_saturated(stats.attack_cost, equipment.attack_cost);
    add_saturated(stats.attack_cost, conditions.attack_cost);
    add_saturated(stats.attack_cost, equipment_skill_bonuses.attack_cost);
    stats.use_item_cost = 5;
    add_saturated(stats.use_item_cost, equipment.use_item_cost);
    add_saturated(stats.use_item_cost, conditions.use_item_cost);
    add_saturated(stats.use_item_cost, equipment_skill_bonuses.use_item_cost);
    stats.reequip_cost = 5;
    add_saturated(stats.reequip_cost, equipment.reequip_cost);
    add_saturated(stats.reequip_cost, conditions.reequip_cost);
    add_saturated(stats.reequip_cost, equipment_skill_bonuses.reequip_cost);
    stats.attack_chance = core->attack_chance;
    add_saturated(stats.attack_chance, equipment.attack_chance);
    add_saturated(stats.attack_chance, conditions.attack_chance);
    add_saturated(stats.attack_chance, equipment_skill_bonuses.attack_chance);
    add_saturated(stats.attack_chance, skill_bonuses.attack_chance);
    stats.minimum_damage = core->attack_damage;
    add_saturated(stats.minimum_damage, equipment.minimum_damage);
    add_saturated(stats.minimum_damage, conditions.minimum_damage);
    add_saturated(stats.minimum_damage, equipment_skill_bonuses.minimum_damage);
    stats.maximum_damage = core->attack_damage;
    add_saturated(stats.maximum_damage, equipment.maximum_damage);
    add_saturated(stats.maximum_damage, conditions.maximum_damage);
    add_saturated(stats.maximum_damage, equipment_skill_bonuses.maximum_damage);
    add_saturated(stats.maximum_damage, skill_bonuses.maximum_damage);
    stats.critical_skill = core->critical_skill;
    add_saturated(stats.critical_skill, equipment.critical_skill);
    add_saturated(stats.critical_skill, conditions.critical_skill);
    add_saturated(stats.critical_skill,
        equipment_skill_bonuses.critical_skill);
    if (stats.critical_skill > 0) {
        stats.critical_skill = apply_percentage(stats.critical_skill,
            skill_bonuses.critical_skill_percent);
    }
    stats.critical_multiplier = core->critical_multiplier;
    if (conditions.has_critical_multiplier) {
        stats.critical_multiplier = conditions.critical_multiplier;
    }
    if (equipment.has_critical_multiplier) {
        stats.critical_multiplier = equipment.critical_multiplier;
    }
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        if (slot != GAME_CORE_EQUIP_WEAPON && slot != GAME_CORE_EQUIP_SHIELD) continue;
        const char *item_id = core->equipment[slot];
        if (item_id == NULL || item_id[0] == '\0') continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, item_id, NULL
        );
        GameJsonDocument document;
        GameJsonRef item;
        if (record == NULL || !game_resources_open_record(resources, record,
                &document, &item)) {
            error = std::string("weapon record could not be read: ") + item_id;
            return false;
        }
        AbilityBonuses weapon_bonus = {};
        const bool valid = read_ability_bonuses(
            game_json_object_get(item, "equipEffect"), weapon_bonus
        );
        game_json_dispose(&document);
        if (!valid) {
            error = std::string("weapon data is invalid: ") + item_id;
            return false;
        }
        if (weapon_bonus.has_critical_multiplier) {
            stats.critical_multiplier = weapon_bonus.critical_multiplier;
            break;
        }
    }
    if (equipment_skill_bonuses.has_critical_multiplier) {
        stats.critical_multiplier = equipment_skill_bonuses.critical_multiplier;
    }
    if (stats.critical_multiplier != 0.0 && stats.critical_multiplier != 1.0) {
        stats.critical_multiplier *= 1.0
            + skill_bonuses.critical_multiplier_percent / 100.0;
    }
    stats.block_chance = core->block_chance;
    add_saturated(stats.block_chance, equipment.block_chance);
    add_saturated(stats.block_chance, conditions.block_chance);
    add_saturated(stats.block_chance, equipment_skill_bonuses.block_chance);
    add_saturated(stats.block_chance, skill_bonuses.block_chance);
    stats.damage_resistance = skill_bonuses.damage_resistance;
    add_saturated(stats.damage_resistance, equipment.damage_resistance);
    add_saturated(stats.damage_resistance, conditions.damage_resistance);
    add_saturated(stats.damage_resistance,
        equipment_skill_bonuses.damage_resistance);
    if (non_weapon_damage_modifier != 100) {
        stats.minimum_damage = apply_non_weapon_damage_modifier(
            stats.minimum_damage, weapon_minimum_damage,
            non_weapon_damage_modifier);
        stats.maximum_damage = apply_non_weapon_damage_modifier(
            stats.maximum_damage, weapon_maximum_damage,
            non_weapon_damage_modifier);
    }
    stats.minimum_damage = std::max(0, stats.minimum_damage);
    stats.maximum_damage = std::max(stats.minimum_damage, stats.maximum_damage);
    stats.maximum_health = std::max(1, stats.maximum_health);
    stats.maximum_action_points = std::max(1, stats.maximum_action_points);
    stats.move_cost = std::max(1, stats.move_cost);
    stats.attack_cost = std::max(1, stats.attack_cost);
    stats.use_item_cost = std::max(0, stats.use_item_cost);
    stats.reequip_cost = std::max(0, stats.reequip_cost);
    if (stats.attack_chance < 0) stats.attack_chance = 0;
    if (stats.block_chance < 0) stats.block_chance = 0;
    if (stats.critical_skill < 0 || !std::isfinite(stats.critical_multiplier)) {
        error = "effective player combat stats are invalid";
        return false;
    }
    return true;
}

bool use_item(GameResources *resources, GameCore *core, const std::string &item_id,
    int &action_points, int maximum_action_points, int &health_change,
    std::string &error)
{
    error.clear();
    health_change = 0;
    if (resources == NULL || core == NULL || item_id.empty()
            || maximum_action_points < 0 || action_points < 0
            || action_points > maximum_action_points) {
        error = "invalid item-use configuration";
        return false;
    }
    items::ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    if (!items::item_action(resources, item_id, action, slot, two_handed, error)) {
        return false;
    }
    if (action != items::ACTION_USE) {
        error = "item cannot be used: " + item_id;
        return false;
    }
    if (game_core_inventory_count(core, item_id.c_str()) <= 0) {
        error = "item is not in inventory: " + item_id;
        return false;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    const GameJsonRef effect = game_json_object_get(item, "useEffect");
    int hp_minimum = 0;
    int hp_maximum = 0;
    int ap_minimum = 0;
    int ap_maximum = 0;
    const bool valid_effect = effect.node != GAME_JSON_NO_NODE
        && read_effect_range(effect, "increaseCurrentHP", hp_minimum, hp_maximum)
        && read_effect_range(effect, "increaseCurrentAP", ap_minimum, ap_maximum);
    if (!valid_effect) {
        game_json_dispose(&document);
        error = "item use effect is missing or invalid: " + item_id;
        return false;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        game_json_dispose(&document);
        error = "item-use state could not be allocated";
        return false;
    }
    *previous_state = *core;
    const int previous_action_points = action_points;
    bool success = game_core_remove_item(core, item_id.c_str(), 1);
    const GameJsonRef conditions = game_json_object_get(effect, "conditionsSource");
    for (std::size_t index = 0; success
            && index < game_json_array_size(conditions); ++index) {
        success = apply_condition_effect(resources, core,
            game_json_array_get(conditions, index), error);
    }
    const int ap_delta = rolled_signed_value(core, ap_minimum, ap_maximum);
    const int64_t updated_ap = static_cast<std::int64_t>(action_points) + ap_delta;
    action_points = static_cast<int>(std::max<std::int64_t>(0,
        std::min<std::int64_t>(maximum_action_points, updated_ap)));
    const int hp_delta = rolled_signed_value(core, hp_minimum, hp_maximum);
    if (success && hp_delta != 0) {
        if (hp_delta < 0) {
            success = game_core_damage_player(core,
                hp_delta == INT_MIN ? INT_MAX : -hp_delta);
        } else {
            PlayerCombatStats stats;
            success = player_combat_stats(resources, core, stats, error);
            if (success) {
                const int old_health = core->health;
                const int64_t healed = static_cast<int64_t>(core->health) + hp_delta;
                core->health = static_cast<int>(std::min<std::int64_t>(
                    stats.maximum_health, healed));
                health_change = core->health - old_health;
            }
        }
    }
    game_json_dispose(&document);
    if (!success) {
        *core = *previous_state;
        action_points = previous_action_points;
        health_change = 0;
        if (error.empty()) error = "item use effect could not be applied: " + item_id;
        return false;
    }
    return true;
}

bool finish_combat_round(GameResources *resources, GameCore *core,
    int &action_points, int maximum_action_points, bool adjacent_enemy,
    std::string &error, bool full_round, bool apply_round_skills,
    bool apply_regeneration)
{
    error.clear();
    if (resources == NULL || core == NULL || maximum_action_points < 0
            || action_points < 0 || action_points > maximum_action_points
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) {
        error = "invalid combat-round configuration";
        return false;
    }

    const int rejuvenation_level = !apply_round_skills || full_round
        || core->condition_count == 0 ? 0
        : game_core_skill_level(core, "rejuvenation");
    const int rejuvenation_chance = static_cast<int>(std::min<std::int64_t>(
        100, static_cast<std::int64_t>(rejuvenation_level) * 20));
    if (rejuvenation_chance > 0
            && game_core_random(core, 100)
                < static_cast<std::uint32_t>(rejuvenation_chance)) {
        std::vector<std::size_t> removable;
        for (std::size_t condition_index = 0;
                condition_index < core->condition_count; ++condition_index) {
            const GameCoreCondition &active = core->conditions[condition_index];
            if (active.immunity || active.duration <= 0
                    || active.duration == 998 || active.duration == 999) continue;
            const GameResourceRecordEntry *record = game_resources_find(
                GAME_RESOURCE_CONDITION, active.id, NULL
            );
            if (record == NULL) {
                error = std::string("condition record not found: ") + active.id;
                return false;
            }
            GameJsonDocument document;
            GameJsonRef condition;
            if (!game_resources_open_record(resources, record, &document,
                    &condition)) {
                error = std::string("condition record could not be read: ")
                    + active.id;
                return false;
            }
            char category[32];
            int positive = 0;
            const bool valid = game_json_string_copy(
                    game_json_object_get(condition, "category"),
                    category, sizeof(category))
                && read_optional_integer(condition, "isPositive", 0, positive);
            game_json_dispose(&document);
            if (!valid) {
                error = std::string("condition data is invalid: ") + active.id;
                return false;
            }
            if (positive == 0 && std::strcmp(category, "spiritual") != 0) {
                removable.push_back(condition_index);
            }
        }
        if (!removable.empty()) {
            const std::size_t selected = removable[game_core_random(core,
                static_cast<std::uint32_t>(removable.size()))];
            if (core->conditions[selected].magnitude > 1) {
                --core->conditions[selected].magnitude;
            } else {
                for (std::size_t move = selected + 1;
                        move < core->condition_count; ++move) {
                    core->conditions[move - 1] = core->conditions[move];
                }
                --core->condition_count;
            }
        }
    }

    for (int positive = 0; positive <= 1; ++positive) {
        for (std::size_t index = 0; index < core->condition_count; ++index) {
            GameCoreCondition &active = core->conditions[index];
            if (active.immunity) continue;
            const GameResourceRecordEntry *record = game_resources_find(
                GAME_RESOURCE_CONDITION, active.id, NULL
            );
            if (record == NULL) {
                error = std::string("condition record not found: ") + active.id;
                return false;
            }
            GameJsonDocument document;
            GameJsonRef condition;
            if (!game_resources_open_record(resources, record, &document, &condition)) {
                error = std::string("condition record could not be read: ") + active.id;
                return false;
            }
            int is_positive = 0;
            const GameJsonRef effect = game_json_object_get(condition,
                full_round ? "fullRoundEffect" : "roundEffect");
            int hp_minimum = 0;
            int hp_maximum = 0;
            int ap_minimum = 0;
            int ap_maximum = 0;
            bool success = read_optional_integer(condition, "isPositive", 0,
                    is_positive)
                && read_effect_range(effect, "increaseCurrentHP",
                    hp_minimum, hp_maximum)
                && read_effect_range(effect, "increaseCurrentAP",
                    ap_minimum, ap_maximum);
            game_json_dispose(&document);
            if (!success) {
                error = std::string("condition round effect is invalid: ") + active.id;
                return false;
            }
            if (is_positive != positive) continue;

            const int hp_delta = rolled_signed_value(core, hp_minimum, hp_maximum);
            const int64_t scaled_hp = static_cast<std::int64_t>(hp_delta)
                * active.magnitude;
            const int health_effect = static_cast<int>(std::max<std::int64_t>(
                INT_MIN, std::min<std::int64_t>(INT_MAX, scaled_hp)));
            if (health_effect < 0) {
                if (!game_core_damage_player(core,
                        health_effect == INT_MIN ? INT_MAX : -health_effect)) {
                    error = std::string("condition damage could not be applied: ")
                        + active.id;
                    return false;
                }
            } else if (health_effect > 0) {
                PlayerCombatStats stats;
                if (!player_combat_stats(resources, core, stats, error)) return false;
                const int64_t healed = static_cast<std::int64_t>(core->health)
                    + health_effect;
                core->health = static_cast<int>(std::min<std::int64_t>(
                    stats.maximum_health, healed));
            }
            const int ap_delta = rolled_signed_value(core, ap_minimum, ap_maximum);
            const int64_t scaled_ap = static_cast<std::int64_t>(ap_delta)
                * active.magnitude;
            const int64_t updated_ap = static_cast<std::int64_t>(action_points)
                + scaled_ap;
            action_points = static_cast<int>(std::max<std::int64_t>(0,
                std::min<std::int64_t>(maximum_action_points, updated_ap)));
        }
    }

    if (!full_round) {
        std::size_t index = 0;
        while (index < core->condition_count) {
            GameCoreCondition &active = core->conditions[index];
            if (active.duration > 0 && active.duration != 998
                    && active.duration != 999) --active.duration;
            if (active.duration == 0) {
                for (std::size_t move = index + 1;
                        move < core->condition_count; ++move) {
                    core->conditions[move - 1] = core->conditions[move];
                }
                --core->condition_count;
            } else {
                ++index;
            }
        }
    }

    PlayerCombatStats stats;
    if (!player_combat_stats(resources, core, stats, error)) return false;
    core->health = std::min(core->health, stats.maximum_health);
    if (apply_regeneration && apply_round_skills && !full_round
            && !adjacent_enemy
            && core->health > 0) {
        const int regeneration = game_core_skill_level(core, "regeneration");
        if (regeneration > 0) {
            const int64_t healed = static_cast<int64_t>(core->health) + regeneration;
            core->health = static_cast<int>(std::min<std::int64_t>(
                stats.maximum_health, healed));
        }
    }
    return true;
}

static bool finish_monster_defeat(GameResources *resources, GameCore *core,
    const std::string &monster_id, const std::string &drop_list_id,
    const std::string &instance_id, int experience, AttackResult &result,
    std::string &error)
{
    const std::string defeated_id = instance_id + "_defeated";
    const std::int64_t cleave_action_points =
        static_cast<std::int64_t>(game_core_skill_level(core, "cleave")) * 3;
    add_saturated(result.player_action_points_change,
        static_cast<int>(std::min<std::int64_t>(INT_MAX,
            cleave_action_points)));
    const int eater_level = core->skill_count <= GAME_CORE_MAX_VALUES
        ? game_core_skill_level(core, "eater") : 0;
    PlayerCombatStats player_stats;
    if (!player_combat_stats(resources, core, player_stats, error)) return false;
    const int64_t health_available =
        static_cast<int64_t>(player_stats.maximum_health) - core->health;
    if (eater_level > 0 && health_available > 0) {
        result.health_restored = static_cast<int>(
            health_available < eater_level ? health_available : eater_level);
        core->health += result.health_restored;
    }

    int experience_reward = experience;
    const int more_experience_level = core->skill_count <= GAME_CORE_MAX_VALUES
        ? game_core_skill_level(core, "moreExp") : 0;
    if (!game_core_set_quest_progress(core, defeated_id.c_str(), 1)
            || (!drop_list_id.empty()
                && !items::apply_drop_list(resources, core, drop_list_id, error))
            || !experience_with_skill_bonus(experience, more_experience_level,
                experience_reward)
            || !game_core_add_experience(core, experience_reward)) {
        if (error.empty()) {
            error = "monster defeat could not be recorded: " + instance_id;
        }
        return false;
    }
    result.experience_gained = experience_reward;

    if (!apply_equipment_condition_effects(resources, core, "killEffect",
                "conditionsSource", error)
            || !apply_monster_condition_effects(resources, core, monster_id,
                "deathEffect", "conditionsSource", error)
            || !apply_monster_condition_effects(resources, core, monster_id,
                "onDeathEffect", "conditionsSource", error)) return false;
    EffectActor player = {true, NULL, 0, &result.player_action_points_change};
    EffectActor unused_attacker = {false, NULL, 0, NULL};
    if (!apply_equipment_stat_effects(resources, core, "killEffect", false,
                player, unused_attacker, error)
            || !apply_monster_stat_effect(resources, core, monster_id,
                "deathEffect", false, player, unused_attacker, error)
            || !apply_monster_stat_effect(resources, core, monster_id,
                "onDeathEffect", false, player, unused_attacker, error)) {
        return false;
    }
    return true;
}

bool finish_enemy_combat_round(GameResources *resources, GameCore *core,
    const std::string &monster_id, const std::string &drop_list_id,
    const std::string &instance_id, int maximum_health, int &health,
    int maximum_action_points, int &action_points,
    std::vector<ConditionState> &conditions, AttackResult &result,
    std::string &error, bool full_round)
{
    error.clear();
    result = {};
    if (resources == NULL || core == NULL || monster_id.empty()
            || instance_id.empty() || maximum_health <= 0
            || health < 0 || health > maximum_health
            || maximum_action_points < 0 || action_points < 0
            || action_points > maximum_action_points) {
        error = "invalid enemy combat-round configuration";
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
    int base_experience = 0;
    int monster_maximum_health = 0;
    int damage_minimum = 0;
    int damage_maximum = 0;
    int attack_chance = 0;
    const GameJsonRef damage = game_json_object_get(monster, "attackDamage");
    const bool valid = read_optional_integer(monster, "maxHP", 1,
            monster_maximum_health)
        && read_optional_integer(damage, "min", 0, damage_minimum)
        && read_optional_integer(damage, "max", 0, damage_maximum)
        && read_optional_integer(monster, "attackChance", 0, attack_chance)
        && calculate_experience(monster, monster_maximum_health,
            damage_minimum, damage_maximum, attack_chance, base_experience);
    game_json_dispose(&document);
    if (!valid || monster_maximum_health != maximum_health
            || damage_maximum < damage_minimum) {
        error = "monster combat data is invalid: " + monster_id;
        return false;
    }

    const std::unique_ptr<GameCore> previous_core(new (std::nothrow) GameCore);
    if (!previous_core) {
        error = "enemy round state could not be allocated";
        return false;
    }
    *previous_core = *core;
    const int previous_health = health;
    const int previous_action_points = action_points;
    const std::vector<ConditionState> previous_conditions = conditions;
    bool success = true;
    for (int positive = 0; success && positive <= 1; ++positive) {
        for (ConditionState &active : conditions) {
            if (active.immunity) continue;
            const GameResourceRecordEntry *condition_record = game_resources_find(
                GAME_RESOURCE_CONDITION, active.id.c_str(), NULL
            );
            if (condition_record == NULL) {
                error = "condition record not found: " + active.id;
                success = false;
                break;
            }
            GameJsonDocument condition_document;
            GameJsonRef condition;
            if (!game_resources_open_record(resources, condition_record,
                    &condition_document, &condition)) {
                error = "condition record could not be read: " + active.id;
                success = false;
                break;
            }
            int is_positive = 0;
            const GameJsonRef effect = game_json_object_get(condition,
                full_round ? "fullRoundEffect" : "roundEffect");
            int hp_minimum = 0;
            int hp_maximum = 0;
            int ap_minimum = 0;
            int ap_maximum = 0;
            success = read_optional_integer(condition, "isPositive", 0, is_positive)
                && read_effect_range(effect, "increaseCurrentHP",
                    hp_minimum, hp_maximum)
                && read_effect_range(effect, "increaseCurrentAP",
                    ap_minimum, ap_maximum);
            game_json_dispose(&condition_document);
            if (!success) {
                error = "condition round effect is invalid: " + active.id;
                break;
            }
            if (is_positive != positive) continue;

            const int hp_delta = rolled_signed_value(core, hp_minimum, hp_maximum);
            const int64_t scaled_hp = static_cast<int64_t>(hp_delta)
                * active.magnitude;
            const int64_t updated_health = static_cast<int64_t>(health)
                + scaled_hp;
            health = static_cast<int>(std::max<std::int64_t>(0,
                std::min<std::int64_t>(maximum_health, updated_health)));
            const int ap_delta = rolled_signed_value(core, ap_minimum, ap_maximum);
            const int64_t scaled_ap = static_cast<int64_t>(ap_delta)
                * active.magnitude;
            const int64_t updated_action_points =
                static_cast<int64_t>(action_points) + scaled_ap;
            action_points = static_cast<int>(std::max<std::int64_t>(0,
                std::min<std::int64_t>(maximum_action_points,
                    updated_action_points)));
        }
    }
    if (success) {
        std::size_t index = 0;
        while (!full_round && index < conditions.size()) {
            if (conditions[index].duration > 0
                    && conditions[index].duration != 998
                    && conditions[index].duration != 999) {
                --conditions[index].duration;
            }
            if (conditions[index].duration == 0) {
                conditions.erase(conditions.begin() + index);
            } else {
                ++index;
            }
        }
        success = game_core_set_quest_progress(core, health_id.c_str(), health);
    }
    result.enemy_health = health;
    result.enemy_max_health = maximum_health;
    result.enemy_defeated = health == 0;
    if (success && result.enemy_defeated) {
        success = finish_monster_defeat(resources, core, monster_id,
            drop_list_id, instance_id, base_experience, result, error);
    }
    if (!success) {
        *core = *previous_core;
        health = previous_health;
        action_points = previous_action_points;
        conditions = previous_conditions;
        result = {};
        if (error.empty()) error = "enemy round effects could not be applied";
        return false;
    }
    return true;
}

static bool attack_monster_impl(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error,
    bool resolve_counterattack,
    std::vector<ConditionState> *enemy_conditions
)
{
    error.clear();
    result = {};
    if (resources == NULL || core == NULL || monster_id.empty()
            || instance_id.empty() || player_damage <= 0) {
        error = "invalid combat configuration";
        return false;
    }
    if (core->health <= 0) {
        error = "player cannot attack while knocked out";
        return false;
    }
    std::vector<ConditionState> transient_enemy_conditions;
    std::vector<ConditionState> &active_enemy_conditions =
        enemy_conditions == NULL ? transient_enemy_conditions : *enemy_conditions;

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
    bool monster_is_critical_immune = false;
    const GameJsonRef damage = game_json_object_get(monster, "attackDamage");
    const bool valid_stats =
        read_optional_integer(monster, "maxHP", 1, maximum_health)
        && read_optional_integer(damage, "min", 0, damage_minimum)
        && read_optional_integer(damage, "max", 0, damage_maximum)
        && read_optional_integer(monster, "attackChance", 0, attack_chance)
        && read_optional_integer(monster, "blockChance", 0, block_chance)
        && read_optional_integer(monster, "damageResistance", 0, damage_resistance)
        && calculate_experience(monster, maximum_health, damage_minimum,
            damage_maximum, attack_chance, experience);
    if (valid_stats) {
        monster_is_critical_immune = immune_to_critical_hits(monster);
    }
    game_json_dispose(&document);
    if (!valid_stats || maximum_health <= 0 || damage_maximum < damage_minimum) {
        error = "monster combat data is invalid: " + monster_id;
        return false;
    }

    int enemy_health = game_core_quest_progress(core, health_id.c_str());
    if (enemy_health == 0) enemy_health = maximum_health;
    if (enemy_health > maximum_health) {
        error = "saved monster health exceeds its maximum: " + instance_id;
        return false;
    }
    AbilityBonuses condition_bonuses = {};
    if (!condition_ability_bonuses(resources, active_enemy_conditions,
            condition_bonuses, error)) {
        return false;
    }
    add_saturated(damage_resistance, condition_bonuses.damage_resistance);
    PlayerCombatStats player_stats;
    if (!player_combat_stats(resources, core, player_stats, error)) return false;
    const int damage_override = player_damage - core->attack_damage;
    add_saturated(player_stats.minimum_damage, damage_override);
    add_saturated(player_stats.maximum_damage, damage_override);
    if (player_stats.maximum_damage < player_stats.minimum_damage) {
        player_stats.maximum_damage = player_stats.minimum_damage;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "combat state could not be allocated";
        return false;
    }
    *previous_state = *core;
    const std::vector<ConditionState> previous_enemy_conditions =
        active_enemy_conditions;
    const bool player_hit = game_core_random(core, 100)
        < static_cast<std::uint32_t>(attack_hit_chance(
            player_stats.attack_chance, block_chance));
    int damage_dealt = 0;
    bool player_critical_hit = false;
    if (player_hit) {
        const int rolled_player_damage = rolled_damage(core,
            player_stats.minimum_damage, player_stats.maximum_damage);
        if (!monster_is_critical_immune
                && has_critical_attacks(player_stats.critical_skill,
                    player_stats.critical_multiplier)) {
            player_critical_hit = game_core_random(core, 100)
                < static_cast<std::uint32_t>(
                    effective_critical_chance(player_stats.critical_skill));
        }
        const int critical_player_damage = player_critical_hit
            ? critical_damage(rolled_player_damage, player_stats.critical_multiplier)
            : rolled_player_damage;
        damage_dealt = critical_player_damage - damage_resistance;
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
    result.health_restored = 0;
    result.player_hit = player_hit;
    result.player_critical_hit = player_critical_hit;
    result.enemy_critical_hit = false;
    result.enemy_defeated = enemy_health == 0;
    result.player_knocked_out = false;
    EffectActor player_actor = {
        true, NULL, 0, &result.player_action_points_change
    };
    EffectActor enemy_actor = {
        false, &enemy_health, maximum_health,
        &result.enemy_action_points_change
    };
    const char *attacker_effect = player_hit ? "hitEffect" : "missEffect";
    const char *target_effect = player_hit
        ? "hitReceivedEffect" : "missReceivedEffect";
    if (!apply_equipment_combat_effects(resources, core,
                active_enemy_conditions, attacker_effect, false,
                player_actor, enemy_actor, error)
            || !apply_monster_combat_effects(resources, core,
                active_enemy_conditions, monster_id, target_effect, true,
                enemy_actor, player_actor, error)) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        result = {};
        return false;
    }
    const auto apply_player_attack_skill_effects = [&]() {
        if (!player_hit) return true;
        PlayerCombatStats skill_stats;
        if (!player_combat_stats(resources, core, skill_stats, error)) {
            return false;
        }
        if (skill_stats.attack_chance - block_chance > 50) {
            const int chance = 15 * game_core_skill_level(core, "concussion");
            if (chance > 0 && !apply_enemy_condition_data(resources, core,
                    active_enemy_conditions, "concussion", 1, 5, 100,
                    std::min(100, chance), error)) {
                return false;
            }
        }
        if (player_critical_hit) {
            const int fracture_chance =
                50 * game_core_skill_level(core, "crit2");
            const int bleeding_chance =
                50 * game_core_skill_level(core, "crit1");
            if (fracture_chance > 0 && !apply_enemy_condition_data(resources,
                    core, active_enemy_conditions, "crit2", 1, 5, 100,
                    std::min(100, fracture_chance), error)) {
                return false;
            }
            if (bleeding_chance > 0 && !apply_enemy_condition_data(resources,
                    core, active_enemy_conditions, "crit1", 1, 5, 100,
                    std::min(100, bleeding_chance), error)) {
                return false;
            }
        }
        return true;
    };
    if (!game_core_set_quest_progress(core, health_id.c_str(), enemy_health)) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        error = "monster health could not be saved: " + instance_id;
        result = {};
        return false;
    }
    result.enemy_health = enemy_health;
    result.enemy_defeated = enemy_health == 0;
    result.player_knocked_out = core->health == 0;
    if (result.enemy_defeated) {
        if (!finish_monster_defeat(resources, core, monster_id, drop_list_id,
                instance_id, experience, result, error)) {
            *core = *previous_state;
            active_enemy_conditions = previous_enemy_conditions;
            result = {};
            return false;
        }
        if (!apply_player_attack_skill_effects()) {
            *core = *previous_state;
            active_enemy_conditions = previous_enemy_conditions;
            result = {};
            return false;
        }
        return true;
    }

    if (!apply_player_attack_skill_effects()) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        result = {};
        return false;
    }
    if (!resolve_counterattack) return true;

    MonsterAttackResult counterattack;
    if (!monster_attack_player(resources, core, monster_id, counterattack,
            error, &active_enemy_conditions, &enemy_health, maximum_health)) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        result = {};
        return false;
    }
    result.damage_taken = counterattack.damage_taken;
    result.enemy_critical_hit = counterattack.critical_hit;
    result.player_knocked_out = counterattack.player_knocked_out;
    add_saturated(result.player_action_points_change,
        counterattack.player_action_points_change);
    add_saturated(result.enemy_action_points_change,
        counterattack.monster_action_points_change);
    result.enemy_health = enemy_health;
    result.enemy_defeated = enemy_health == 0;
    if (!game_core_set_quest_progress(core, health_id.c_str(), enemy_health)) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        error = "monster health could not be saved: " + instance_id;
        result = {};
        return false;
    }
    if (result.enemy_defeated
            && !finish_monster_defeat(resources, core, monster_id,
                drop_list_id, instance_id, experience, result, error)) {
        *core = *previous_state;
        active_enemy_conditions = previous_enemy_conditions;
        result = {};
        return false;
    }
    return true;
}

bool attack_monster(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error,
    std::vector<ConditionState> *enemy_conditions
)
{
    return attack_monster_impl(resources, core, monster_id, drop_list_id,
        instance_id, player_damage, result, error, true, enemy_conditions);
}

bool attack_monster_once(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error,
    std::vector<ConditionState> *enemy_conditions
)
{
    return attack_monster_impl(resources, core, monster_id, drop_list_id,
        instance_id, player_damage, result, error, false, enemy_conditions);
}

bool monster_attack_player(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    MonsterAttackResult &result,
    std::string &error,
    std::vector<ConditionState> *monster_conditions,
    int *monster_health,
    int maximum_monster_health
)
{
    error.clear();
    result = {};
    if (resources == NULL || core == NULL || monster_id.empty()) {
        error = "invalid combat configuration";
        return false;
    }
    if (core->health <= 0) {
        error = "player cannot be attacked while knocked out";
        return false;
    }
    std::vector<ConditionState> transient_monster_conditions;
    std::vector<ConditionState> &active_monster_conditions =
        monster_conditions == NULL ? transient_monster_conditions
            : *monster_conditions;
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
        error = "monster combat record could not be read: " + monster_id;
        return false;
    }
    int attack_chance = 0;
    int critical_skill = 0;
    int damage_minimum = 0;
    int damage_maximum = 0;
    double critical_multiplier = 0.0;
    const GameJsonRef damage = game_json_object_get(monster, "attackDamage");
    const bool valid_stats =
        read_optional_integer(monster, "attackChance", 0, attack_chance)
        && read_optional_integer(monster, "criticalSkill", 0, critical_skill)
        && read_optional_number(monster, "criticalMultiplier", 0.0,
            critical_multiplier)
        && read_optional_integer(damage, "min", 0, damage_minimum)
        && read_optional_integer(damage, "max", 0, damage_maximum);
    game_json_dispose(&document);
    if (!valid_stats || damage_maximum < damage_minimum) {
        error = "monster combat data is invalid: " + monster_id;
        return false;
    }
    AbilityBonuses condition_bonuses = {};
    if (!condition_ability_bonuses(resources, active_monster_conditions,
            condition_bonuses, error)) return false;
    add_saturated(attack_chance, condition_bonuses.attack_chance);
    add_saturated(damage_minimum, condition_bonuses.minimum_damage);
    add_saturated(damage_maximum, condition_bonuses.maximum_damage);
    add_saturated(critical_skill, condition_bonuses.critical_skill);
    if (condition_bonuses.has_critical_multiplier) {
        critical_multiplier = condition_bonuses.critical_multiplier;
    }
    if (damage_maximum < damage_minimum) damage_maximum = damage_minimum;
    PlayerCombatStats player_stats;
    if (!player_combat_stats(resources, core, player_stats, error)) return false;

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "combat state could not be allocated";
        return false;
    }
    *previous_state = *core;
    const std::vector<ConditionState> previous_enemy_conditions =
        active_monster_conditions;
    const int previous_monster_health = monster_health == NULL
        ? 0 : *monster_health;
    result.hit = game_core_random(core, 100)
        < static_cast<std::uint32_t>(attack_hit_chance(
            attack_chance, player_stats.block_chance));
    if (result.hit) {
        const int base_damage = rolled_damage(core, damage_minimum, damage_maximum);
        result.critical_hit = has_critical_attacks(critical_skill, critical_multiplier)
            && game_core_random(core, 100)
                < static_cast<std::uint32_t>(
                    effective_critical_chance(critical_skill));
        const int dealt = result.critical_hit
            ? critical_damage(base_damage, critical_multiplier) : base_damage;
        result.damage_taken = dealt > player_stats.damage_resistance
            ? dealt - player_stats.damage_resistance : 0;
        if (result.damage_taken > 0
                && !game_core_damage_player(core, result.damage_taken)) {
            *core = *previous_state;
            if (monster_conditions != NULL) {
                *monster_conditions = previous_enemy_conditions;
            }
            error = "player damage could not be applied";
            return false;
        }
        result.player_knocked_out = core->health == 0;
    }
    EffectActor monster_actor = {
        false, monster_health, maximum_monster_health,
        &result.monster_action_points_change
    };
    EffectActor player_actor = {
        true, NULL, 0, &result.player_action_points_change
    };
    const char *monster_effect = result.hit ? "hitEffect" : "missEffect";
    const char *player_effect = result.hit
        ? "hitReceivedEffect" : "missReceivedEffect";
    if (!apply_monster_combat_effects(resources, core,
                active_monster_conditions, monster_id, monster_effect, false,
                monster_actor, player_actor, error)
            || !apply_equipment_combat_effects(resources, core,
                active_monster_conditions, player_effect, true,
                player_actor, monster_actor, error)) {
        *core = *previous_state;
        if (monster_health != NULL) *monster_health = previous_monster_health;
        active_monster_conditions = previous_enemy_conditions;
        result = {};
        return false;
    }
    result.player_knocked_out = core->health == 0;
    return true;
}

}
}
