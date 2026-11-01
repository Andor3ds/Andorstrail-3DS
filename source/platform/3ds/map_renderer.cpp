#include "map_renderer.hpp"
#include "game_combat.hpp"
#include "game_items.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <new>
#include <sstream>
#include <vector>

namespace andortrail {
namespace platform3ds {
namespace {

const std::uint64_t MAP_UNVISITED_RESPAWN_DURATION_MS = 3 * 60 * 1000;
const std::uint64_t GAME_ROUND_TICK_DURATION_MS = 500;
const std::uint64_t COMBAT_ACTION_INTERVAL_MS = 1000;
const unsigned int TICKS_PER_ROUND = 12;
const unsigned int TICKS_PER_FULL_ROUND = 50;

std::uint64_t current_epoch_time_ms()
{
    const std::chrono::milliseconds now =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    return now.count() < 0 ? 0 : static_cast<std::uint64_t>(now.count());
}

std::string basename_of(const std::string &path)
{
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool valid_map_id(const std::string &map_id)
{
    if (map_id.empty()) return false;
    for (unsigned char character : map_id) {
        if (!std::isalnum(character) && character != '_' && character != '-') return false;
    }
    return true;
}

bool contains(const map::Rect &rect, map::GridPosition point)
{
    return point.x >= rect.x && point.y >= rect.y
        && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

bool monster_can_move_to(const map::Document &document,
    const map::TranslatedDocument &translated, map::GridPosition position,
    bool ignore_areas)
{
    if (!map::is_walkable(document, position.x, position.y)) return false;
    if (ignore_areas) return true;
    for (const map::Event &event : translated.events) {
        if (!event.active || !(event.kind == map::EventKind::MapChange
                || event.kind == map::EventKind::Key
                || event.kind == map::EventKind::Rest)) continue;
        if (contains(event.tile_rect, position)) return false;
    }
    return true;
}

int distance_to_tile(map::GridPosition from, map::GridPosition to)
{
    return std::abs(from.x - to.x) + std::abs(from.y - to.y);
}

const map::SpawnArea *find_spawn_area(
    const map::TranslatedDocument &translated,
    const std::string &spawn_group
)
{
    for (const map::SpawnArea &area : translated.spawn_areas) {
        if (area.active_for_new_game && area.spawn_group == spawn_group) return &area;
    }
    return NULL;
}

bool spawn_position(
    const map::Document &document,
    const map::TranslatedDocument &translated,
    const std::string &spawn_group,
    int spawn_index,
    map::GridPosition &position
)
{
    const map::SpawnArea *area = find_spawn_area(translated, spawn_group);
    if (area == NULL || spawn_index < 0 || spawn_index >= area->quantity) return false;

    int walkable_count = 0;
    for (int y = area->tile_rect.y; y < area->tile_rect.y + area->tile_rect.height; ++y) {
        for (int x = area->tile_rect.x; x < area->tile_rect.x + area->tile_rect.width; ++x) {
            if (!map::is_walkable(document, x, y)) continue;
            if (walkable_count++ == spawn_index) {
                position = {x, y};
                return true;
            }
        }
    }
    return false;
}

bool phrase_message(GameResources *resources, const std::string &phrase_id, std::string &message)
{
    if (resources == NULL || phrase_id.empty()) return false;
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONVERSATION, phrase_id.c_str(), NULL
    );
    if (record == NULL) return false;
    GameJsonDocument document;
    GameJsonRef phrase;
    if (!game_resources_open_record(resources, record, &document, &phrase)) return false;
    char source[2048];
    const bool copied = game_json_string_copy(
        game_json_object_get(phrase, "message"), source, sizeof(source)
    );
    if (copied) message.assign(source);
    game_json_dispose(&document);
    return copied;
}

bool json_integer_or_default(GameJsonRef object, const char *key, int fallback, int &value)
{
    const GameJsonRef field = game_json_object_get(object, key);
    if (field.node == GAME_JSON_NO_NODE || game_json_is_null(field)) {
        value = fallback;
        return true;
    }
    int64_t parsed = 0;
    if (!game_json_integer(field, &parsed) || parsed < 0 || parsed > INT_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool json_string_or_empty(GameJsonRef object, const char *key, std::string &value)
{
    const GameJsonRef field = game_json_object_get(object, key);
    if (field.node == GAME_JSON_NO_NODE || game_json_is_null(field)) {
        value.clear();
        return true;
    }
    char buffer[256];
    if (!game_json_string_copy(field, buffer, sizeof(buffer))) return false;
    value.assign(buffer);
    return true;
}

bool string_equals_ignore_case(const std::string &left, const std::string &right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
                != std::tolower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

}

MapRenderer::MapRenderer()
    : player_({0, 0}), resources_(NULL), core_(NULL), conversation_(NULL, NULL),
      selected_conversation_reply_(0), player_image_({NULL, NULL}),
      modal_interaction_(false), shop_interaction_(false), initialized_(false),
      player_action_points_(0), maximum_player_action_points_(10),
      last_player_action_ap_(0), last_player_action_max_ap_(10),
      last_player_action_time_ms_(0),
      player_attack_cost_(4), player_move_cost_(6), player_use_item_cost_(5),
      player_reequip_cost_(5),
      combat_active_(false), monster_turn_active_(false),
      next_combat_enemy_action_ms_(0), combat_enemy_actions_(0),
      next_respawn_check_ms_(0),
      last_round_update_ms_(0), round_tick_accumulator_ms_(0),
      ticks_until_next_round_(TICKS_PER_ROUND),
      ticks_until_next_full_round_(TICKS_PER_FULL_ROUND),
      has_round_update_time_(false)
{
}

MapRenderer::~MapRenderer()
{
    dispose();
}

void MapRenderer::dispose()
{
    for (C2D_SpriteSheet sheet : sheets_) {
        if (sheet != NULL) C2D_SpriteSheetFree(sheet);
    }
    sheets_.clear();
    image_names_.clear();
    image_indices_.clear();
    tile_subtextures_.clear();
    document_ = map::Document();
    translated_ = map::TranslatedDocument();
    maps_directory_.clear();
    interaction_message_.clear();
    conversation_.set_context(NULL, NULL);
    resources_ = NULL;
    core_ = NULL;
    selected_conversation_reply_ = 0;
    player_image_ = {NULL, NULL};
    modal_interaction_ = false;
    shop_interaction_ = false;
    enemies_.clear();
    spawn_candidates_.clear();
    spawn_area_unique_.clear();
    spawn_area_has_npc_.clear();
    player_action_points_ = 0;
    maximum_player_action_points_ = 10;
    last_player_action_ap_ = 0;
    last_player_action_max_ap_ = 10;
    last_player_action_time_ms_ = 0;
    player_attack_cost_ = 4;
    player_move_cost_ = 6;
    player_use_item_cost_ = 5;
    player_reequip_cost_ = 5;
    combat_active_ = false;
    monster_turn_active_ = false;
    active_combat_enemy_id_.clear();
    next_combat_enemy_action_ms_ = 0;
    combat_enemy_actions_ = 0;
    selected_enemy_instance_id_.clear();
    next_respawn_check_ms_ = 0;
    last_round_update_ms_ = 0;
    round_tick_accumulator_ms_ = 0;
    ticks_until_next_round_ = TICKS_PER_ROUND;
    ticks_until_next_full_round_ = TICKS_PER_FULL_ROUND;
    has_round_update_time_ = false;
    initialized_ = false;
}

bool MapRenderer::initialize(
    const char *maps_directory,
    const char *initial_map_id,
    const char *tileset_list_path,
    const char *texture_directory,
    GameResources *resources,
    GameCore *core,
    std::string &error
)
{
    dispose();
    if (maps_directory == NULL || initial_map_id == NULL || tileset_list_path == NULL
            || texture_directory == NULL || resources == NULL || core == NULL) {
        error = "invalid map renderer configuration";
        return false;
    }
    maps_directory_ = maps_directory;
    resources_ = resources;
    core_ = core;
    conversation_.set_context(resources_, core_);
    if (!items::reconcile_equipment_conditions(resources_, core_, error)) {
        dispose();
        return false;
    }
    if (!load_tileset_sheets(tileset_list_path, texture_directory, error)) {
        dispose();
        return false;
    }

    const std::map<std::string, std::size_t>::const_iterator hero_index =
        image_indices_.find("char_hero.png");
    if (hero_index == image_indices_.end()) {
        error = "original player image is missing from the tileset list";
        dispose();
        return false;
    }
    player_image_ = C2D_SpriteSheetGetImage(sheets_[hero_index->second], 0);
    if (player_image_.subtex == NULL || player_image_.subtex->width != 32
            || player_image_.subtex->height != 32) {
        error = "original Android player image is not one 32-pixel tile";
        dispose();
        return false;
    }

    const char *saved_map = core_->current_map_id[0] == '\0'
        ? initial_map_id : core_->current_map_id;
    if (!load_map(saved_map, "rest", map::EventKind::Rest, 0, 0, error)) {
        dispose();
        return false;
    }
    initialized_ = true;
    error.clear();
    return true;
}

bool MapRenderer::load_map(
    const std::string &map_id,
    const std::string &place_id,
    map::EventKind place_kind,
    int offset_x,
    int offset_y,
    std::string &error,
    bool persist_current_map
)
{
    if (!valid_map_id(map_id)) {
        error = "invalid source map ID";
        return false;
    }
    map::Document next_document;
    const std::string map_path = maps_directory_ + "/" + map_id + ".tmx";
    if (!map::load_tmx(map_path, map_id, true, next_document, &error)) return false;
    map::TranslatedDocument next_translated;
    if (!map::translate_document(next_document, next_translated, &error)
            || !prepare_tile_sources(next_document, error)
            || !validate_visible_layers(next_document, error)) return false;

    const map::Event *entry = NULL;
    for (const map::Event &event : next_translated.events) {
        if (event.kind == place_kind && event.active && event.name == place_id) {
            entry = &event;
            break;
        }
    }
    map::GridPosition entry_position = {0, 0};
    bool has_entry_position = false;
    if (core_ != NULL && core_->current_map_id[0] != '\0'
            && map_id == core_->current_map_id) {
        const map::GridPosition saved_position = {
            core_->current_map_x, core_->current_map_y
        };
        if (saved_position.x >= 0 && saved_position.y >= 0
                && saved_position.x < next_document.width
                && saved_position.y < next_document.height
                && map::is_walkable(next_document, saved_position.x,
                    saved_position.y)) {
            entry_position = saved_position;
            has_entry_position = true;
        }
    }
    if (!has_entry_position && entry != NULL && entry->tile_rect.width > 0
            && entry->tile_rect.height > 0) {
        entry_position = {
            entry->tile_rect.x + std::min(offset_x, entry->tile_rect.width - 1),
            entry->tile_rect.y + std::min(offset_y, entry->tile_rect.height - 1),
        };
        has_entry_position = true;
    }
    if (!has_entry_position) {
        error = "source map place not found: " + map_id + ":" + place_id;
        return false;
    }
    if (!map::is_walkable(next_document, entry_position.x, entry_position.y)) {
        bool found_fallback = false;
        for (const map::Event &event : next_translated.events) {
            if (event.kind != map::EventKind::MapChange || !event.active) continue;
            if (map::is_walkable(next_document, event.tile_rect.x, event.tile_rect.y)) {
                entry_position = {event.tile_rect.x, event.tile_rect.y};
                found_fallback = true;
                break;
            }
        }
        if (!found_fallback) {
            error = "source entry place is not walkable and map has no walkable mapchange fallback";
            return false;
        }
    }

    if (persist_current_map && !persist_current_map_state(true, error)) return false;
    document_ = std::move(next_document);
    translated_ = std::move(next_translated);
    player_ = entry_position;
    return initialize_enemies(error);
}

bool MapRenderer::initialize_enemies(std::string &error)
{
    enemies_.clear();
    selected_enemy_instance_id_.clear();
    spawn_candidates_.assign(translated_.spawn_areas.size(),
        std::vector<const GameResourceRecordEntry *>());
    spawn_area_unique_.assign(translated_.spawn_areas.size(), false);
    spawn_area_has_npc_.assign(translated_.spawn_areas.size(), false);
    next_respawn_check_ms_ = 0;
    player_action_points_ = 0;
    andortrail::combat::PlayerCombatStats player_stats;
    if (!andortrail::combat::player_combat_stats(resources_, core_,
            player_stats, error)) return false;
    maximum_player_action_points_ = player_stats.maximum_action_points;
    core_->health = std::min(core_->health, player_stats.maximum_health);
    player_attack_cost_ = player_stats.attack_cost;
    player_move_cost_ = player_stats.move_cost;
    player_use_item_cost_ = player_stats.use_item_cost;
    player_reequip_cost_ = player_stats.reequip_cost;
    combat_active_ = false;
    selected_enemy_instance_id_.clear();
    std::map<std::string, std::vector<const GameResourceRecordEntry *> > records_by_group;
    std::map<std::uint16_t, std::map<std::uint32_t, const GameResourceRecordEntry *> >
        records_by_document;
    for (std::size_t index = 0; index < game_resource_record_count; ++index) {
        const GameResourceRecordEntry &record = game_resource_records[index];
        if (record.type != GAME_RESOURCE_MONSTER) continue;
        records_by_document[record.document_index][record.record_index] = &record;
    }
    for (std::map<std::uint16_t,
            std::map<std::uint32_t, const GameResourceRecordEntry *> >::const_iterator
            document_records = records_by_document.begin();
            document_records != records_by_document.end(); ++document_records) {
        GameJsonDocument resource_document;
        if (!game_resources_open_document(resources_, document_records->first,
                &resource_document)) {
            error = "monster resource document could not be read";
            return false;
        }
        const GameJsonRef records = game_json_root(&resource_document);
        for (std::map<std::uint32_t, const GameResourceRecordEntry *>::const_iterator
                record = document_records->second.begin();
                record != document_records->second.end(); ++record) {
            std::string group;
            if (json_string_or_empty(game_json_array_get(records, record->first),
                    "spawnGroup", group) && !group.empty()) {
                records_by_group[group].push_back(record->second);
            }
        }
        game_json_dispose(&resource_document);
    }

    std::vector<int> spawn_starts(translated_.spawn_areas.size(), 0);
    int global_spawn_index = 0;
    for (std::size_t area_index = 0;
            area_index < translated_.spawn_areas.size(); ++area_index) {
        const map::SpawnArea &area = translated_.spawn_areas[area_index];
        const int area_spawn_start = global_spawn_index;
        spawn_starts[area_index] = area_spawn_start;
        if (area.quantity > 0 && global_spawn_index <= INT_MAX - area.quantity) {
            global_spawn_index += area.quantity;
        }
        if (!area.active_for_new_game) continue;
        std::vector<const GameResourceRecordEntry *> candidates;
        for (std::map<std::string, std::vector<const GameResourceRecordEntry *> >::const_iterator
                group = records_by_group.begin(); group != records_by_group.end(); ++group) {
            if (string_equals_ignore_case(group->first, area.spawn_group)) {
                candidates = group->second;
                break;
            }
        }
        if (candidates.empty()) {
            const GameResourceRecordEntry *direct = game_resources_find(
                GAME_RESOURCE_MONSTER, area.spawn_group.c_str(), NULL
            );
            if (direct != NULL) candidates.push_back(direct);
        }
        spawn_candidates_[area_index] = candidates;
        if (!candidates.empty()) {
            GameJsonDocument first_monster_document;
            GameJsonRef first_monster = {NULL, GAME_JSON_NO_NODE};
            int unique = 0;
            if (!game_resources_open_record(resources_, candidates[0],
                    &first_monster_document, &first_monster)) {
                error = std::string("spawn group data is invalid: ")
                    + area.spawn_group;
                return false;
            }
            const bool valid_unique = json_integer_or_default(
                first_monster, "unique", 0, unique);
            std::string phrase_id;
            const bool valid_phrase = json_string_or_empty(first_monster,
                "phraseID", phrase_id);
            game_json_dispose(&first_monster_document);
            if (!valid_unique || !valid_phrase) {
                error = std::string("spawn group data is invalid: ")
                    + area.spawn_group;
                return false;
            }
            spawn_area_unique_[area_index] = unique > 0;
            spawn_area_has_npc_[area_index] = !phrase_id.empty();
        }
    }

    uint64_t last_visit_time_ms = 0;
    bool visited = false;
    const bool has_map_state = game_core_get_map_state(core_,
        document_.name.c_str(), &last_visit_time_ms, &visited);
    const uint64_t now_ms = current_epoch_time_ms();
    const bool recently_visited = has_map_state && last_visit_time_ms != 0
        && (now_ms < last_visit_time_ms
            || now_ms - last_visit_time_ms < MAP_UNVISITED_RESPAWN_DURATION_MS);
    const std::unique_ptr<GameCoreSavedEnemy[]> saved_enemies(
        new (std::nothrow) GameCoreSavedEnemy[GAME_CORE_MAX_SAVED_ENEMIES]());
    if (!saved_enemies) {
        error = "saved monster state could not be allocated";
        return false;
    }
    const std::size_t saved_enemy_count = game_core_get_map_enemies(core_,
        document_.name.c_str(), saved_enemies.get(), GAME_CORE_MAX_SAVED_ENEMIES);
    if (saved_enemy_count > GAME_CORE_MAX_SAVED_ENEMIES) {
        error = "saved map has too many monsters";
        return false;
    }

    for (std::size_t saved_index = 0; saved_index < saved_enemy_count;
            ++saved_index) {
        GameCoreSavedEnemy saved = saved_enemies[saved_index];
        std::size_t area_index = saved.spawn_area_index >= 0
                && static_cast<std::size_t>(saved.spawn_area_index)
                    < translated_.spawn_areas.size()
            ? static_cast<std::size_t>(saved.spawn_area_index)
            : translated_.spawn_areas.size();
        const GameResourceRecordEntry *monster_record = NULL;
        if (area_index < translated_.spawn_areas.size()) {
            const std::vector<const GameResourceRecordEntry *> &candidates =
                spawn_candidates_[area_index];
            for (const GameResourceRecordEntry *candidate : candidates) {
                if (saved.monster_id == candidate->id) {
                    monster_record = candidate;
                    break;
                }
            }
        }
        if (monster_record == NULL) {
            for (std::size_t candidate_area = 0;
                    candidate_area < spawn_candidates_.size()
                        && monster_record == NULL; ++candidate_area) {
                for (const GameResourceRecordEntry *candidate :
                        spawn_candidates_[candidate_area]) {
                    if (saved.monster_id == candidate->id) {
                        area_index = candidate_area;
                        monster_record = candidate;
                        break;
                    }
                }
            }
        }
        if (monster_record == NULL) {
            if (!game_core_set_enemy_conditions(core_, document_.name.c_str(),
                    saved.instance_id, NULL, 0)) {
                error = "obsolete monster conditions could not be cleared: "
                    + std::string(saved.instance_id);
                return false;
            }
            continue;
        }
        saved.spawn_area_index = static_cast<int>(area_index);
        const map::SpawnArea &area = translated_.spawn_areas[area_index];
        const std::string defeated_key =
            std::string(saved.instance_id) + "_defeated";
        if (spawn_area_unique_[area_index]
                && game_core_quest_progress(core_, defeated_key.c_str()) > 0) {
            if (!game_core_set_enemy_conditions(core_, document_.name.c_str(),
                    saved.instance_id, NULL, 0)) {
                error = "defeated monster conditions could not be cleared: "
                    + std::string(saved.instance_id);
                return false;
            }
            continue;
        }
        if (!recently_visited && !spawn_area_unique_[area_index]) {
            if (!game_core_set_enemy_conditions(core_, document_.name.c_str(),
                    saved.instance_id, NULL, 0)) {
                error = "reset monster conditions could not be cleared";
                return false;
            }
            continue;
        }
        if (saved.health <= 0
                || !monster_can_move_to(document_, translated_,
                    {saved.x, saved.y}, area.ignore_areas)
                || (saved.x == player_.x && saved.y == player_.y)
                || enemy_at({saved.x, saved.y})) {
            error = "saved monster position or health is invalid: "
                + std::string(saved.instance_id);
            return false;
        }
        EnemyState enemy;
        const map::GridPosition position = {saved.x, saved.y};
        if (!initialize_enemy(area, spawn_starts[area_index], position,
                monster_record, enemy)
                || saved.health > enemy.maximum_health
                || saved.action_points > enemy.maximum_action_points
                || saved.unique != spawn_area_unique_[area_index]) {
            error = "saved monster state is incompatible with its record: "
                + std::string(saved.instance_id);
            return false;
        }
        enemy.spawn_area_index = area_index;
        enemy.instance_id = saved.instance_id;
        enemy.health = saved.health;
        enemy.action_points = saved.action_points;
        enemy.unique = spawn_area_unique_[area_index];
        if (!restore_enemy_conditions(enemy, error)) return false;
        enemies_.push_back(enemy);
    }

    const bool training_rats_need_seed =
        game_core_quest_progress(core_, "mikhail_rats_spawned") == 0;
    for (std::size_t area_index = 0;
            area_index < translated_.spawn_areas.size(); ++area_index) {
        const map::SpawnArea &area = translated_.spawn_areas[area_index];
        const std::vector<const GameResourceRecordEntry *> &candidates =
            spawn_candidates_[area_index];
        const bool seed_area = !recently_visited
            || area.spawn_group == "trainingrat"
            || spawn_area_has_npc_[area_index];
        if (!seed_area || !area.active_for_new_game || candidates.empty()
                || (spawn_area_unique_[area_index] && visited
                    && !spawn_area_has_npc_[area_index])) continue;

        std::size_t alive_in_area = 0;
        for (const EnemyState &enemy : enemies_) {
            if (enemy.spawn_area_index == area_index && enemy.health > 0) {
                ++alive_in_area;
            }
        }
        while (alive_in_area < static_cast<std::size_t>(area.quantity)) {
            if (area.tile_rect.width <= 0 || area.tile_rect.height <= 0) {
                error = "spawn area dimensions are invalid: " + area.name;
                return false;
            }
            map::GridPosition position = {0, 0};
            bool found_position = false;
            for (int attempt = 0; attempt < 100; ++attempt) {
                position.x = area.tile_rect.x + static_cast<int>(
                    game_core_random(core_,
                        static_cast<uint32_t>(area.tile_rect.width)));
                position.y = area.tile_rect.y + static_cast<int>(
                    game_core_random(core_,
                        static_cast<uint32_t>(area.tile_rect.height)));
                if ((position.x == player_.x && position.y == player_.y)
                        || !monster_can_move_to(document_, translated_,
                            position, area.ignore_areas)
                        || enemy_at(position)) continue;
                found_position = true;
                break;
            }
            if (!found_position) {
                break;
            }
            const std::size_t candidate_index = candidates.size() == 1 ? 0
                : static_cast<std::size_t>(game_core_random(core_,
                    static_cast<uint32_t>(candidates.size())));
            const int spawn_index = spawn_starts[area_index]
                + static_cast<int>(alive_in_area);
            EnemyState enemy;
            if (!initialize_enemy(area, spawn_index, position,
                    candidates[candidate_index], enemy)) {
                error = std::string("spawned monster record is invalid: ")
                    + candidates[candidate_index]->id;
                return false;
            }
            enemy.spawn_area_index = area_index;
            enemy.unique = spawn_area_unique_[area_index];
            if (area.spawn_group == "trainingrat") {
                bool found_rat_id = false;
                for (int rat_number = 1; rat_number <= area.quantity; ++rat_number) {
                    std::ostringstream rat_id;
                    rat_id << "mikhail_rat" << rat_number;
                    const std::string candidate_id = rat_id.str();
                    const std::string defeated_key = candidate_id + "_defeated";
                    bool already_alive = false;
                    for (const EnemyState &active : enemies_) {
                        if (active.instance_id == candidate_id) {
                            already_alive = true;
                            break;
                        }
                    }
                    if (already_alive
                            || game_core_quest_progress(core_,
                                defeated_key.c_str()) > 0) continue;
                    enemy.instance_id = candidate_id;
                    found_rat_id = true;
                    break;
                }
                if (!found_rat_id) break;
            } else if (visited) {
                if (core_->enemy_spawn_sequence == UINT32_MAX) {
                    error = "enemy respawn sequence is exhausted";
                    return false;
                }
                const uint32_t sequence = ++core_->enemy_spawn_sequence;
                std::ostringstream instance_id;
                instance_id << "enc_" << document_.name << "_"
                    << spawn_index << "_r" << sequence;
                enemy.instance_id = instance_id.str();
                if (enemy.instance_id.size() >= GAME_CORE_ID_CAPACITY) {
                    error = "respawned monster ID exceeds the supported length";
                    return false;
                }
            }
            const std::string defeated_key = enemy.instance_id + "_defeated";
            if (spawn_area_unique_[area_index]
                    && game_core_quest_progress(core_,
                        defeated_key.c_str()) > 0) break;
            if (!has_map_state && !visited) {
                const std::string health_key = enemy.instance_id + "_health";
                const int saved_health = game_core_quest_progress(core_,
                    health_key.c_str());
                if (saved_health > 0 && saved_health <= enemy.maximum_health) {
                    enemy.health = saved_health;
                }
            }
            if (!restore_enemy_conditions(enemy, error)) return false;
            enemies_.push_back(enemy);
            ++alive_in_area;
        }
        if (area.spawn_group == "trainingrat" && training_rats_need_seed
                && alive_in_area >= static_cast<std::size_t>(area.quantity)
                && !game_core_set_quest_progress(core_,
                    "mikhail_rats_spawned", 1)) {
            error = "training rat activation could not be saved";
            return false;
        }
    }

    return persist_current_map_state(true, error);
}

bool MapRenderer::initialize_enemy(const map::SpawnArea &area, int spawn_index,
    const map::GridPosition &position, const GameResourceRecordEntry *record,
    EnemyState &enemy)
{
    if (record == NULL) return false;
    GameJsonDocument document;
    GameJsonRef monster = {NULL, GAME_JSON_NO_NODE};
    if (!game_resources_open_record(resources_, record, &document, &monster)) return false;

    std::string phrase_id;
    std::string icon_id;
    std::string drop_list_id;
    std::string name;
    int maximum_health = 1;
    int maximum_action_points = 10;
    int move_cost = 10;
    int attack_cost = 10;
    int attack_chance = 0;
    int unique_value = 0;
    bool valid = json_string_or_empty(monster, "phraseID", phrase_id)
        && json_string_or_empty(monster, "iconID", icon_id)
        && json_string_or_empty(monster, "droplistID", drop_list_id)
        && json_string_or_empty(monster, "name", name)
        && json_integer_or_default(monster, "maxHP", 1, maximum_health)
        && json_integer_or_default(monster, "maxAP", 10, maximum_action_points)
        && json_integer_or_default(monster, "moveCost", 10, move_cost)
        && json_integer_or_default(monster, "attackCost", 10, attack_cost)
        && json_integer_or_default(monster, "attackChance", 0, attack_chance)
        && json_integer_or_default(monster, "unique", 0, unique_value);
    const GameJsonRef monster_class = game_json_object_get(monster, "monsterClass");
    const GameJsonRef aggression = game_json_object_get(monster, "movementAggressionType");
    std::string monster_class_name;
    std::string aggression_name;
    valid = valid && json_string_or_empty(monster, "monsterClass", monster_class_name)
        && json_string_or_empty(monster, "movementAggressionType", aggression_name);
    (void)monster_class;
    (void)aggression;
    game_json_dispose(&document);
    if (!valid || maximum_health <= 0
            || maximum_action_points <= 0 || attack_cost <= 0 || move_cost <= 0) {
        return false;
    }

    const std::size_t separator = icon_id.rfind(':');
    if (separator == std::string::npos || separator == 0
            || separator + 1 >= icon_id.size()) return false;
    char *end = NULL;
    const long icon_index = std::strtol(icon_id.c_str() + separator + 1, &end, 10);
    if (end == icon_id.c_str() + separator + 1 || *end != '\0'
            || icon_index < 0
            || static_cast<unsigned long>(icon_index) > UINT_MAX) return false;

    enemy.monster_id = record->id;
    enemy.drop_list_id = drop_list_id;
    enemy.name = name;
    enemy.icon_sheet = icon_id.substr(0, separator) + ".png";
    enemy.icon_index = static_cast<unsigned int>(icon_index);
    enemy.spawn_area = area.tile_rect;
    enemy.position = position;
    enemy.destination = position;
    enemy.spawn_area_index = 0;
    std::ostringstream instance_id;
    instance_id << "enc_" << document_.name << "_" << spawn_index;
    enemy.instance_id = instance_id.str();
    enemy.health = maximum_health;
    enemy.maximum_health = maximum_health;
    enemy.action_points = maximum_action_points;
    enemy.maximum_action_points = maximum_action_points;
    enemy.attack_cost = attack_cost;
    enemy.move_cost = move_cost;
    enemy.attack_chance = attack_chance;
    enemy.ignore_areas = area.ignore_areas;
    enemy.aggression_type = aggression_name == "wholeMap" ? 3
        : aggression_name == "protectSpawn" ? 1
        : aggression_name == "helpOthers" ? 2
        : aggression_name == "flee" ? 4 : 0;
    enemy.critical_immune = monster_class_name == "ghost"
        || monster_class_name == "construct" || monster_class_name == "demon";
    enemy.aggressive = phrase_id.empty();
    enemy.unique = unique_value > 0;
    enemy.has_destination = false;
    enemy.next_action_time_ms = 0;
    return true;
}

bool MapRenderer::persist_enemy_conditions(const EnemyState &enemy,
    std::string &error)
{
    if (core_ == NULL || document_.name.empty()
            || enemy.conditions.size() > GAME_CORE_MAX_CONDITIONS) {
        error = "enemy condition state is invalid";
        return false;
    }
    GameCoreCondition saved[GAME_CORE_MAX_CONDITIONS] = {};
    for (std::size_t index = 0; index < enemy.conditions.size(); ++index) {
        const combat::ConditionState &condition = enemy.conditions[index];
        if (condition.id.empty() || condition.id.size() >= GAME_CORE_ID_CAPACITY) {
            error = "enemy condition ID is invalid";
            return false;
        }
        std::strcpy(saved[index].id, condition.id.c_str());
        saved[index].magnitude = condition.magnitude;
        saved[index].duration = condition.duration;
        saved[index].immunity = condition.immunity;
    }
    if (!game_core_set_enemy_conditions(core_, document_.name.c_str(),
            enemy.instance_id.c_str(), saved, enemy.conditions.size())) {
        error = "enemy condition state could not be stored";
        return false;
    }
    error.clear();
    return true;
}

bool MapRenderer::persist_enemy_defeat(const EnemyState &enemy,
    std::string &error)
{
    if (enemy.unique) {
        const std::string defeated_key = enemy.instance_id + "_defeated";
        if (!game_core_set_quest_progress(core_, defeated_key.c_str(), 1)) {
            error = "defeated monster state could not be saved";
            return false;
        }
    }
    error.clear();
    return true;
}

bool MapRenderer::restore_enemy_conditions(EnemyState &enemy, std::string &error)
{
    GameCoreCondition saved[GAME_CORE_MAX_CONDITIONS] = {};
    const std::size_t count = game_core_get_enemy_conditions(core_,
        document_.name.c_str(), enemy.instance_id.c_str(), saved,
        GAME_CORE_MAX_CONDITIONS);
    if (count > GAME_CORE_MAX_CONDITIONS) {
        error = "saved enemy has too many conditions";
        return false;
    }
    enemy.conditions.clear();
    for (std::size_t index = 0; index < count; ++index) {
        combat::ConditionState condition;
        condition.id = saved[index].id;
        condition.magnitude = saved[index].magnitude;
        condition.duration = saved[index].duration;
        condition.immunity = saved[index].immunity;
        enemy.conditions.push_back(condition);
    }
    error.clear();
    return true;
}

bool MapRenderer::persist_all_enemy_conditions(std::string &error)
{
    for (const EnemyState &enemy : enemies_) {
        if (!persist_enemy_conditions(enemy, error)) return false;
    }
    error.clear();
    return true;
}

bool MapRenderer::persist_state(std::string &error)
{
    return persist_current_map_state(false, error);
}

bool MapRenderer::persist_current_map_state(bool update_visit_time,
    std::string &error)
{
    if (core_ == NULL) {
        error = "current map state is unavailable";
        return false;
    }
    if (document_.name.empty()) {
        error.clear();
        return true;
    }
    if (!persist_all_enemy_conditions(error)) return false;
    std::vector<GameCoreSavedEnemy> saved_enemies;
    saved_enemies.reserve(enemies_.size());
    for (const EnemyState &enemy : enemies_) {
        if (enemy.health <= 0) continue;
        if (enemy.spawn_area_index >= translated_.spawn_areas.size()) {
            error = "monster references a missing spawn area: "
                + enemy.instance_id;
            return false;
        }
        GameCoreSavedEnemy saved = {};
        saved.spawn_area_index = static_cast<int>(enemy.spawn_area_index);
        saved.x = enemy.position.x;
        saved.y = enemy.position.y;
        saved.health = enemy.health;
        saved.action_points = enemy.action_points;
        saved.unique = enemy.unique;
        if (enemy.instance_id.empty()
                || enemy.instance_id.size() >= GAME_CORE_ID_CAPACITY
                || enemy.monster_id.empty()
                || enemy.monster_id.size() >= GAME_CORE_ID_CAPACITY) {
            error = "monster identity cannot be saved";
            return false;
        }
        std::strcpy(saved.instance_id, enemy.instance_id.c_str());
        std::strcpy(saved.monster_id, enemy.monster_id.c_str());
        saved_enemies.push_back(saved);
    }
    if (!game_core_set_map_enemies(core_, document_.name.c_str(),
            saved_enemies.empty() ? NULL : &saved_enemies[0],
            saved_enemies.size())) {
        error = "current map monsters could not be saved";
        return false;
    }

    uint64_t last_visit_time_ms = 0;
    bool visited = false;
    const bool has_saved_state = game_core_get_map_state(core_,
        document_.name.c_str(), &last_visit_time_ms, &visited);
    if (update_visit_time || !has_saved_state) {
        last_visit_time_ms = current_epoch_time_ms();
    }
    if (!game_core_set_map_state(core_, document_.name.c_str(),
            last_visit_time_ms, true)
            || !game_core_set_current_map(core_, document_.name.c_str(),
                player_.x, player_.y)) {
        error = "current map visit state could not be saved";
        return false;
    }
    error.clear();
    return true;
}

bool MapRenderer::reset_encounters_after_time_passed(std::string &error)
{
    if (!persist_current_map_state(false, error)) return false;
    if (!game_core_reset_map_encounters(core_)) {
        error = "map encounter state is invalid";
        return false;
    }
    if (!initialize_enemies(error)
            || !game_core_set_map_state(core_, document_.name.c_str(), 0, true)
            || !persist_current_map_state(false, error)) {
        if (error.empty()) error = "map encounters could not be reset";
        return false;
    }
    return true;
}

bool MapRenderer::apply_enemy_condition_round(EnemyState &enemy,
    bool full_round, combat::AttackResult &result, std::string &error)
{
    if (!combat::finish_enemy_combat_round(resources_, core_,
            enemy.monster_id, enemy.drop_list_id, enemy.instance_id,
            enemy.maximum_health, enemy.health, enemy.maximum_action_points,
            enemy.action_points, enemy.conditions, result, error, full_round)) {
        return false;
    }
    player_action_points_ = std::max(0, std::min(maximum_player_action_points_,
        player_action_points_ + result.player_action_points_change));
    if (result.enemy_defeated) enemy.conditions.clear();
    return persist_enemy_conditions(enemy, error);
}

bool MapRenderer::apply_enemy_round_on_combat_exit(
    std::string &message, std::string &error)
{
    for (std::size_t index = 0; index < enemies_.size();) {
        EnemyState &enemy = enemies_[index];
        if (enemy.conditions.empty()) {
            ++index;
            continue;
        }
        combat::AttackResult result;
        if (!apply_enemy_condition_round(enemy, false, result, error)) {
            error = enemy.name + ": " + error;
            return false;
        }
        if (!result.enemy_defeated) {
            ++index;
            continue;
        }
        if (!persist_enemy_defeat(enemy, error)) return false;
        message += "\n" + enemy.name + " succumbed to its conditions."
            "\nExperience gained: " + std::to_string(result.experience_gained) + ".";
        if (result.health_restored > 0) {
            message += "\nCorpse Eater restored "
                + std::to_string(result.health_restored) + " HP.";
        }
        enemies_.erase(enemies_.begin() + index);
    }
    return true;
}

bool MapRenderer::reset_maps_not_recently_visited(std::string &error)
{
    if (core_ == NULL || core_->map_state_count > GAME_CORE_MAX_MAP_STATES) {
        error = "map visit state is invalid";
        return false;
    }
    const std::uint64_t now_ms = current_epoch_time_ms();
    for (std::size_t index = 0; index < core_->map_state_count; ++index) {
        const GameCoreMapState &state = core_->map_states[index];
        if (!state.visited || state.last_visit_time_ms == 0
                || document_.name == state.id
                || now_ms < state.last_visit_time_ms
                || now_ms - state.last_visit_time_ms
                    < MAP_UNVISITED_RESPAWN_DURATION_MS) continue;
        if (!game_core_reset_map_encounters_for_map(core_, state.id)) {
            error = std::string("map encounters could not be reset: ") + state.id;
            return false;
        }
    }
    error.clear();
    return true;
}

bool MapRenderer::apply_out_of_combat_round(bool full_round,
    std::string &message, std::string &error)
{
    if (core_ == NULL || resources_ == NULL) {
        error = "round state is unavailable";
        return false;
    }
    if (full_round && !reset_maps_not_recently_visited(error)) return false;

    for (std::size_t index = 0; index < enemies_.size();) {
        EnemyState &enemy = enemies_[index];
        if (enemy.conditions.empty()) {
            ++index;
            continue;
        }
        combat::AttackResult result;
        if (!apply_enemy_condition_round(enemy, full_round, result, error)) {
            error = enemy.name + ": " + error;
            return false;
        }
        if (!result.enemy_defeated) {
            ++index;
            continue;
        }
        if (!persist_enemy_defeat(enemy, error)) return false;
        message += "\n" + enemy.name + " succumbed to its conditions."
            "\nExperience gained: " + std::to_string(result.experience_gained) + ".";
        if (result.health_restored > 0) {
            message += "\nCorpse Eater restored "
                + std::to_string(result.health_restored) + " HP.";
        }
        enemies_.erase(enemies_.begin() + index);
    }

    if (!combat::finish_combat_round(resources_, core_, player_action_points_,
            maximum_player_action_points_, false, error, full_round)) return false;
    if (core_->health <= 0) {
        handle_player_knockout(message);
        error.clear();
        return true;
    }
    return persist_current_map_state(false, error);
}

void MapRenderer::reset_world_round_timers()
{
    round_tick_accumulator_ms_ = 0;
    ticks_until_next_round_ = TICKS_PER_ROUND;
    ticks_until_next_full_round_ = TICKS_PER_FULL_ROUND;
}

void MapRenderer::handle_player_knockout(std::string &message)
{
    const int current_level_experience = game_core_experience_in_current_level(core_);
    const int loss_reduction = std::min(100,
        game_core_skill_level(core_, "lowerExploss") * 20);
    const int experience_loss = current_level_experience * 20
        * (100 - loss_reduction) / 10000;
    core_->total_experience -= experience_loss;
    game_core_restore_player(core_);
    std::string respawn_error;
    if (!persist_current_map_state(false, respawn_error)) {
        message += "\nThe current encounter state could not be saved: "
            + respawn_error;
    }
    if (!game_core_reset_map_encounters(core_)) {
        message += "\nSaved encounter state could not be reset.";
    }
    if (load_map("home", "rest", map::EventKind::Rest, 0, 0,
            respawn_error, false)) {
        if (!game_core_set_map_state(core_, "home", 0, true)
                || !persist_current_map_state(false, respawn_error)) {
            message += "\nHome encounter state could not be reset: "
                + respawn_error;
        }
        message += "\nYou were knocked out and woke at home.";
    } else {
        message += "\nYou were knocked out; returning home failed: " + respawn_error;
    }
    combat_active_ = false;
    monster_turn_active_ = false;
    player_action_points_ = 0;
    reset_world_round_timers();
    modal_interaction_ = true;
}

bool MapRenderer::enemy_at(map::GridPosition position) const
{
    for (const EnemyState &enemy : enemies_) {
        if (enemy.position.x == position.x && enemy.position.y == position.y) return true;
    }
    return false;
}

bool MapRenderer::enemy_can_move_to(const EnemyState &enemy,
    map::GridPosition position) const
{
    if (!monster_can_move_to(document_, translated_, position,
            enemy.ignore_areas)) return false;
    for (const EnemyState &other : enemies_) {
        if (&other != &enemy && other.health > 0
                && other.position.x == position.x
                && other.position.y == position.y) return false;
    }
    return true;
}

bool MapRenderer::find_enemy_path_step(const EnemyState &enemy,
    map::GridPosition destination, map::GridPosition &next_step) const
{
    return map::find_path_step(document_, enemy.position, destination,
        [this, &enemy](map::GridPosition position) {
            return (position.x != enemy.position.x
                    || position.y != enemy.position.y)
                && enemy_can_move_to(enemy, position);
        }, next_step);
}

void MapRenderer::update(std::uint64_t current_time_ms)
{
    if (!initialized_) return;

    if (monster_turn_active_) {
        advance_monster_turn(current_time_ms);
        return;
    }

    const bool world_active = !modal_interaction_ && !combat_active_;
    if (has_round_update_time_ && world_active
            && current_time_ms >= last_round_update_ms_) {
        round_tick_accumulator_ms_ += current_time_ms - last_round_update_ms_;
    }
    last_round_update_ms_ = current_time_ms;
    has_round_update_time_ = true;
    if (modal_interaction_) return;

    if (world_active) update_enemies(current_time_ms);
    if (!world_active) return;

    while (round_tick_accumulator_ms_ >= GAME_ROUND_TICK_DURATION_MS) {
        round_tick_accumulator_ms_ -= GAME_ROUND_TICK_DURATION_MS;
        if (--ticks_until_next_round_ == 0) {
            std::string error;
            if (!apply_out_of_combat_round(false, interaction_message_, error)) {
                interaction_message_ = "World round effects failed: " + error;
                modal_interaction_ = true;
                return;
            }
            ticks_until_next_round_ = TICKS_PER_ROUND;
        }
        if (--ticks_until_next_full_round_ == 0) {
            std::string error;
            if (!apply_out_of_combat_round(true, interaction_message_, error)) {
                interaction_message_ = "Full-round effects failed: " + error;
                modal_interaction_ = true;
                return;
            }
            ticks_until_next_full_round_ = TICKS_PER_FULL_ROUND;
        }
        if (combat_active_ || modal_interaction_) return;
    }
}

void MapRenderer::update_enemies(std::uint64_t current_time_ms)
{
    if (core_ == NULL || combat_active_) return;
    std::string respawn_error;
    if (!update_spawn_areas(current_time_ms, respawn_error)) {
        interaction_message_ = "Monster respawn failed: " + respawn_error;
        modal_interaction_ = true;
        return;
    }

    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        EnemyState &enemy = enemies_[index];
        const std::uint64_t movement_interval_ms = std::max<std::uint64_t>(1,
            static_cast<std::uint64_t>(1200) * enemy.move_cost
                / enemy.maximum_action_points);
        if (current_time_ms < enemy.next_action_time_ms) continue;
        enemy.next_action_time_ms = current_time_ms + movement_interval_ms;

        if (enemy.has_destination
                && enemy.position.x == enemy.destination.x
                && enemy.position.y == enemy.destination.y) {
            enemy.has_destination = false;
            enemy.next_action_time_ms = current_time_ms + movement_interval_ms
                * (1 + game_core_random(core_, 5));
            continue;
        }

        const bool chase_player = enemy.aggression_type == 3
            || (enemy.aggression_type == 1
                && contains(enemy.spawn_area, player_));
        if (!enemy.has_destination) {
            if (enemy.aggression_type == 4) {
                enemy.destination.x = std::max(0, std::min(document_.width - 1,
                    enemy.position.x + enemy.position.x - player_.x));
                enemy.destination.y = std::max(0, std::min(document_.height - 1,
                    enemy.position.y + enemy.position.y - player_.y));
            } else {
                enemy.destination = enemy.position;
                if (game_core_random(core_, 2) == 0) {
                    enemy.destination.x = enemy.spawn_area.x
                        + static_cast<int>(game_core_random(core_,
                            static_cast<std::uint32_t>(enemy.spawn_area.width)));
                } else {
                    enemy.destination.y = enemy.spawn_area.y
                        + static_cast<int>(game_core_random(core_,
                            static_cast<std::uint32_t>(enemy.spawn_area.height)));
                }
            }
            enemy.has_destination = true;
        }

        map::GridPosition next;
        const bool has_path_step = chase_player
            && find_enemy_path_step(enemy, player_, next);
        if (has_path_step) enemy.has_destination = false;
        else next = map::step_toward(enemy.position, enemy.destination);
        if (next.x == player_.x && next.y == player_.y) {
            const int aggression_chance = std::max(0,
                15 - game_core_skill_level(core_, "evasion") * 5);
            if (game_core_random(core_, 100)
                    < static_cast<std::uint32_t>(aggression_chance)) {
                combat_active_ = true;
                player_action_points_ = maximum_player_action_points_;
                selected_enemy_instance_id_ = enemy.instance_id;
                interaction_message_ = enemy.name + " engaged you.";
                resolve_monster_turn(interaction_message_, true);
                modal_interaction_ = core_->health <= 0;
                return;
            }
            enemy.has_destination = false;
            enemy.next_action_time_ms = current_time_ms + movement_interval_ms
                * (1 + game_core_random(core_, 5));
            continue;
        }

        if (enemy_can_move_to(enemy, next)) {
            enemy.position = next;
        } else {
            enemy.has_destination = false;
            enemy.next_action_time_ms = current_time_ms + movement_interval_ms
                * (1 + game_core_random(core_, 5));
        }
    }
}

bool MapRenderer::update_spawn_areas(std::uint64_t current_time_ms,
    std::string &error)
{
    if (current_time_ms < next_respawn_check_ms_) return true;
    next_respawn_check_ms_ = current_time_ms + 500;
    for (std::size_t area_index = 0;
            area_index < translated_.spawn_areas.size(); ++area_index) {
        const map::SpawnArea &area = translated_.spawn_areas[area_index];
        if (!area.active_for_new_game || area_index >= spawn_candidates_.size()
                || area_index >= spawn_area_unique_.size()
                || spawn_area_unique_[area_index]
                || spawn_candidates_[area_index].empty()) continue;
        std::size_t alive_in_area = 0;
        for (const EnemyState &enemy : enemies_) {
            if (enemy.spawn_area_index == area_index && enemy.health > 0) {
                ++alive_in_area;
            }
        }
        if (alive_in_area >= static_cast<std::size_t>(area.quantity)
                || area.respawn_speed <= 0
                || game_core_random(core_, 1000)
                    >= static_cast<std::uint32_t>(area.respawn_speed)) continue;
        if (!respawn_enemy(area_index, error)) return false;
    }
    error.clear();
    return true;
}

bool MapRenderer::respawn_enemy(std::size_t area_index, std::string &error)
{
    if (area_index >= translated_.spawn_areas.size()
            || area_index >= spawn_candidates_.size()
            || spawn_candidates_[area_index].empty()
            || core_ == NULL) {
        error = "spawn area data is unavailable";
        return false;
    }
    const map::SpawnArea &area = translated_.spawn_areas[area_index];
    if (area.tile_rect.width <= 0 || area.tile_rect.height <= 0
            || area.quantity <= 0) {
        error = "spawn area dimensions are invalid";
        return false;
    }
    map::GridPosition position = {0, 0};
    bool found_position = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        position.x = area.tile_rect.x + static_cast<int>(game_core_random(
            core_, static_cast<std::uint32_t>(area.tile_rect.width)));
        position.y = area.tile_rect.y + static_cast<int>(game_core_random(
            core_, static_cast<std::uint32_t>(area.tile_rect.height)));
        if (position.x == player_.x && position.y == player_.y) continue;
        if (!monster_can_move_to(document_, translated_, position,
                area.ignore_areas)
                || enemy_at(position)) continue;
        found_position = true;
        break;
    }
    if (!found_position) {
        error.clear();
        return true;
    }
    const std::vector<const GameResourceRecordEntry *> &candidates =
        spawn_candidates_[area_index];
    const std::size_t candidate_index = candidates.size() == 1 ? 0
        : static_cast<std::size_t>(game_core_random(core_,
            static_cast<std::uint32_t>(candidates.size())));
    int spawn_index = 0;
    for (std::size_t index = 0; index < area_index; ++index) {
        if (translated_.spawn_areas[index].quantity > INT_MAX - spawn_index) {
            error = "spawn index exceeds the supported range";
            return false;
        }
        spawn_index += translated_.spawn_areas[index].quantity;
    }
    if (core_->enemy_spawn_sequence == UINT32_MAX) {
        error = "enemy respawn sequence is exhausted";
        return false;
    }
    const std::uint32_t sequence = core_->enemy_spawn_sequence + 1;
    EnemyState enemy;
    if (!initialize_enemy(area, spawn_index, position,
            candidates[candidate_index], enemy)) {
        error = std::string("respawned monster record is invalid: ")
            + candidates[candidate_index]->id;
        return false;
    }
    enemy.spawn_area_index = area_index;
    enemy.unique = spawn_area_unique_[area_index];
    std::ostringstream instance_id;
    instance_id << "enc_" << document_.name << "_" << spawn_index
        << "_r" << sequence;
    enemy.instance_id = instance_id.str();
    if (enemy.instance_id.size() >= GAME_CORE_ID_CAPACITY) {
        error = "respawned monster ID exceeds the supported length";
        return false;
    }
    core_->enemy_spawn_sequence = sequence;
    enemies_.push_back(enemy);
    error.clear();
    return true;
}

bool MapRenderer::has_adjacent_aggressive_enemy() const
{
    for (const EnemyState &enemy : enemies_) {
        if (enemy.aggressive && map::is_adjacent(player_, enemy.position)) return true;
    }
    return false;
}

void MapRenderer::select_adjacent_enemy_if_needed()
{
    for (const EnemyState &enemy : enemies_) {
        if (enemy.aggressive && map::is_adjacent(player_, enemy.position)
                && enemy.instance_id == selected_enemy_instance_id_) return;
    }
    selected_enemy_instance_id_.clear();
    for (const EnemyState &enemy : enemies_) {
        if (enemy.aggressive && map::is_adjacent(player_, enemy.position)) {
            selected_enemy_instance_id_ = enemy.instance_id;
            return;
        }
    }
}

bool MapRenderer::combat_action_completed(std::string &message)
{
    if (!combat_active_) return true;
    combat::PlayerCombatStats player_stats;
    std::string stats_error;
    if (!combat::player_combat_stats(resources_, core_, player_stats,
            stats_error)) {
        message += "\nCombat stats could not be updated: " + stats_error;
        combat_active_ = false;
        return false;
    }
    maximum_player_action_points_ = player_stats.maximum_action_points;
    player_attack_cost_ = player_stats.attack_cost;
    player_move_cost_ = player_stats.move_cost;
    player_use_item_cost_ = player_stats.use_item_cost;
    player_reequip_cost_ = player_stats.reequip_cost;
    player_action_points_ = std::max(0, std::min(maximum_player_action_points_,
        player_action_points_));

    if (!has_adjacent_aggressive_enemy()) {
        std::string error;
        player_action_points_ = maximum_player_action_points_;
        if (!combat::finish_combat_round(resources_, core_, player_action_points_,
                maximum_player_action_points_, false, error, false, true,
                false)) {
            message += "\nCombat exit effects failed: " + error;
            combat_active_ = false;
            return false;
        }
        if (core_->health <= 0) {
            handle_player_knockout(message);
            return false;
        }
        if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
            message += "\nCombat stats could not be updated: " + error;
            combat_active_ = false;
            return false;
        }
        if (!apply_enemy_round_on_combat_exit(message, error)) {
            message += "\nCombat exit effects failed: " + error;
            combat_active_ = false;
            return false;
        }
        if (core_->health <= 0) {
            handle_player_knockout(message);
            return false;
        }
        if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
            message += "\nCombat stats could not be updated: " + error;
            combat_active_ = false;
            return false;
        }
        core_->health = std::min(core_->health, player_stats.maximum_health);
        maximum_player_action_points_ = player_stats.maximum_action_points;
        player_action_points_ = maximum_player_action_points_;
        player_attack_cost_ = player_stats.attack_cost;
        player_move_cost_ = player_stats.move_cost;
        player_use_item_cost_ = player_stats.use_item_cost;
        player_reequip_cost_ = player_stats.reequip_cost;
        combat_active_ = false;
        selected_enemy_instance_id_.clear();
        reset_world_round_timers();
        return true;
    }
    if (player_action_points_ >= player_attack_cost_
            || player_action_points_ >= player_move_cost_
            || player_action_points_ >= player_use_item_cost_) {
        select_adjacent_enemy_if_needed();
        return true;
    }
    resolve_monster_turn(message);
    return core_ != NULL && core_->health > 0;
}

void MapRenderer::resolve_monster_turn(std::string &message,
    bool first_turn_of_combat)
{
    if (core_ == NULL || !combat_active_) return;
    player_action_points_ = 0;
    last_player_action_ap_ = 0;
    last_player_action_max_ap_ = maximum_player_action_points_;
    last_player_action_time_ms_ = current_epoch_time_ms();
    for (EnemyState &enemy : enemies_) {
        enemy.action_points = enemy.maximum_action_points;
    }

    if (!first_turn_of_combat) {
        for (std::size_t index = 0; index < enemies_.size();) {
            EnemyState &enemy = enemies_[index];
            if (enemy.conditions.empty()) {
                ++index;
                continue;
            }
            combat::AttackResult result;
            std::string round_error;
            if (!apply_enemy_condition_round(enemy, false, result, round_error)) {
                message += "\n" + enemy.name
                    + " condition effects failed: " + round_error;
                combat_active_ = false;
                return;
            }
            if (!result.enemy_defeated) {
                ++index;
                continue;
            }
            if (!persist_enemy_defeat(enemy, round_error)) {
                message += "\n" + enemy.name
                    + " defeated state could not be saved: " + round_error;
                combat_active_ = false;
                return;
            }
            message += "\n" + enemy.name + " succumbed to its conditions."
                "\nExperience gained: " + std::to_string(result.experience_gained) + ".";
            if (result.health_restored > 0) {
                message += "\nCorpse Eater restored "
                    + std::to_string(result.health_restored) + " HP.";
            }
            enemies_.erase(enemies_.begin() + index);
        }
    }

    active_combat_enemy_id_.clear();
    combat_enemy_actions_ = 0;
    monster_turn_active_ = true;
    next_combat_enemy_action_ms_ = last_round_update_ms_
        + COMBAT_ACTION_INTERVAL_MS;
}

void MapRenderer::advance_monster_turn(std::uint64_t current_time_ms)
{
    if (!monster_turn_active_ || current_time_ms < next_combat_enemy_action_ms_) {
        return;
    }
    if (core_ == NULL || !combat_active_) {
        monster_turn_active_ = false;
        return;
    }
    if (core_->health <= 0) {
        monster_turn_active_ = false;
        handle_player_knockout(interaction_message_);
        return;
    }

    EnemyState *next_enemy = NULL;
    for (EnemyState &enemy : enemies_) {
        if (enemy.instance_id == active_combat_enemy_id_
                && enemy.health > 0 && enemy.aggressive
                && map::is_adjacent(player_, enemy.position)
                && enemy.action_points >= enemy.attack_cost) {
            next_enemy = &enemy;
            break;
        }
    }
    if (next_enemy == NULL) {
        for (EnemyState &enemy : enemies_) {
            if (enemy.health <= 0 || !enemy.aggressive) continue;
            if (map::is_adjacent(player_, enemy.position)
                    && enemy.action_points >= enemy.attack_cost) {
                next_enemy = &enemy;
                break;
            }
            if (enemy_should_move_in_combat(enemy)) {
                next_enemy = &enemy;
                break;
            }
        }
    }

    const std::size_t maximum_actions = std::max<std::size_t>(32,
        enemies_.size() * 32);
    if (next_enemy == NULL || combat_enemy_actions_ >= maximum_actions) {
        monster_turn_active_ = false;
        finish_monster_turn(interaction_message_);
        return;
    }
    active_combat_enemy_id_ = next_enemy->instance_id;
    ++combat_enemy_actions_;
    update_enemy_turn(*next_enemy, interaction_message_);
    if (!combat_active_) {
        monster_turn_active_ = false;
        return;
    }
    std::string persistence_error;
    if (!persist_current_map_state(false, persistence_error)) {
        interaction_message_ += "\nCombat state could not be saved: "
            + persistence_error;
        combat_active_ = false;
        monster_turn_active_ = false;
        return;
    }
    if (core_->health <= 0) {
        monster_turn_active_ = false;
        handle_player_knockout(interaction_message_);
        return;
    }
    next_combat_enemy_action_ms_ = current_time_ms
        + COMBAT_ACTION_INTERVAL_MS;
}

void MapRenderer::finish_monster_turn(std::string &message)
{
    for (std::size_t index = 0; index < enemies_.size();) {
        EnemyState &enemy = enemies_[index];
        if (enemy.health > 0) {
            ++index;
            continue;
        }
        enemy.conditions.clear();
        std::string persistence_error;
        if (!persist_enemy_conditions(enemy, persistence_error)) {
            message += "\n" + enemy.name
                + " condition state could not be cleared: " + persistence_error;
            combat_active_ = false;
            return;
        }
        message += "\n" + enemy.name + " defeated.";
        enemies_.erase(enemies_.begin() + index);
    }

    if (core_->health > 0) {
        combat::PlayerCombatStats player_stats;
        std::string round_error;
        if (!combat::player_combat_stats(resources_, core_, player_stats,
                round_error)) {
            message += "\nCombat stats could not be updated: " + round_error;
            combat_active_ = false;
            return;
        }
        maximum_player_action_points_ = player_stats.maximum_action_points;
        player_action_points_ = maximum_player_action_points_;

        const std::unique_ptr<GameCore> previous_state(
            new (std::nothrow) GameCore);
        if (!previous_state) {
            message += "\nCombat round state could not be allocated.";
            combat_active_ = false;
            return;
        }
        *previous_state = *core_;
        const int previous_action_points = player_action_points_;
        const bool adjacent_enemy = has_adjacent_aggressive_enemy();
        if (!combat::finish_combat_round(resources_, core_, player_action_points_,
                maximum_player_action_points_, adjacent_enemy, round_error,
                false, true, adjacent_enemy)) {
            *core_ = *previous_state;
            player_action_points_ = previous_action_points;
            message += "\nCombat round effects failed: " + round_error;
            combat_active_ = false;
            return;
        }
        if (!combat::player_combat_stats(resources_, core_, player_stats, round_error)) {
            *core_ = *previous_state;
            player_action_points_ = previous_action_points;
            message += "\nCombat stats could not be updated: " + round_error;
            combat_active_ = false;
            return;
        }
        maximum_player_action_points_ = player_stats.maximum_action_points;
        core_->health = std::min(core_->health, player_stats.maximum_health);
        player_attack_cost_ = player_stats.attack_cost;
        player_move_cost_ = player_stats.move_cost;
        player_use_item_cost_ = player_stats.use_item_cost;
        player_reequip_cost_ = player_stats.reequip_cost;
    }

    if (core_->health <= 0) {
        handle_player_knockout(message);
        return;
    }

    if (!has_adjacent_aggressive_enemy()) {
        std::string round_error;
        if (!apply_enemy_round_on_combat_exit(message, round_error)) {
            message += "\nCombat exit effects failed: " + round_error;
            combat_active_ = false;
            return;
        }
        combat::PlayerCombatStats player_stats;
        if (!combat::player_combat_stats(resources_, core_, player_stats,
                round_error)) {
            message += "\nCombat stats could not be updated: " + round_error;
            combat_active_ = false;
            return;
        }
        core_->health = std::min(core_->health, player_stats.maximum_health);
        maximum_player_action_points_ = player_stats.maximum_action_points;
        player_attack_cost_ = player_stats.attack_cost;
        player_move_cost_ = player_stats.move_cost;
        player_use_item_cost_ = player_stats.use_item_cost;
        player_reequip_cost_ = player_stats.reequip_cost;
        if (core_->health <= 0) {
            handle_player_knockout(message);
            return;
        }
        combat_active_ = false;
        player_action_points_ = 0;
        selected_enemy_instance_id_.clear();
        reset_world_round_timers();
    } else {
        player_action_points_ = maximum_player_action_points_;
        select_adjacent_enemy_if_needed();
    }
}

void MapRenderer::update_enemy_turn(EnemyState &enemy, std::string &message)
{
    const bool adjacent = map::is_adjacent(player_, enemy.position);
    if (adjacent) {
        if (enemy.action_points < enemy.attack_cost) {
            enemy.action_points = 0;
            return;
        }
        enemy.action_points -= enemy.attack_cost;
        combat::MonsterAttackResult result;
        std::string error;
        if (!combat::monster_attack_player(resources_, core_, enemy.monster_id,
                result, error, &enemy.conditions, &enemy.health,
                enemy.maximum_health)) {
            message += "\n" + enemy.name + " could not attack: " + error;
            enemy.action_points = 0;
            return;
        }
        if (!persist_enemy_conditions(enemy, error)) {
            message += "\n" + enemy.name
                + " condition state could not be saved: " + error;
            combat_active_ = false;
            return;
        }
        enemy.action_points = std::max(0, std::min(enemy.maximum_action_points,
            enemy.action_points + result.monster_action_points_change));
        player_action_points_ = std::max(0, std::min(maximum_player_action_points_,
            player_action_points_ + result.player_action_points_change));
        std::ostringstream action;
        action << "\n" << enemy.name;
        if (result.hit) {
            action << " hit you for " << result.damage_taken << " damage";
            if (result.critical_hit) action << " (critical)";
            action << ".";
        } else {
            action << " missed.";
            const int taunt_level = game_core_skill_level(core_, "taunt");
            const int taunt_chance = static_cast<int>(std::min<std::int64_t>(
                100, static_cast<std::int64_t>(taunt_level) * 75));
            if (taunt_chance > 0 && game_core_random(core_, 100)
                    < static_cast<std::uint32_t>(taunt_chance)) {
                enemy.action_points = std::max(0, enemy.action_points - 2);
                action << " Your taunt costs it 2 AP.";
            }
        }
        message += action.str();
        return;
    }

    if (!enemy_should_move_in_combat(enemy)) {
        enemy.action_points = 0;
        return;
    }
    enemy.action_points -= enemy.move_cost;
    map::GridPosition next;
    if (!find_enemy_path_step(enemy, player_, next)) return;
    enemy.position = next;
}

bool MapRenderer::enemy_should_move_in_combat(const EnemyState &enemy) const
{
    if (enemy.health <= 0 || !enemy.aggressive
            || enemy.action_points < enemy.move_cost
            || map::is_adjacent(player_, enemy.position)) return false;
    if (enemy.aggression_type == 1) {
        return contains(enemy.spawn_area, player_);
    }
    if (enemy.aggression_type == 2) {
        for (const EnemyState &other : enemies_) {
            if (&other != &enemy && other.spawn_area_index == enemy.spawn_area_index
                    && map::is_adjacent(player_, other.position)) return true;
        }
        return false;
    }
    return enemy.aggression_type == 3;
}

bool MapRenderer::end_combat_turn()
{
    if (!combat_active_ || modal_interaction_ || monster_turn_active_) return false;
    interaction_message_ = "You ended your turn.";
    resolve_monster_turn(interaction_message_);
    return true;
}

bool MapRenderer::use_or_equip_item(const std::string &item_id, std::string &error,
    int target_equipment_slot)
{
    error.clear();
    if (core_ == NULL || resources_ == NULL || item_id.empty()
            || monster_turn_active_) {
        error = "invalid inventory action";
        return false;
    }
    items::ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    if (!items::item_action(resources_, item_id, action, slot, two_handed, error)) {
        return false;
    }
    if (action == items::ACTION_NOT_USABLE) {
        error = "This item cannot be used or equipped.";
        return false;
    }

    combat::PlayerCombatStats player_stats;
    if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
        return false;
    }
    const int action_cost = action == items::ACTION_USE
        ? player_stats.use_item_cost : player_stats.reequip_cost;
    if (combat_active_ && player_action_points_ < action_cost) {
        error = "Not enough AP for that inventory action.";
        return false;
    }
    const int previous_action_points = player_action_points_;
    if (combat_active_) player_action_points_ -= action_cost;

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "Inventory action state could not be allocated.";
        return false;
    }
    *previous_state = *core_;
    int health_change = 0;
    bool success = false;
    if (action == items::ACTION_USE) {
        success = combat::use_item(resources_, core_, item_id, player_action_points_,
            combat_active_ ? maximum_player_action_points_ : 0,
            health_change, error);
    } else if (target_equipment_slot >= 0
            && target_equipment_slot < GAME_CORE_EQUIPMENT_SLOTS) {
        success = items::equip_item(resources_, core_, item_id,
            static_cast<GameCoreEquipmentSlot>(target_equipment_slot), error);
    } else {
        success = items::equip_item(resources_, core_, item_id, error);
    }
    if (!success) {
        player_action_points_ = previous_action_points;
        return false;
    }

