#ifndef ANDORSTRAIL_GAME_SKILLS_HPP
#define ANDORSTRAIL_GAME_SKILLS_HPP

#include "game_core.h"

#include <cstddef>

namespace andortrail {
namespace skills {

struct Definition {
    const char *id;
    const char *name;
    const char *description;
    int maximum_level;
    int required_player_level_per_rank;
    int required_player_level_offset;
    const char *required_skill_a;
    int required_skill_a_per_rank;
    const char *required_skill_b;
    int required_skill_b_per_rank;
    const char *required_skill_c;
    int required_skill_c_per_rank;
    int required_block_chance_per_rank;
    int required_max_health_per_rank;
    int required_max_health_offset;
};

struct CombatBonuses {
    int attack_chance;
    int maximum_damage;
    int block_chance;
    int damage_resistance;
};

const Definition *visible_definitions(std::size_t &count);
bool can_level_up(const GameCore *core, const Definition &definition);
bool level_up(GameCore *core, const Definition &definition);
CombatBonuses combat_bonuses(const GameCore *core);

}
}

#endif
