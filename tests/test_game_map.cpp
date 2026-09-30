#include "game_map.hpp"

#include <cassert>
#include <iostream>
#include <string>

using andortrail::map::Document;
using andortrail::map::Layer;
using andortrail::map::Catalog;
using andortrail::map::EventKind;
using andortrail::map::GridPosition;
using andortrail::map::ScriptWhen;
using andortrail::map::TranslatedDocument;
using andortrail::map::ViewTransform;
using andortrail::map::load_tmx;
using andortrail::map::load_catalog;
using andortrail::map::translate_document;

int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string directory = argv[1];
    std::string error;

    Document home;
    assert(load_tmx(directory + "/home.tmx", "home", true, home, &error));
    assert(home.name == "home");
    assert(home.width == 12 && home.height == 10);
    assert(home.tile_width == 32 && home.tile_height == 32);
    assert(!home.tilesets.empty());
    assert(home.tilesets[0].image_source == "../drawable/map_bed_1.png");
    assert(home.tilesets[0].tile_count == 128 && home.tilesets[0].columns == 16);
    assert(!home.layers.empty());
    assert(!home.object_groups.empty());
    TranslatedDocument translated_home;
    assert(translate_document(home, translated_home, &error));
    bool found_android_start = false;
    for (const auto &event : translated_home.events) {
        if (event.kind == EventKind::Rest && event.name == "rest") {
            found_android_start = event.tile_rect.x == 1 && event.tile_rect.y == 4;
        }
    }
    assert(found_android_start);
    const Layer *home_walkable = andortrail::map::find_layer(home, "walkable");
    assert(home_walkable != NULL);
    assert(home_walkable->gids.size() == 12u * 10u);
    const Layer *home_ground = andortrail::map::find_layer(home, "ground");
    assert(home_ground != NULL);
    std::uint32_t ground_gid = 0;
    for (std::uint32_t gid : home_ground->gids) {
        if (gid != 0) {
            ground_gid = gid;
            break;
        }
    }
    andortrail::map::TileSource ground_tile;
    assert(ground_gid != 0 && andortrail::map::resolve_tile_source(home, ground_gid, ground_tile));
    assert(ground_tile.pixel_x >= 0 && ground_tile.pixel_y >= 0);
    assert(ground_tile.pixel_x % 32 == 0 && ground_tile.pixel_y % 32 == 0);

    GridPosition player = {1, 4};
    assert(andortrail::map::is_walkable(home, player.x, player.y));
    const GridPosition original_player = player;
    assert(!andortrail::map::is_walkable(home, 0, 4));
    assert(!andortrail::map::move_player(home, player, -1, 0));
    assert(player.x == original_player.x && player.y == original_player.y);
    assert(andortrail::map::move_player(home, player, 1, 0));
    assert(player.x == 2 && player.y == 4);
    for (int step = 0; step < 6; ++step) {
        assert(andortrail::map::move_player(home, player, 1, 0));
    }
    assert(player.x == 8 && player.y == 4);
    assert(!andortrail::map::move_player(home, player, 0, -1));
    assert(player.x == 8 && player.y == 4);
    assert(!andortrail::map::is_walkable(home, -1, 4));

    const ViewTransform view = andortrail::map::camera_for_player(home, original_player, 400, 240);
    assert(view.visible_columns == 12 && view.visible_rows == 7);
    assert(view.camera_x == 0 && view.camera_y == 1);
    assert(view.screen_offset_x == 8 && view.screen_offset_y == 8);
    GridPosition player_screen = {-1, -1};
    assert(andortrail::map::map_to_screen(view, original_player, player_screen));
    assert(player_screen.x == 40 && player_screen.y == 104);
    GridPosition outside_screen = {-1, -1};
    assert(!andortrail::map::map_to_screen(view, {1, 9}, outside_screen));

    Document crossglen;
    assert(load_tmx(directory + "/crossglen.tmx", "crossglen", false, crossglen, &error));
    assert(crossglen.width == 30 && crossglen.height == 30);
    assert(crossglen.layers.empty());
    bool found_replacement = false;
    bool found_spawn = false;
    bool found_exit = false;
    int replacement_count = 0;
    int spawn_count = 0;
    for (const auto &group : crossglen.object_groups) {
        for (const auto &object : group.objects) {
            if (object.has_type && object.type == "replace") {
                found_replacement = true;
                ++replacement_count;
            }
            if (object.has_type && object.type == "spawn") {
                found_spawn = true;
                ++spawn_count;
            }
            found_exit |= object.has_type && object.type == "mapchange";
        }
    }
    assert(found_replacement && found_spawn && found_exit);
    assert(replacement_count == 9 && spawn_count == 19);

    TranslatedDocument translated_crossglen;
    assert(translate_document(crossglen, translated_crossglen, &error));
    assert(translated_crossglen.replacements.size() == 9);
    assert(translated_crossglen.spawn_areas.size() == 19);
    assert(!translated_crossglen.initially_active_groups.empty());
    bool found_map_change = false;
    bool found_key = false;
    bool found_script = false;
    for (const auto &event : translated_crossglen.events) {
        found_map_change |= event.kind == EventKind::MapChange && !event.target_map.empty();
        found_key |= event.kind == EventKind::Key;
        found_script |= event.kind == EventKind::Script;
        if (event.kind == EventKind::Script && event.name == "ratdom_check_kills") {
            assert(event.script_when == ScriptWhen::Step);
        }
    }
    assert(found_map_change && found_key && found_script);

    Document wild1;
    assert(load_tmx(directory + "/wild1.tmx", "wild1", false, wild1, &error));
    assert(wild1.width == 21 && wild1.height == 21);
    bool found_sign = false;
    for (const auto &group : wild1.object_groups) {
        for (const auto &object : group.objects) {
            found_sign |= object.has_type && object.type == "sign";
        }
    }
    assert(found_sign);

    const auto label_translation = [](const std::string &source) {
        return "[REVIEW]translated:" + source;
    };
    Catalog catalog;
    assert(load_catalog(directory, label_translation, catalog, &error));
    assert(catalog.maps.size() == 1296);
    assert(catalog.world_map.segments.size() == 46);
    std::size_t world_map_count = 0;
    std::size_t named_area_count = 0;
    bool found_translated_area = false;
    for (const auto &segment : catalog.world_map.segments) {
        world_map_count += segment.maps.size();
        named_area_count += segment.named_areas.size();
        for (const auto &area : segment.named_areas) {
            found_translated_area |= area.translated_name == "translated:" + area.source_name;
        }
    }
    assert(world_map_count == 917);
    assert(named_area_count == 36);
    assert(found_translated_area);

    andortrail::map::WorldMap outdated_world_map;
    const auto outdated_translation = [](const std::string &) {
        return std::string("[OUTDATED]stale label");
    };
    std::vector<std::string> map_names;
    for (const auto &map : catalog.maps) map_names.push_back(map.source.name);
    assert(andortrail::map::load_world_map(
        directory + "/worldmap.xml", map_names, outdated_translation, outdated_world_map, &error
    ));
    assert(!outdated_world_map.segments.empty());
    bool found_outdated_fallback = false;
    for (const auto &segment : outdated_world_map.segments) {
        for (const auto &area : segment.named_areas) {
            found_outdated_fallback |= area.translated_name == area.source_name;
        }
    }
    assert(found_outdated_fallback);

    std::cout << "game map parser tests passed\n";
    return 0;
}