    if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
        *core_ = *previous_state;
        player_action_points_ = previous_action_points;
        return false;
    }
    maximum_player_action_points_ = player_stats.maximum_action_points;
    player_attack_cost_ = player_stats.attack_cost;
    player_move_cost_ = player_stats.move_cost;
    player_use_item_cost_ = player_stats.use_item_cost;
    player_reequip_cost_ = player_stats.reequip_cost;
    player_action_points_ = std::min(player_action_points_,
        maximum_player_action_points_);

    if (action == items::ACTION_USE) {
        interaction_message_ = "Used " + item_id;
        if (health_change > 0) {
            interaction_message_ += "; restored "
                + std::to_string(health_change) + " HP";
        }
        interaction_message_ += ".";
    } else {
        interaction_message_ = "Equipped " + item_id + ".";
    }
    if (combat_active_ && !combat_action_completed(interaction_message_)
            && core_->health <= 0) {
        return true;
    }
    return true;
}

bool MapRenderer::unequip_equipment(GameCoreEquipmentSlot slot, std::string &error)
{
    error.clear();
    if (core_ == NULL || resources_ == NULL || monster_turn_active_) {
        error = "invalid equipment action";
        return false;
    }
    if (game_core_equipped_item(core_, slot) == NULL) {
        error = "That equipment slot is empty.";
        return false;
    }
    combat::PlayerCombatStats player_stats;
    if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
        return false;
    }
    if (combat_active_ && player_action_points_ < player_stats.reequip_cost) {
        error = "Not enough AP to unequip equipment.";
        return false;
    }
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "Equipment action state could not be allocated.";
        return false;
    }
    *previous_state = *core_;
    const int previous_action_points = player_action_points_;
    if (combat_active_) player_action_points_ -= player_stats.reequip_cost;
    if (!items::unequip_item(resources_, core_, slot, error)) {
        player_action_points_ = previous_action_points;
        return false;
    }
    if (!combat::player_combat_stats(resources_, core_, player_stats, error)) {
        *core_ = *previous_state;
        player_action_points_ = previous_action_points;
        return false;
    }
    maximum_player_action_points_ = player_stats.maximum_action_points;
    player_attack_cost_ = player_stats.attack_cost;
    player_move_cost_ = player_stats.move_cost;
    player_use_item_cost_ = player_stats.use_item_cost;
    player_reequip_cost_ = player_stats.reequip_cost;
    player_action_points_ = std::min(player_action_points_,
        maximum_player_action_points_);
    interaction_message_ = "Unequipped equipment and returned it to inventory.";
    if (combat_active_) combat_action_completed(interaction_message_);
    return true;
}

