#ifndef ANDORSTRAIL_GAME_COMBAT_HPP
#define ANDORSTRAIL_GAME_COMBAT_HPP

#include "game_core.h"
#include "game_resources.h"

#include <string>
#include <vector>

namespace andortrail {
namespace combat {

struct AttackResult {
    int enemy_health;
    int enemy_max_health;
    int damage_dealt;
    int damage_taken;
    int experience_gained;
    int health_restored;
    int player_action_points_change;
    int enemy_action_points_change;
    bool player_hit;
    bool player_critical_hit;
    bool enemy_critical_hit;
    bool enemy_defeated;
    bool player_knocked_out;
};

struct MonsterAttackResult {
    bool hit;
    bool critical_hit;
    int damage_taken;
    int player_action_points_change;
    int monster_action_points_change;
    bool player_knocked_out;
};

struct ConditionState {
    std::string id;
    int magnitude;
    int duration;
    bool immunity;
};

struct PlayerCombatStats {
    int maximum_health;
    int maximum_action_points;
    int move_cost;
    int attack_cost;
    int use_item_cost;
    int reequip_cost;
    int attack_chance;
    int minimum_damage;
    int maximum_damage;
    int critical_skill;
    double critical_multiplier;
    int block_chance;
    int damage_resistance;
};

bool player_combat_stats(GameResources *resources, const GameCore *core,
    PlayerCombatStats &stats, std::string &error);
bool monster_difficulty(GameResources *resources, const GameCore *core,
    const std::string &monster_id, int &difficulty, std::string &error);
bool use_item(GameResources *resources, GameCore *core, const std::string &item_id,
    int &action_points, int maximum_action_points, int &health_change,
    std::string &error);
bool finish_combat_round(GameResources *resources, GameCore *core,
    int &action_points, int maximum_action_points, bool adjacent_enemy,
    std::string &error, bool full_round = false,
    bool apply_round_skills = true, bool apply_regeneration = true);
bool finish_enemy_combat_round(GameResources *resources, GameCore *core,
    const std::string &monster_id, const std::string &drop_list_id,
    const std::string &instance_id, int maximum_health, int &health,
    int maximum_action_points, int &action_points,
    std::vector<ConditionState> &conditions, AttackResult &result,
    std::string &error, bool full_round = false);

bool attack_monster(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error,
    std::vector<ConditionState> *enemy_conditions = NULL
);

bool attack_monster_once(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    const std::string &drop_list_id,
    const std::string &instance_id,
    int player_damage,
    AttackResult &result,
    std::string &error,
    std::vector<ConditionState> *enemy_conditions = NULL
);

bool monster_attack_player(
    GameResources *resources,
    GameCore *core,
    const std::string &monster_id,
    MonsterAttackResult &result,
    std::string &error,
    std::vector<ConditionState> *monster_conditions = NULL,
    int *monster_health = NULL,
    int maximum_monster_health = 0
);

}
}

#endif
