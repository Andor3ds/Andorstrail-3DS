#ifndef ANDORSTRAIL_3DS_MAP_RENDERER_HPP
#define ANDORSTRAIL_3DS_MAP_RENDERER_HPP

#include "game_dialogue.hpp"
#include "game_combat.hpp"
#include "game_map.hpp"
#include "game_resources.h"

#include <citro2d.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace andortrail {
namespace platform3ds {

class MapRenderer {
public:
    MapRenderer();
    ~MapRenderer();

    bool initialize(const char *maps_directory, const char *initial_map_id,
        const char *tileset_list_path, const char *texture_directory,
        GameResources *resources, GameCore *core, std::string &error);
    bool persist_state(std::string &error);
    void dispose();
    void draw(int screen_width, int screen_height,
        float scaling_factor = 1.0f) const;
    void draw_player_portrait(float x, float y, float depth) const;
    void update(std::uint64_t current_time_ms);
    bool move_player(int dx, int dy);
    bool move_player_toward(map::GridPosition destination);
    bool interact();
    bool can_talk_to_mikhail() const;
    bool can_shop_with_mara() const;
    bool can_attack_with_action_button() const;
    bool cycle_combat_target(int direction);
    bool selected_combat_target(std::string &name, int &health,
        int &maximum_health) const;
    void draw_selected_combat_target_icon(float x, float y, float depth) const;
    const char *nearby_interaction_hint() const;
    bool attack_nearby_enemy();
    bool end_combat_turn();
    bool use_or_equip_item(const std::string &item_id, std::string &error,
        int target_equipment_slot = -1);
    bool unequip_equipment(GameCoreEquipmentSlot slot, std::string &error);
    bool is_in_combat() const;
    int player_action_points() const;
    int displayed_player_action_points() const;
    int maximum_player_action_points() const;
    bool has_modal_interaction() const;
    bool has_encounter() const;
    bool has_encounter_info() const;
    int selected_encounter_option() const;
    void move_encounter_selection(int direction);
    bool choose_encounter_option(int option);
    void return_to_encounter();
    void dismiss_modal_interaction();
    bool has_conversation() const;
    bool has_shop() const;
    bool buy_shop_bread(std::string &error);
    const std::vector<dialogue::Reply> &conversation_replies() const;
    std::size_t selected_conversation_reply() const;
    void move_conversation_selection(int direction);
    bool choose_conversation_reply(std::string &error);
    const std::string &interaction_message() const;
    map::GridPosition player_position() const;
    map::ViewTransform camera(int screen_width, int screen_height,
        float scaling_factor = 1.0f) const;
    bool player_walkable() const;
    const char *map_id() const;

private:
    struct EnemyState {
        std::string monster_id;
        std::string drop_list_id;
        std::string name;
        std::string phrase_id;
        std::string faction_id;
        std::string icon_sheet;
        std::string instance_id;
        map::Rect spawn_area;
        map::GridPosition position;
        map::GridPosition destination;
        std::size_t spawn_area_index;
        unsigned int icon_index;
        int health;
        int maximum_health;
        int action_points;
        int maximum_action_points;
        int attack_cost;
        int move_cost;
        int attack_chance;
        int aggression_type;
        std::vector<combat::ConditionState> conditions;
        bool critical_immune;
        bool ignore_areas;
        bool force_aggressive;
        bool aggressive;
        bool unique;
        bool has_destination;
        std::uint64_t next_action_time_ms;
    };