bool MapRenderer::is_in_combat() const
{
    return combat_active_;
}

int MapRenderer::player_action_points() const
{
    return player_action_points_;
}

int MapRenderer::displayed_player_action_points() const
{
    const std::uint64_t now_ms = current_epoch_time_ms();
    if (combat_active_ && last_player_action_time_ms_ != 0
            && now_ms >= last_player_action_time_ms_
            && now_ms - last_player_action_time_ms_ < 800) {
        return std::min(last_player_action_ap_, last_player_action_max_ap_);
    }
    return player_action_points_;
}

int MapRenderer::maximum_player_action_points() const
{
    return maximum_player_action_points_;
}

bool MapRenderer::load_tileset_sheets(
    const char *tileset_list_path,
    const char *texture_directory,
    std::string &error
)
{
    FILE *list = std::fopen(tileset_list_path, "rb");
    if (list == NULL) {
        error = std::string("cannot open original tileset list: ") + tileset_list_path;
        return false;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), list) != NULL) {
        std::size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0) continue;
        if (std::strchr(line, '/') != NULL || std::strchr(line, '\\') != NULL) {
            std::fclose(list);
            error = "invalid filename in original tileset list";
            return false;
        }

        const std::string image_name(line);
        const std::size_t dot = image_name.find_last_of('.');
        const std::string texture_name = dot == std::string::npos
            ? image_name + ".t3x" : image_name.substr(0, dot) + ".t3x";
        const std::string texture_path = std::string(texture_directory) + "/" + texture_name;
        C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(texture_path.c_str());
        if (sheet == NULL || C2D_SpriteSheetCount(sheet) != 1) {
            if (sheet != NULL) C2D_SpriteSheetFree(sheet);
            std::fclose(list);
            error = "cannot load original map texture sheet: " + texture_path;
            return false;
        }

        const C2D_Image whole = C2D_SpriteSheetGetImage(sheet, 0);
        if (whole.tex == NULL || whole.subtex == NULL) {
            C2D_SpriteSheetFree(sheet);
            std::fclose(list);
            error = "original texture sheet contains no image: " + texture_path;
            return false;
        }
        C3D_TexSetFilter(whole.tex, GPU_NEAREST, GPU_NEAREST);

        const std::size_t index = sheets_.size();
        sheets_.push_back(sheet);
        image_names_.push_back(image_name);
        image_indices_[image_name] = index;
    }

    const bool failed = std::ferror(list) != 0;
    std::fclose(list);
    if (failed || sheets_.empty()) {
        error = "failed while reading original tileset list";
        return false;
    }
    return true;
}

