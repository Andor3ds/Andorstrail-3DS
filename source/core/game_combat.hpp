#ifndef ANDORSTRAIL_GAME_COMBAT_HPP
#define ANDORSTRAIL_GAME_COMBAT_HPP

#include "game_core.h"
#include "game_resources.h"

#include <string>

namespace andortrail {
namespace combat {

struct AttackResult {
    int enemy_health;
    int enemy_max_health;
    int damage_dealt;
    int damage_taken;
    int experience_gained;
    bool player_hit;
    bool enemy_defeated;
    bool player_knocked_out;
};

bool attack_monster(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error
);

}
}

#endif