    bool load_tileset_sheets(const char *tileset_list_path,
        const char *texture_directory, std::string &error);
    bool load_map(const std::string &map_id, const std::string &place_id,
        map::EventKind place_kind, int offset_x, int offset_y, std::string &error,
        bool persist_current_map = true);
    bool prepare_tile_sources(const map::Document &document, std::string &error);
    bool validate_visible_layers(const map::Document &document, std::string &error) const;
    bool handle_map_event(const map::Event &event, map::GridPosition entered,
        std::string &error);
    bool interact_with_mikhail();
    bool interact_with_mara();
    bool interact_with_enemy(EnemyState &enemy);
    bool handle_conversation_terminal(std::string &error);
    bool apply_conversation_spawn_effects(std::string &error);
    bool transition(const map::Event &event, map::GridPosition entered,
        std::string &error);
    void show_sign(const map::Event &event);
    void show_container(const map::Event &event);
    void show_key_requirement(const map::Event &event);
    bool initialize_enemies(std::string &error);
    void update_enemies(std::uint64_t current_time_ms);
    bool initialize_enemy(const map::SpawnArea &area, int spawn_index,
        const map::GridPosition &position, const GameResourceRecordEntry *record,
        EnemyState &enemy);
    void refresh_enemy_aggressiveness();
    bool respawn_enemy(std::size_t area_index, std::string &error);
    bool update_spawn_areas(std::uint64_t current_time_ms, std::string &error);
    bool persist_enemy_conditions(const EnemyState &enemy, std::string &error);
    bool persist_enemy_defeat(const EnemyState &enemy, std::string &error);
    bool restore_enemy_conditions(EnemyState &enemy, std::string &error);
    bool persist_all_enemy_conditions(std::string &error);
    bool persist_current_map_state(bool update_visit_time, std::string &error);
    bool reset_encounters_after_time_passed(std::string &error);
    bool combat_action_completed(std::string &message);
    void select_adjacent_enemy_if_needed();
    bool apply_enemy_condition_round(EnemyState &enemy, bool full_round,
        combat::AttackResult &result, std::string &error);
    bool apply_enemy_round_on_combat_exit(std::string &message,
        std::string &error);
    bool apply_out_of_combat_round(bool full_round, std::string &message,
        std::string &error);
    bool reset_maps_not_recently_visited(std::string &error);
    void handle_player_knockout(std::string &message);
    void reset_world_round_timers();
    void resolve_monster_turn(std::string &message,
        bool first_turn_of_combat = false);
    void advance_monster_turn(std::uint64_t current_time_ms);
    void finish_monster_turn(std::string &message);
    bool check_monster_aggression();
    void begin_encounter_with_enemy(EnemyState &enemy);
    void update_enemy_turn(EnemyState &enemy, std::string &message);
    bool enemy_should_move_in_combat(const EnemyState &enemy) const;
    bool has_adjacent_aggressive_enemy() const;
    bool npc_at(map::GridPosition position) const;
    bool enemy_at(map::GridPosition position) const;
    bool enemy_can_move_to(const EnemyState &enemy,
        map::GridPosition position) const;
    bool find_enemy_path_step(const EnemyState &enemy,
        map::GridPosition destination, map::GridPosition &next_step) const;
    void draw_layer(const std::string &name, const map::ViewTransform &view,
        float depth, float scale) const;
    void draw_gid(std::uint32_t gid, int screen_x, int screen_y, float depth,
        float scale) const;
    void draw_actor_icon(const char *sheet_name, std::uint32_t icon_index,
        map::GridPosition position, const map::ViewTransform &view, float depth) const;
    void draw_actor_icon_at(const char *sheet_name, std::uint32_t icon_index,
        float x, float y, float depth, float scale = 1.0f) const;

    map::Document document_;
    map::TranslatedDocument translated_;
    map::GridPosition player_;
    std::string maps_directory_;
    std::string interaction_message_;
    std::string conversation_enemy_instance_id_;
    GameResources *resources_;
    GameCore *core_;
    dialogue::Conversation conversation_;
    std::size_t selected_conversation_reply_;
    std::vector<C2D_SpriteSheet> sheets_;
    std::vector<std::string> image_names_;
    std::map<std::string, std::size_t> image_indices_;
    std::map<std::string, std::vector<Tex3DS_SubTexture> > tile_subtextures_;
    std::map<std::pair<std::string, std::uint32_t>, std::uint32_t>
        actor_icon_slots_;
    C2D_Image player_image_;
    mutable bool map_textures_filtered_;
    bool modal_interaction_;
    bool shop_interaction_;
    bool initialized_;
    std::vector<EnemyState> enemies_;
    std::string selected_enemy_instance_id_;
    std::vector<std::vector<const GameResourceRecordEntry *> > spawn_candidates_;
    std::vector<bool> spawn_area_active_;
    std::vector<bool> spawn_area_remove_all_pending_;
    std::vector<bool> spawn_area_spawn_all_pending_;
    std::vector<bool> spawn_area_unique_;
    std::vector<bool> spawn_area_has_npc_;
    int player_action_points_;
    int maximum_player_action_points_;
    int last_player_action_ap_;
    int last_player_action_max_ap_;
    std::uint64_t last_player_action_time_ms_;
    int player_attack_cost_;
    int player_move_cost_;
    int player_use_item_cost_;
    int player_reequip_cost_;
    bool combat_active_;
    bool monster_turn_active_;
    bool encounter_pending_;
    bool encounter_info_open_;
    int encounter_selection_;
    std::string encounter_difficulty_;
    std::string declined_enemy_instance_id_;
    std::string active_combat_enemy_id_;
    std::uint64_t next_combat_enemy_action_ms_;
    std::uint64_t next_respawn_check_ms_;
    std::uint64_t last_round_update_ms_;
    std::uint64_t round_tick_accumulator_ms_;
    unsigned int ticks_until_next_round_;
    unsigned int ticks_until_next_full_round_;
    bool has_round_update_time_;
};

}
}

#endif