bool MapRenderer::prepare_tile_sources(const map::Document &document, std::string &error)
{
    for (const map::Tileset &tileset : document.tilesets) {
        const std::string image_name = basename_of(tileset.image_source);
        const std::map<std::string, std::size_t>::const_iterator image_index =
            image_indices_.find(image_name);
        if (image_index == image_indices_.end()) continue;
        if (tileset.columns <= 0 || tileset.tile_count == 0 || tileset.image_width <= 0
                || tileset.image_height <= 0 || tileset.image_width % tileset.columns != 0
                || tileset.image_height % static_cast<int>(
                    (tileset.tile_count + tileset.columns - 1) / tileset.columns) != 0) {
            error = "invalid original tileset grid: " + tileset.name;
            return false;
        }

        const C2D_Image whole = C2D_SpriteSheetGetImage(sheets_[image_index->second], 0);
        if (whole.subtex == NULL || whole.subtex->width < tileset.image_width
                || whole.subtex->height < tileset.image_height) {
            error = "original tileset image is smaller than TMX metadata: " + tileset.name;
            return false;
        }

        std::vector<Tex3DS_SubTexture> &subtextures = tile_subtextures_[image_name];
        subtextures.resize(tileset.tile_count);
        const float u_range = whole.subtex->right - whole.subtex->left;
        const float v_range = whole.subtex->bottom - whole.subtex->top;
        for (std::uint32_t local_id = 0; local_id < tileset.tile_count; ++local_id) {
            const int tile_x = static_cast<int>(local_id % static_cast<std::uint32_t>(tileset.columns));
            const int tile_y = static_cast<int>(local_id / static_cast<std::uint32_t>(tileset.columns));
            Tex3DS_SubTexture &subtexture = subtextures[local_id];
            subtexture.width = static_cast<u16>(tileset.declared_tile_width);
            subtexture.height = static_cast<u16>(tileset.declared_tile_height);
            subtexture.left = whole.subtex->left
                + (tile_x * tileset.declared_tile_width / static_cast<float>(whole.subtex->width)) * u_range;
            subtexture.right = whole.subtex->left
                + ((tile_x + 1) * tileset.declared_tile_width / static_cast<float>(whole.subtex->width)) * u_range;
            subtexture.top = whole.subtex->top
                + (tile_y * tileset.declared_tile_height / static_cast<float>(whole.subtex->height)) * v_range;
            subtexture.bottom = whole.subtex->top
                + ((tile_y + 1) * tileset.declared_tile_height / static_cast<float>(whole.subtex->height)) * v_range;
        }
    }
    return true;
}

