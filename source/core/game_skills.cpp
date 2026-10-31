#include "game_skills.hpp"

#include <climits>
#include <cstdint>

namespace andortrail {
namespace skills {
namespace {

Definition make_definition(const char *id, const char *name, const char *description,
    int maximum_level = -1, int required_player_level_per_rank = 0,
    int required_player_level_offset = 0, const char *required_skill_a = NULL,
    int required_skill_a_per_rank = 0, const char *required_skill_b = NULL,
    int required_skill_b_per_rank = 0, const char *required_skill_c = NULL,
    int required_skill_c_per_rank = 0, int required_block_chance_per_rank = 0,
    int required_max_health_per_rank = 0, int required_max_health_offset = 0)
{
    Definition result = {
        id, name, description, maximum_level, required_player_level_per_rank,
        required_player_level_offset, required_skill_a, required_skill_a_per_rank,
        required_skill_b, required_skill_b_per_rank, required_skill_c,
        required_skill_c_per_rank, required_block_chance_per_rank,
        required_max_health_per_rank, required_max_health_offset
    };
    return result;
}

const Definition VISIBLE_SKILLS[] = {
    make_definition("weaponChance", "Weapon Accuracy", "Increased attack chance"),
    make_definition("weaponDmg", "Hard Hit", "Increased attack damage"),
    make_definition("barter", "Merchant", "Better shop prices", 3),
    make_definition("dodge", "Dodge", "Increased block chance"),
    make_definition("barkSkin", "Bark Skin", "Damage resistance", 5, 10, 0,
        NULL, 0, NULL, 0, NULL, 0, 15),
    make_definition("moreCriticals", "More Criticals", "Increased critical skill"),
    make_definition("betterCriticals", "Better Criticals", "Increased critical damage",
        -1, 0, 0, "moreCriticals", 1),
    make_definition("speed", "Combat Speed", "Increased maximum action points",
        2, 15),
    make_definition("coinfinder", "Treasure Hunter", "Higher chance of finding gold"),
    make_definition("moreExp", "Quick Learner", "More experience from monster kills"),
    make_definition("cleave", "Cleave", "Recover action points on every kill",
        -1, 0, 0, "weaponChance", 1, "weaponDmg", 1),
    make_definition("eater", "Corpse Eater", "Recover health points on every kill",
        -1, 0, 0, NULL, 0, NULL, 0, NULL, 0, 0, 20, 20),
    make_definition("fortitude", "Increased Fortitude", "Gain health on each level up",
        -1, 15, -10),
    make_definition("evasion", "Evasion", "Increased chance of fleeing", 4),
    make_definition("regeneration", "Regeneration", "Gain health every round",
        -1, 0, 0, "fortitude", 1, NULL, 0, NULL, 0, 0, 30),
    make_definition("lowerExploss", "Failure Mastery",
        "Decrease amount of lost experience when dying", 5),
    make_definition("magicfinder", "Magic Finder", "Increased chance of finding magic items"),
    make_definition("resistanceMental", "Strong Mind",
        "Resistance against mental conditions", 7),
    make_definition("resistancePhysical", "Enduring Body",
        "Resistance against physical capacity conditions", 7),
    make_definition("resistanceBlood", "Pure Blood",
        "Resistance against blood disorders", 7),
    make_definition("crit1", "Internal bleeding", "Chance of internal bleeding", 1,
        0, 0, "moreCriticals", 2, "betterCriticals", 2),
    make_definition("crit2", "Fracture", "Chance of bone fracture", 1,
        0, 0, "moreCriticals", 4, "betterCriticals", 4, "crit1", 1),
    make_definition("rejuvenation", "Rejuvenation", "Chance of effect removal", 1,
        0, 0, "resistanceBlood", 3, "resistanceMental", 3,
        "resistancePhysical", 3),
    make_definition("taunt", "Taunt", "Attacker loses AP on miss", 1,
        0, 0, "evasion", 2, "dodge", 4),
    make_definition("concussion", "Concussion", "Chance of concussion", 1,
        0, 0, "speed", 2, "weaponChance", 3, "weaponDmg", 5),
    make_definition("fightstyleDualWield", "Fighting style: Dual wield",
        "Wield two weapons at the same time", 2, 15),
    make_definition("fightstyle2hand", "Fighting style: Two-handed weapon",
        "Make better use of weapons that require both hands", 2, 15),
    make_definition("fightstyleWeaponShield", "Fighting style: Weapon and shield",
        "Better at fighting with weapon and shield", 2, 15),
    make_definition("fightstyleUnarmedUnarmored", "Fighting style: Way of the monk",
        "Better at fighting unarmed/unarmored", 3, 15),
    make_definition("specializationDualWield", "Specialization: Dual wield",
        "Expert at dual wielding", 1, 45, 0, "fightstyleDualWield", 2),
    make_definition("specialization2hand", "Specialization: Two-handed weapon",
        "Expert at two-handed weapons", 1, 45, 0, "fightstyle2hand", 2),
    make_definition("specializationWeaponShield", "Specialization: Weapon and shield",
        "Expert at fighting with weapon and shield", 1, 45, 0,
        "fightstyleWeaponShield", 2)
};

int required_value(int per_rank, int offset, int requested_level)
{
    const int64_t value = static_cast<int64_t>(requested_level) * per_rank + offset;
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return static_cast<int>(value);
}

bool meets_skill_requirement(const GameCore *core, const char *id,
    int per_rank, int requested_level)
{
    return id == NULL || game_core_skill_level(core, id)
        >= required_value(per_rank, 0, requested_level);
}

int scaled_bonus(const GameCore *core, const char *skill_id, int amount_per_rank)
{
    const int64_t bonus = static_cast<int64_t>(
        game_core_skill_level(core, skill_id)) * amount_per_rank;
    if (bonus > INT_MAX) return INT_MAX;
    return static_cast<int>(bonus);
}

}

const Definition *visible_definitions(std::size_t &count)
{
    count = sizeof(VISIBLE_SKILLS) / sizeof(VISIBLE_SKILLS[0]);
    return VISIBLE_SKILLS;
}

bool can_level_up(const GameCore *core, const Definition &definition)
{
    if (core == NULL || core->skill_points <= 0
            || core->skill_count > GAME_CORE_MAX_VALUES
            || definition.id == NULL || definition.id[0] == '\0') return false;
    const int current_level = game_core_skill_level(core, definition.id);
    if (current_level == INT_MAX
            || (definition.maximum_level >= 0
                && current_level >= definition.maximum_level)) return false;
    const int requested_level = current_level + 1;
    if (core->level < required_value(definition.required_player_level_per_rank,
                definition.required_player_level_offset, requested_level)) return false;
    if (!meets_skill_requirement(core, definition.required_skill_a,
                definition.required_skill_a_per_rank, requested_level)
            || !meets_skill_requirement(core, definition.required_skill_b,
                definition.required_skill_b_per_rank, requested_level)
            || !meets_skill_requirement(core, definition.required_skill_c,
                definition.required_skill_c_per_rank, requested_level)) return false;
    if (core->block_chance < required_value(
                definition.required_block_chance_per_rank, 0, requested_level)
            || core->max_health < required_value(
                definition.required_max_health_per_rank,
                definition.required_max_health_offset, requested_level)) return false;
    return true;
}

bool level_up(GameCore *core, const Definition &definition)
{
    if (!can_level_up(core, definition)) return false;
    const int current_level = game_core_skill_level(core, definition.id);
    if (!game_core_set_skill_level(core, definition.id, current_level + 1)) return false;
    --core->skill_points;
    return true;
}

CombatBonuses combat_bonuses(const GameCore *core)
{
    CombatBonuses bonuses = {0, 0, 0, 0};
    if (core == NULL || core->skill_count > GAME_CORE_MAX_VALUES) return bonuses;
    bonuses.attack_chance = scaled_bonus(core, "weaponChance", 12);
    bonuses.maximum_damage = scaled_bonus(core, "weaponDmg", 2);
    bonuses.block_chance = scaled_bonus(core, "dodge", 9);
    bonuses.damage_resistance = scaled_bonus(core, "barkSkin", 1);
    return bonuses;
}

}
}