bool MapRenderer::validate_visible_layers(const map::Document &document, std::string &error) const
{
    static const char *layer_names[] = {"base", "ground", "objects", "above", "top"};
    for (const char *name : layer_names) {
        const map::Layer *layer = map::find_layer(document, name);
        if (layer == NULL) continue;
        for (std::uint32_t gid : layer->gids) {
            if (gid == 0) continue;
            map::TileSource source;
            if (!map::resolve_tile_source(document, gid, source)) {
                error = std::string("cannot resolve original GID in ") + name;
                return false;
            }
            const std::string image_name = basename_of(document.tilesets[source.tileset_index].image_source);
            const std::map<std::string, std::vector<Tex3DS_SubTexture> >::const_iterator tiles =
                tile_subtextures_.find(image_name);
            if (tiles == tile_subtextures_.end() || source.local_id >= tiles->second.size()) {
                error = "original tile image is not bundled: " + image_name;
                return false;
            }
        }
    }
    return true;
}

bool MapRenderer::transition(const map::Event &event, map::GridPosition entered,
    std::string &error)
{
    if (!valid_map_id(event.target_map)) {
        error = "invalid destination map ID in source map data";
        return false;
    }
    const std::string path = maps_directory_ + "/" + event.target_map + ".tmx";
    map::Document destination_document;
    if (!map::load_tmx(path, event.target_map, true, destination_document, &error)) return false;
    map::TranslatedDocument destination_translated;
    if (!map::translate_document(destination_document, destination_translated, &error)
            || !prepare_tile_sources(destination_document, error)
            || !validate_visible_layers(destination_document, error)) return false;

    map::MapTransition resolved;
    if (!map::resolve_map_transition(translated_, destination_translated,
            event.name, entered, resolved)) {
        error = "source destination place not found: " + event.target_map + ":" + event.target_place;
        return false;
    }
    if (!map::is_walkable(destination_document, resolved.destination.x, resolved.destination.y)) {
        bool found_fallback = false;
        for (const map::Event &destination_event : destination_translated.events) {
            if (destination_event.kind != map::EventKind::MapChange || !destination_event.active) continue;
            if (map::is_walkable(destination_document, destination_event.tile_rect.x,
                    destination_event.tile_rect.y)) {
                resolved.destination = {destination_event.tile_rect.x, destination_event.tile_rect.y};
                found_fallback = true;
                break;
            }
        }
        if (!found_fallback) {
            error = "Android destination and mapchange fallback are not walkable";
            return false;
        }
    }
    if (!persist_current_map_state(true, error)) return false;
    document_ = std::move(destination_document);
    translated_ = std::move(destination_translated);
    player_ = resolved.destination;
    if (!initialize_enemies(error)) return false;
    interaction_message_.clear();
    modal_interaction_ = false;
    error.clear();
    return true;
}

bool MapRenderer::handle_map_event(const map::Event &event, map::GridPosition entered,
    std::string &error)
{
    switch (event.kind) {
    case map::EventKind::MapChange:
        return transition(event, entered, error);
    case map::EventKind::Sign:
        show_sign(event);
        return true;
    case map::EventKind::Rest:
        game_core_restore_player(core_);
        if (!reset_encounters_after_time_passed(error)) {
            interaction_message_ = "Rest failed: " + error;
            modal_interaction_ = true;
            return true;
        }
        reset_world_round_timers();
        interaction_message_ = "Rested at " + document_.name + ":" + event.name
            + ". Health restored; encounters reset.";
        modal_interaction_ = true;
        return true;
    case map::EventKind::Container:
        show_container(event);
        return true;
    case map::EventKind::Key:
    case map::EventKind::Script:
        return false;
    }
    return false;
}

void MapRenderer::show_sign(const map::Event &event)
{
    std::string message;
    if (!phrase_message(resources_, event.phrase_id, message)) {
        interaction_message_ = "Sign phrase data unavailable: " + event.phrase_id;
    } else {
        interaction_message_ = message;
    }
    modal_interaction_ = true;
}

void MapRenderer::show_container(const map::Event &event)
{
    if (resources_ == NULL || event.drop_list_id.empty()) {
        interaction_message_ = "Container " + event.name
            + ": original drop-list reference is unavailable.";
        modal_interaction_ = true;
        return;
    }
    const std::string opened_flag = "container_" + event.name;
    if (game_core_quest_progress(core_, opened_flag.c_str()) > 0) {
        interaction_message_ = "Container " + event.name + " has already been opened.";
        modal_interaction_ = true;
        return;
    }
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        interaction_message_ = "Container state could not be allocated.";
        modal_interaction_ = true;
        return;
    }
    *previous_state = *core_;
    if (!items::apply_drop_list(resources_, core_, event.drop_list_id, interaction_message_)
            || !game_core_set_quest_progress(core_, opened_flag.c_str(), 1)) {
        *core_ = *previous_state;
        if (interaction_message_.empty()) {
            interaction_message_ = "Container could not be opened: " + event.name;
        }
        modal_interaction_ = true;
        return;
    }
    std::ostringstream message;
    message << "Opened " << event.name << ". Supplies added to your inventory.";
    interaction_message_ = message.str();
    modal_interaction_ = true;
}

void MapRenderer::show_key_requirement(const map::Event &event)
{
    interaction_message_.clear();
    if (!event.phrase_id.empty()) phrase_message(resources_, event.phrase_id, interaction_message_);
    if (interaction_message_.empty()) {
        interaction_message_ = "You cannot go that way yet.";
    }
    modal_interaction_ = true;
}

static bool key_requirement_met(const map::Requirement &requirement, const GameCore *core)
{
    if (!requirement.valid || !requirement.recognized_type || core == NULL) return false;
    bool met = false;
    if (requirement.type == "questProgress" || requirement.type == "questLatestProgress") {
        met = game_core_quest_progress(core, requirement.id.c_str()) == requirement.value;
    } else {
        return false;
    }
    return requirement.negate ? !met : met;
}

bool MapRenderer::move_player(int dx, int dy)
{
    if (!initialized_ || modal_interaction_ || monster_turn_active_) return false;
    const map::GridPosition previous = player_;
    map::GridPosition next = player_;
    if (!map::move_player(document_, next, dx, dy)) return false;

    map::GridPosition actor_position;
    if (document_.name == "home"
            && spawn_position(document_, translated_, "mikhail", 0, actor_position)
            && next.x == actor_position.x && next.y == actor_position.y) {
        return false;
    }
    if (enemy_at(next)) return false;

    const bool starting_combat = !combat_active_ && has_adjacent_aggressive_enemy();
    if (starting_combat) {
        combat_active_ = true;
        player_action_points_ = maximum_player_action_points_;
        select_adjacent_enemy_if_needed();
    }
    if (combat_active_) {
        if (player_action_points_ < player_move_cost_) {
            interaction_message_ = "Not enough AP to move.";
            return false;
        }
        const int flee_chance = std::max(0,
            20 - game_core_skill_level(core_, "evasion") * 5);
        if (game_core_random(core_, 100) < static_cast<std::uint32_t>(flee_chance)) {
            interaction_message_ = "You failed to flee.";
            resolve_monster_turn(interaction_message_);
            modal_interaction_ = core_->health <= 0;
            return false;
        }
        player_action_points_ -= player_move_cost_;
    }

    for (const map::Event &event : translated_.events) {
        if (event.kind != map::EventKind::Key || !event.active || !contains(event.tile_rect, next)
                || !event.has_requirement) continue;
        if (!key_requirement_met(event.requirement, core_)) {
            show_key_requirement(event);
            return false;
        }
    }

    player_ = next;
    std::string error;
    for (const map::Event &event : translated_.events) {
        if (combat_active_ && event.kind != map::EventKind::Script) continue;
        if (!event.active || !contains(event.tile_rect, player_)
                || contains(event.tile_rect, previous)) continue;
        if (handle_map_event(event, player_, error)) return true;
        if (!error.empty()) {
            interaction_message_ = "Map event deferred: " + error;
            modal_interaction_ = true;
            return false;
        }
    }
    interaction_message_.clear();
    if (combat_active_) {
        std::string action_message;
        combat_action_completed(action_message);
        if (!action_message.empty()) interaction_message_ = action_message;
        modal_interaction_ = core_->health <= 0;
    } else if (has_adjacent_aggressive_enemy()) {
        combat_active_ = true;
        player_action_points_ = maximum_player_action_points_;
        select_adjacent_enemy_if_needed();
    }
    return true;
}

bool MapRenderer::interact()
{
    if (!initialized_ || modal_interaction_ || monster_turn_active_) return false;
    for (const map::Event &event : translated_.events) {
        if (event.kind != map::EventKind::Container || !event.active) continue;
        const map::GridPosition target = {event.tile_rect.x, event.tile_rect.y};
        if (!contains(event.tile_rect, player_)
                && distance_to_tile(player_, target) > 1) continue;
        shop_interaction_ = false;
        show_container(event);
        return true;
    }
    if (can_shop_with_mara()) {
        int bread_price = 0;
        std::string error;
        if (!items::item_price(resources_, "bread", bread_price, error)) {
            interaction_message_ = "Mara's shop: " + error;
            shop_interaction_ = true;
            modal_interaction_ = true;
            return false;
        }
        std::ostringstream message;
        message << "Mara's shop\nBread: " << bread_price
            << " gold\nYour gold: " << game_core_inventory_count(core_, "gold");
        interaction_message_ = message.str();
        shop_interaction_ = true;
        modal_interaction_ = true;
        return true;
    }
    if (can_talk_to_mikhail()) {
        std::string error;
        if (!conversation_.start("mikhail_start_select", error)) {
            interaction_message_ = "Mikhail: " + error;
            modal_interaction_ = true;
            return false;
        }
        selected_conversation_reply_ = 0;
        interaction_message_ = "Mikhail\n" + conversation_.message();
        shop_interaction_ = false;
        modal_interaction_ = true;
        return true;
    }
    interaction_message_.clear();
    shop_interaction_ = false;
    modal_interaction_ = false;
    return false;
}

bool MapRenderer::can_talk_to_mikhail() const
{
    if (!initialized_ || modal_interaction_ || document_.name != "home") return false;
    map::GridPosition mikhail;
    return spawn_position(document_, translated_, "mikhail", 0, mikhail)
        && distance_to_tile(player_, mikhail) <= 2;
}

bool MapRenderer::can_shop_with_mara() const
{
    if (!initialized_ || modal_interaction_ || document_.name != "crossglen_hall") return false;
    map::GridPosition mara;
    return spawn_position(document_, translated_, "Mara", 0, mara)
        && distance_to_tile(player_, mara) <= 1;
}

const char *MapRenderer::nearby_interaction_hint() const
{
    if (!initialized_ || modal_interaction_) return "";
    if (combat_active_) {
        return has_adjacent_aggressive_enemy()
            ? "COMBAT - A: ATTACK  B: END TURN"
            : "COMBAT - A: END COMBAT  B: END TURN";
    }
    if (can_talk_to_mikhail()) return "MIKHAIL NEARBY - A: TALK";
    if (can_shop_with_mara()) return "MARA'S SHOP - A: BUY";
    if (has_adjacent_aggressive_enemy()) return "ENEMY NEARBY - A: ATTACK";
    return "";
}

bool MapRenderer::can_attack_with_action_button() const
{
    return initialized_ && !modal_interaction_ && !monster_turn_active_
        && (combat_active_ || (has_adjacent_aggressive_enemy()
            && !can_talk_to_mikhail() && !can_shop_with_mara()));
}

bool MapRenderer::cycle_combat_target(int direction)
{
    if (!initialized_ || modal_interaction_ || monster_turn_active_
            || !combat_active_ || direction == 0) {
        return false;
    }

    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        if (enemies_[index].aggressive
                && map::is_adjacent(player_, enemies_[index].position)) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        selected_enemy_instance_id_.clear();
        interaction_message_ = "No adjacent monster to target.";
        return false;
    }

    std::size_t selected = direction > 0 ? 0 : candidates.size() - 1;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (enemies_[candidates[index]].instance_id
                != selected_enemy_instance_id_) continue;
        selected = direction > 0
            ? (index + 1) % candidates.size()
            : (index + candidates.size() - 1) % candidates.size();
        break;
    }
    EnemyState &target = enemies_[candidates[selected]];
    selected_enemy_instance_id_ = target.instance_id;
    std::ostringstream message;
    message << "Target: " << target.name << " (" << target.health << "/"
        << target.maximum_health << " HP).";
    interaction_message_ = message.str();
    return true;
}

bool MapRenderer::selected_combat_target(std::string &name, int &health,
    int &maximum_health) const
{
    name.clear();
    health = 0;
    maximum_health = 0;
    if (!initialized_ || !combat_active_) return false;

    const EnemyState *first_adjacent = NULL;
    for (const EnemyState &enemy : enemies_) {
        if (!enemy.aggressive || !map::is_adjacent(player_, enemy.position)) continue;
        if (first_adjacent == NULL) first_adjacent = &enemy;
        if (combat_active_ && enemy.instance_id == selected_enemy_instance_id_) {
            first_adjacent = &enemy;
            break;
        }
    }
    if (first_adjacent == NULL) return false;
    name = first_adjacent->name;
    health = first_adjacent->health;
    maximum_health = first_adjacent->maximum_health;
    return true;
}

bool MapRenderer::attack_nearby_enemy()
{
    if (!initialized_ || modal_interaction_ || monster_turn_active_) return false;

    std::size_t target_index = 0;
    for (; target_index < enemies_.size(); ++target_index) {
        if (enemies_[target_index].aggressive
                && enemies_[target_index].instance_id == selected_enemy_instance_id_
                && map::is_adjacent(player_, enemies_[target_index].position)) break;
    }
    if (target_index == enemies_.size()) {
        for (target_index = 0; target_index < enemies_.size(); ++target_index) {
            if (enemies_[target_index].aggressive
                    && map::is_adjacent(player_, enemies_[target_index].position)) {
                selected_enemy_instance_id_ = enemies_[target_index].instance_id;
                break;
            }
        }
    }
    if (target_index == enemies_.size()) {
        if (combat_active_ && !has_adjacent_aggressive_enemy()) {
            interaction_message_ = "Combat ended.";
            combat_action_completed(interaction_message_);
            return true;
        }
        interaction_message_ = "No enemy is close enough to attack.";
        modal_interaction_ = true;
        return false;
    }

    if (!combat_active_) {
        combat_active_ = true;
        player_action_points_ = maximum_player_action_points_;
    }
    if (player_action_points_ < player_attack_cost_) {
        interaction_message_ = "Not enough AP to attack.";
        return false;
    }

    std::string error;
    combat::AttackResult result;
    EnemyState &target = enemies_[target_index];
    const std::string target_name = target.name;
    if (!combat::attack_monster_once(resources_, core_, target.monster_id,
            target.drop_list_id, target.instance_id, core_->attack_damage,
            result, error, &target.conditions)) {
        interaction_message_ = "Combat failed: " + error;
        modal_interaction_ = true;
        return false;
    }
    player_action_points_ -= player_attack_cost_;
    player_action_points_ = std::max(0, std::min(maximum_player_action_points_,
        player_action_points_ + result.player_action_points_change));
    target.action_points = std::max(0, std::min(target.maximum_action_points,
        target.action_points + result.enemy_action_points_change));
    target.health = result.enemy_health;
    if (result.enemy_defeated) target.conditions.clear();
    if (result.enemy_defeated && !persist_enemy_defeat(target, error)) {
        interaction_message_ = "Defeated monster state could not be saved: " + error;
        combat_active_ = false;
        modal_interaction_ = true;
        return true;
    }
    if (!persist_enemy_conditions(target, error)) {
        interaction_message_ = "Combat condition state could not be saved: " + error;
        combat_active_ = false;
        return true;
    }
    if (result.enemy_defeated) {
        const std::string defeated_instance_id = target.instance_id;
        enemies_.erase(enemies_.begin() + target_index);
        if (selected_enemy_instance_id_ == defeated_instance_id) {
            selected_enemy_instance_id_.clear();
            for (const EnemyState &enemy : enemies_) {
                if (enemy.aggressive
                        && map::is_adjacent(player_, enemy.position)) {
                    selected_enemy_instance_id_ = enemy.instance_id;
                    break;
                }
            }
        }
        player_action_points_ = std::min(maximum_player_action_points_,
            player_action_points_ + game_core_skill_level(core_, "cleave") * 3);
    }
    std::ostringstream message;
    message << "AP: " << player_action_points_ << "/" << maximum_player_action_points_
        << ". ";
    if (result.enemy_defeated) {
        message << target_name << " defeated."
            << "\nExperience gained: " << result.experience_gained << ".";
        if (result.health_restored > 0) {
            message << "\nCorpse Eater restored " << result.health_restored << " HP.";
        }
    } else if (result.player_hit) {
        message << "You hit " << target_name << " for " << result.damage_dealt << " damage ("
            << result.enemy_health << " HP left).";
        if (result.player_critical_hit) message << " Critical hit!";
    } else {
        message << "Your attack missed " << target_name << ".";
    }
    interaction_message_ = message.str();
    shop_interaction_ = false;
    std::string monster_actions;
    combat_action_completed(monster_actions);
    if (!monster_actions.empty()) interaction_message_ += monster_actions;
    modal_interaction_ = core_->health <= 0;
    return true;
}

bool MapRenderer::has_modal_interaction() const
{
    return modal_interaction_;
}

void MapRenderer::dismiss_modal_interaction()
{
    conversation_.close();
    modal_interaction_ = false;
    interaction_message_.clear();
    selected_conversation_reply_ = 0;
    shop_interaction_ = false;
}

bool MapRenderer::has_conversation() const
{
    return conversation_.active();
}

bool MapRenderer::has_shop() const
{
    return shop_interaction_;
}

bool MapRenderer::buy_shop_bread(std::string &error)
{
    if (!shop_interaction_) {
        error = "shop is not open";
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    int bread_price = 0;
    if (!items::item_price(resources_, "bread", bread_price, error)) {
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    if (!items::buy_item(resources_, core_, "bread", 1, error)) {
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    std::ostringstream message;
    message << "Mara's shop\nBread bought for " << bread_price
        << " gold.\nYour gold: " << game_core_inventory_count(core_, "gold");
    interaction_message_ = message.str();
    error.clear();
    return true;
}

const std::vector<dialogue::Reply> &MapRenderer::conversation_replies() const
{
    return conversation_.replies();
}

std::size_t MapRenderer::selected_conversation_reply() const
{
    return selected_conversation_reply_;
}

void MapRenderer::move_conversation_selection(int direction)
{
    const std::size_t count = conversation_.replies().size();
    if (!conversation_.active() || count == 0 || direction == 0) return;
    if (direction < 0) {
        selected_conversation_reply_ = selected_conversation_reply_ == 0
            ? count - 1 : selected_conversation_reply_ - 1;
    } else {
        selected_conversation_reply_ = (selected_conversation_reply_ + 1) % count;
    }
}

bool MapRenderer::choose_conversation_reply(std::string &error)
{
    if (!conversation_.active()) {
        error = "no conversation is active";
        interaction_message_ = "Mikhail: " + error;
        return false;
    }
    if (!conversation_.choose(selected_conversation_reply_, error)) {
        interaction_message_ = "Mikhail: " + error;
        return false;
    }
    selected_conversation_reply_ = 0;
    interaction_message_ = "Mikhail\n" + conversation_.message();
    error.clear();
    return true;
}

const std::string &MapRenderer::interaction_message() const
{
    return interaction_message_;
}

void MapRenderer::draw_layer(
    const std::string &name,
    const map::ViewTransform &view,
    float depth
) const
{
    const map::Layer *layer = map::find_layer(document_, name);
    if (layer == NULL) return;
    for (int y = view.camera_y; y < view.camera_y + view.visible_rows; ++y) {
        for (int x = view.camera_x; x < view.camera_x + view.visible_columns; ++x) {
            const std::uint32_t gid = layer->gid_at(x, y, document_.height);
            if (gid == 0) continue;
            map::GridPosition screen;
            if (!map::map_to_screen(view, {x, y}, screen)) continue;
            draw_gid(gid, screen.x, screen.y, depth);
        }
    }
}

void MapRenderer::draw_gid(std::uint32_t gid, int screen_x, int screen_y, float depth) const
{
    map::TileSource source;
    if (!map::resolve_tile_source(document_, gid, source)) return;
    const map::Tileset &tileset = document_.tilesets[source.tileset_index];
    const std::string image_name = basename_of(tileset.image_source);
    const std::map<std::string, std::size_t>::const_iterator sheet = image_indices_.find(image_name);
    const std::map<std::string, std::vector<Tex3DS_SubTexture> >::const_iterator tiles =
        tile_subtextures_.find(image_name);
    if (sheet == image_indices_.end() || tiles == tile_subtextures_.end()
            || source.local_id >= tiles->second.size()) return;
    C2D_Image image = C2D_SpriteSheetGetImage(sheets_[sheet->second], 0);
    image.subtex = &tiles->second[source.local_id];
    C2D_DrawImageAt(image, static_cast<float>(screen_x), static_cast<float>(screen_y), depth, NULL);
}

void MapRenderer::draw_actor_icon(
    const char *sheet_name,
    std::uint32_t icon_index,
    map::GridPosition position,
    const map::ViewTransform &view,
    float depth
) const
{
    map::GridPosition screen;
    if (!map::map_to_screen(view, position, screen)) return;
    draw_actor_icon_at(sheet_name, icon_index,
        static_cast<float>(screen.x), static_cast<float>(screen.y), depth);
}

void MapRenderer::draw_actor_icon_at(const char *sheet_name,
    std::uint32_t icon_index, float x, float y, float depth) const
{
    const std::map<std::string, std::size_t>::const_iterator sheet =
        image_indices_.find(sheet_name);
    if (sheet == image_indices_.end()) return;
    const C2D_Image whole = C2D_SpriteSheetGetImage(sheets_[sheet->second], 0);
    if (whole.subtex == NULL || whole.subtex->width % 32 != 0
            || whole.subtex->height % 32 != 0) return;
    const std::uint32_t columns = whole.subtex->width / 32;
    const std::uint32_t rows = whole.subtex->height / 32;
    if (columns == 0 || icon_index >= columns * rows) return;

    const std::uint32_t tile_x = icon_index % columns;
    const std::uint32_t tile_y = icon_index / columns;
    const float u_range = whole.subtex->right - whole.subtex->left;
    const float v_range = whole.subtex->bottom - whole.subtex->top;
    Tex3DS_SubTexture subtexture;
    subtexture.width = 32;
    subtexture.height = 32;
    subtexture.left = whole.subtex->left + (tile_x * 32.0f / whole.subtex->width) * u_range;
    subtexture.right = whole.subtex->left + ((tile_x + 1) * 32.0f / whole.subtex->width) * u_range;
    subtexture.top = whole.subtex->top + (tile_y * 32.0f / whole.subtex->height) * v_range;
    subtexture.bottom = whole.subtex->top + ((tile_y + 1) * 32.0f / whole.subtex->height) * v_range;

    C2D_Image image = whole;
    image.subtex = &subtexture;
    C2D_DrawImageAt(image, x, y, depth, NULL);
}

void MapRenderer::draw_selected_combat_target_icon(float x, float y,
    float depth) const
{
    if (!combat_active_) return;
    const EnemyState *first_adjacent = NULL;
    for (const EnemyState &enemy : enemies_) {
        if (!enemy.aggressive || !map::is_adjacent(player_, enemy.position)) continue;
        if (first_adjacent == NULL) first_adjacent = &enemy;
        if (enemy.instance_id == selected_enemy_instance_id_) {
            first_adjacent = &enemy;
            break;
        }
    }
    if (first_adjacent == NULL) return;
    draw_actor_icon_at(first_adjacent->icon_sheet.c_str(),
        first_adjacent->icon_index, x, y, depth);
}

void MapRenderer::draw(int screen_width, int screen_height) const
{
    if (!initialized_) return;
    const map::ViewTransform view = map::camera_for_player(
        document_, player_, screen_width, screen_height
    );
    draw_layer("base", view, 0.5f);
    draw_layer("ground", view, 0.5f);
    draw_layer("objects", view, 0.5f);
    if (document_.name == "home") {
        map::GridPosition mikhail;
        if (spawn_position(document_, translated_, "mikhail", 0, mikhail)) {
            draw_actor_icon("monsters_mage2.png", 0, mikhail, view, 0.45f);
        }
    } else if (document_.name == "crossglen_hall") {
        map::GridPosition mara;
        if (spawn_position(document_, translated_, "Mara", 0, mara)) {
            draw_actor_icon("monsters_men.png", 7, mara, view, 0.45f);
        }
    }
    for (const EnemyState &enemy : enemies_) {
        draw_actor_icon(enemy.icon_sheet.c_str(), enemy.icon_index,
            enemy.position, view, 0.45f);
    }
    map::GridPosition player_screen;
    if (map::map_to_screen(view, player_, player_screen)) {
        C2D_DrawImageAt(player_image_, static_cast<float>(player_screen.x),
            static_cast<float>(player_screen.y), 0.5f, NULL);
    }

    draw_layer("above", view, 0.5f);
    draw_layer("top", view, 0.5f);
}

void MapRenderer::draw_player_portrait(float x, float y, float depth) const
{
    if (player_image_.tex != NULL && player_image_.subtex != NULL) {
        C2D_DrawImageAt(player_image_, x, y, depth, NULL);
    }
}

map::GridPosition MapRenderer::player_position() const
{
    return player_;
}

map::ViewTransform MapRenderer::camera(int screen_width, int screen_height) const
{
    return map::camera_for_player(document_, player_, screen_width, screen_height);
}

bool MapRenderer::player_walkable() const
{
    return map::is_walkable(document_, player_.x, player_.y);
}

const char *MapRenderer::map_id() const
{
    return document_.name.c_str();
}

}
}