#include "game_core.h"
#include "game_items.hpp"
#include "game_resources.h"
#include "hud_view.hpp"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

const char *const APP_DIRECTORY = "sdmc:/3ds/andorstrail";
const char *const QUICK_SAVE_PATH = "sdmc:/3ds/andorstrail/quicksave.dat";
const char *const LEGACY_QUICK_SAVE_PATH = "sdmc:/3ds/andorstrail-save.dat";
const char *const SLOT_DIRECTORY = "sdmc:/3ds/andorstrail/Saves";
const char *const SLOT_PREFIX = "savegame";
const char *const PREFERENCES_PATH = "sdmc:/3ds/andorstrail/preferences.cfg";
const char *const LEGACY_PREFERENCES_PATH = "sdmc:/3ds/andorstrail-preferences.cfg";

struct Preferences {
    bool map_on_bottom;
    bool circle_pad_enabled;
    bool touch_movement_enabled;
    int scaling_factor_index;
};

const float SCALE_FACTORS[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
const char *const SCALE_FACTOR_LABELS[] = {
    "Map zoom 0.5x", "Map zoom 0.75x", "Map zoom 1x",
    "Map zoom 1.5x", "Map zoom 2x"
};

float scaling_factor(const Preferences &preferences)
{
    return SCALE_FACTORS[preferences.scaling_factor_index];
}

bool ensure_save_directories()
{
    if (mkdir("sdmc:/3ds", 0777) != 0 && errno != EEXIST) return false;
    if (mkdir(APP_DIRECTORY, 0777) != 0 && errno != EEXIST) return false;
    if (mkdir(SLOT_DIRECTORY, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

bool file_exists(const std::string &path)
{
    struct stat status;
    return stat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode);
}

bool files_equal(const std::string &left_path, const std::string &right_path)
{
    FILE *left = fopen(left_path.c_str(), "rb");
    FILE *right = fopen(right_path.c_str(), "rb");
    if (left == NULL || right == NULL) {
        if (left != NULL) fclose(left);
        if (right != NULL) fclose(right);
        return false;
    }

    bool equal = true;
    unsigned char left_buffer[4096];
    unsigned char right_buffer[4096];
    while (equal) {
        const size_t left_count = fread(left_buffer, 1, sizeof(left_buffer), left);
        const size_t right_count = fread(right_buffer, 1, sizeof(right_buffer), right);
        if (left_count != right_count
                || memcmp(left_buffer, right_buffer, left_count) != 0) {
            equal = false;
        } else if (left_count < sizeof(left_buffer)) {
            if (ferror(left) || ferror(right)) equal = false;
            break;
        }
    }
    if (fclose(left) != 0 || fclose(right) != 0) equal = false;
    return equal;
}

bool migrate_file(const std::string &source, const std::string &destination,
    std::string &error)
{
    if (!file_exists(source) || file_exists(destination)) return true;
    const std::string temporary = destination + ".migrate.tmp";
    FILE *input = fopen(source.c_str(), "rb");
    FILE *output = fopen(temporary.c_str(), "wb");
    if (input == NULL || output == NULL) {
        if (input != NULL) fclose(input);
        if (output != NULL) fclose(output);
        remove(temporary.c_str());
        error = "Could not open save data for migration.";
        return false;
    }

    unsigned char buffer[4096];
    bool copied = true;
    while (copied) {
        const size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count > 0 && fwrite(buffer, 1, count, output) != count) copied = false;
        if (count < sizeof(buffer)) {
            if (ferror(input)) copied = false;
            break;
        }
    }
    if (fclose(input) != 0) copied = false;
    if (fclose(output) != 0) copied = false;
    if (!copied || rename(temporary.c_str(), destination.c_str()) != 0
            || !files_equal(source, destination)) {
        remove(temporary.c_str());
        error = "Save data migration could not be verified.";
        return false;
    }
    if (remove(source.c_str()) != 0) {
        error = "Migrated save data could not be removed from its old location.";
        return false;
    }
    return true;
}

bool migrate_legacy_data(std::string &error)
{
    if (!ensure_save_directories()) {
        error = "Save directories could not be created.";
        return false;
    }

    const std::string quicksave_path(QUICK_SAVE_PATH);
    if (!file_exists(quicksave_path)) {
        static GameCore validation;
        const std::string old_quicksave(LEGACY_QUICK_SAVE_PATH);
        const std::string old_backup = old_quicksave + ".bak";
        std::string old_candidate;
        if (file_exists(old_quicksave)) {
            game_core_init(&validation);
            if (game_core_load(&validation, old_quicksave.c_str())
                    == GAME_CORE_LOAD_OK) {
                old_candidate = old_quicksave;
            }
        }
        if (old_candidate.empty() && file_exists(old_backup)) {
            game_core_init(&validation);
            if (game_core_load(&validation, old_backup.c_str())
                    == GAME_CORE_LOAD_OK) {
                old_candidate = old_backup;
            }
        }
        if (file_exists(old_candidate)) {
            if (!migrate_file(old_candidate, quicksave_path, error)) return false;
        } else if (file_exists(old_quicksave) || file_exists(old_backup)) {
            error = "The existing Continue save could not be validated for migration.";
            return false;
        }
    }

    const std::string new_preferences(PREFERENCES_PATH);
    if (!file_exists(new_preferences)
            && !migrate_file(LEGACY_PREFERENCES_PATH, new_preferences, error)) {
        return false;
    }

    DIR *directory = opendir(APP_DIRECTORY);
    if (directory == NULL) {
        error = "Manual save directory could not be opened for migration.";
        return false;
    }
    std::vector<std::string> legacy_names;
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        legacy_names.push_back(entry->d_name);
    }
    closedir(directory);

    for (const std::string &name : legacy_names) {
        const std::string prefix(SLOT_PREFIX);
        if (name.compare(0, prefix.size(), prefix) != 0
                || name.size() == prefix.size()) continue;
        const std::string backup_suffix(".bak");
        if (name.size() > backup_suffix.size()
                && name.compare(name.size() - backup_suffix.size(),
                    backup_suffix.size(), backup_suffix) == 0) continue;
        const std::string number = name.substr(prefix.size());
        if (number.empty()
                || !std::all_of(number.begin(), number.end(),
                    [](unsigned char digit) { return digit >= '0' && digit <= '9'; })) {
            continue;
        }
        const std::string source = std::string(APP_DIRECTORY) + "/" + name;
        const std::string destination =
            std::string(SLOT_DIRECTORY) + "/" + name;
        if (!migrate_file(source, destination, error)) {
            return false;
        }
    }

    for (const std::string &name : legacy_names) {
        const std::string prefix(SLOT_PREFIX);
        const std::string backup_suffix(".bak");
        if (name.compare(0, prefix.size(), prefix) != 0
                || name.size() <= prefix.size() + backup_suffix.size()
                || name.compare(name.size() - backup_suffix.size(),
                    backup_suffix.size(), backup_suffix) != 0) continue;
        const std::string slot_name = name.substr(
            0, name.size() - backup_suffix.size());
        const std::string number = slot_name.substr(prefix.size());
        if (number.empty()
                || !std::all_of(number.begin(), number.end(),
                    [](unsigned char digit) { return digit >= '0' && digit <= '9'; })) {
            continue;
        }
        const std::string source = std::string(APP_DIRECTORY) + "/" + name;
        const std::string old_primary =
            std::string(APP_DIRECTORY) + "/" + slot_name;
        const std::string destination =
            std::string(SLOT_DIRECTORY) + "/" + slot_name;
        if (!file_exists(old_primary)
                && !migrate_file(source, destination, error)) {
            return false;
        }
    }
    error.clear();
    return true;
}

std::string slot_path(int slot)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s%d", SLOT_DIRECTORY, SLOT_PREFIX, slot);
    return path;
}

std::vector<int> available_save_slots()
{
    std::vector<int> slots;
    DIR *directory = opendir(SLOT_DIRECTORY);
    if (directory == NULL) return slots;
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        const std::string name(entry->d_name);
        const std::string prefix(SLOT_PREFIX);
        if (name.compare(0, prefix.size(), prefix) != 0
                || name.size() == prefix.size()) continue;
        const std::string number = name.substr(prefix.size());
        char *end = NULL;
        const long slot = strtol(number.c_str(), &end, 10);
        if (end != number.c_str() && *end == '\0' && slot > 0 && slot <= 1000000) {
            slots.push_back(static_cast<int>(slot));
        }
    }
    closedir(directory);
    std::sort(slots.begin(), slots.end());
    return slots;
}

void load_preferences(Preferences &preferences)
{
    preferences = {false, true, true, 2};
    FILE *file = fopen(PREFERENCES_PATH, "r");
    if (file == NULL && errno == ENOENT) {
        file = fopen(LEGACY_PREFERENCES_PATH, "r");
    }
    if (file == NULL) return;
    int map_on_bottom = 0;
    int circle_pad_enabled = 1;
    int touch_movement_enabled = 1;
    int scaling_factor_index = 2;
    if (fscanf(file, "%d %d %d", &map_on_bottom, &circle_pad_enabled,
            &touch_movement_enabled) == 3
            && (map_on_bottom == 0 || map_on_bottom == 1)
            && (circle_pad_enabled == 0 || circle_pad_enabled == 1)
            && (touch_movement_enabled == 0 || touch_movement_enabled == 1)) {
        preferences.map_on_bottom = map_on_bottom != 0;
        preferences.circle_pad_enabled = circle_pad_enabled != 0;
        preferences.touch_movement_enabled = touch_movement_enabled != 0;
        if (fscanf(file, "%d", &scaling_factor_index) == 1
                && scaling_factor_index >= 0
                && scaling_factor_index
                    < static_cast<int>(sizeof(SCALE_FACTORS) / sizeof(SCALE_FACTORS[0]))) {
            preferences.scaling_factor_index = scaling_factor_index;
        }
    }
    fclose(file);
}

bool save_preferences(const Preferences &preferences)
{
    if (!ensure_save_directories()) return false;
    const std::string temporary_path = std::string(PREFERENCES_PATH) + ".tmp";
    const std::string backup_path = std::string(PREFERENCES_PATH) + ".bak";
    FILE *file = fopen(temporary_path.c_str(), "w");
    if (file == NULL) return false;
    const bool written = fprintf(file, "%d %d %d %d\n",
        preferences.map_on_bottom ? 1 : 0,
        preferences.circle_pad_enabled ? 1 : 0,
        preferences.touch_movement_enabled ? 1 : 0,
        preferences.scaling_factor_index) >= 0;
    if (fclose(file) != 0 || !written) {
        remove(temporary_path.c_str());
        return false;
    }
    const bool target_exists = file_exists(PREFERENCES_PATH);
    if (target_exists && rename(PREFERENCES_PATH, backup_path.c_str()) != 0) {
        remove(temporary_path.c_str());
        return false;
    }
    if (rename(temporary_path.c_str(), PREFERENCES_PATH) != 0) {
        if (target_exists) rename(backup_path.c_str(), PREFERENCES_PATH);
        remove(temporary_path.c_str());
        return false;
    }
    if (target_exists) remove(backup_path.c_str());
    return true;
}

bool save_resume_game(andortrail::platform3ds::MapRenderer &renderer,
    GameCore &core, const char *path, std::string &error)
{
    if (!ensure_save_directories()) {
        error = "Save directory could not be created.";
        return false;
    }
    if (!renderer.persist_state(error)) return false;
    if (!game_core_save(&core, path)) {
        error = "Save file could not be written.";
        return false;
    }
    error.clear();
    return true;
}

struct ResumeSaveHook {
    andortrail::platform3ds::MapRenderer *renderer;
    andortrail::platform3ds::HudView *hud;
    GameCore *core;
    const char *path;
    bool *game_started;
    bool *menu_returns_to_game;
    bool *save_permitted;
    bool *exit_save_completed;
    std::string *error;
};

void save_resume_on_suspend(APT_HookType hook, void *parameter)
{
    ResumeSaveHook *context = static_cast<ResumeSaveHook *>(parameter);
    if (context == NULL) return;
    if ((hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONEXIT)
            && (*context->game_started || *context->menu_returns_to_game)
            && *context->save_permitted) {
        const bool saved = save_resume_game(*context->renderer, *context->core,
            context->path, *context->error);
        if (hook == APTHOOK_ONEXIT && saved) {
            *context->exit_save_completed = true;
        }
    } else if (hook == APTHOOK_ONRESTORE && !context->error->empty()) {
        context->hud->set_notice(*context->error);
    }
}

}

int main(void)
{
    gfxInitDefault();
    hidSetRepeatParameters(15, 10);
    static GameCore core;
    game_core_init(&core);
    std::string migration_error;
    migrate_legacy_data(migration_error);
    const char *save_path = QUICK_SAVE_PATH;
    GameCoreLoadResult save_load_result = game_core_load(&core, save_path);
    if (save_load_result != GAME_CORE_LOAD_OK) {
        static GameCore legacy_core;
        game_core_init(&legacy_core);
        const GameCoreLoadResult legacy_result =
            game_core_load(&legacy_core, LEGACY_QUICK_SAVE_PATH);
        if (legacy_result == GAME_CORE_LOAD_OK) {
            core = legacy_core;
            save_load_result = GAME_CORE_LOAD_OK;
        } else if (save_load_result == GAME_CORE_LOAD_NOT_FOUND
                && legacy_result == GAME_CORE_LOAD_ERROR) {
            save_load_result = GAME_CORE_LOAD_ERROR;
        }
    }
    bool save_permitted = save_load_result != GAME_CORE_LOAD_ERROR;
    const bool quicksave_exists = save_load_result == GAME_CORE_LOAD_OK;
    const bool continue_available = quicksave_exists && core.health > 0;
    Preferences preferences;
    load_preferences(preferences);

    const bool romfs_ready = R_SUCCEEDED(romfsInit());
    GameResources resources = {};
    const bool resources_ready = romfs_ready
        && game_resources_init(&resources, "romfs:/game_data.pack");
    const bool seed_starting_inventory = save_load_result != GAME_CORE_LOAD_OK
        || (save_load_result == GAME_CORE_LOAD_OK && core.inventory_count == 0
            && core.gold == 0 && core.quest_count == 0 && core.level == 1
            && core.total_experience == 1);
    bool start_items_ready = true;
    std::string error;
    if (resources_ready && seed_starting_inventory
            && !andortrail::items::apply_drop_list(&resources, &core, "startitems", error)) {
        start_items_ready = false;
        error = "Starting inventory could not be initialized: " + error;
    }
    bool c3d_ready = false;
    bool c2d_ready = false;
    bool cfgu_ready = false;
    bool renderer_ready = false;
    bool hud_ready = false;
    C3D_RenderTarget *top = NULL;
    C3D_RenderTarget *bottom = NULL;
    C2D_Font system_font = NULL;
    C2D_TextBuf text_buffer = NULL;
    andortrail::platform3ds::MapRenderer renderer;
    andortrail::platform3ds::HudView hud;

    if (romfs_ready && C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        c3d_ready = true;
    }
    if (c3d_ready && C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        c2d_ready = true;
        C2D_Prepare();
        top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
        renderer_ready = top != NULL && bottom != NULL && resources_ready && start_items_ready
            && renderer.initialize(
            "romfs:/maps", "home", "romfs:/gfx/tilesets.list", "romfs:/gfx",
            &resources, &core, error
        );
        if (renderer_ready) {
            if (R_SUCCEEDED(cfguInit())) {
                cfgu_ready = true;
                system_font = C2D_FontLoadSystem(CFG_REGION_USA);
            }
            text_buffer = C2D_TextBufNew(8192);
            if (text_buffer == NULL) error = "HUD text buffer could not be allocated";
            else hud_ready = hud.initialize(system_font, text_buffer, &resources,
                &core, &renderer, error);
            renderer_ready = renderer_ready && hud_ready;
        }
    }
    if (!renderer_ready && error.empty()) {
        if (!c3d_ready) error = "Citro3D could not be initialized";
        else if (!c2d_ready) error = "Citro2D could not be initialized";
        else if (top == NULL) error = "top screen render target could not be created";
        else if (bottom == NULL) error = "bottom screen render target could not be created";
        else if (!resources_ready) error = "original game resource pack could not be opened";
        else error = "map or HUD initialization failed";
    }
    renderer_ready = renderer_ready && hud_ready;
    std::string save_error;
    const auto save_game = [&]() {
        return save_resume_game(renderer, core, save_path, save_error);
    };

    bool game_started = false;
    bool menu_returns_to_game = false;
    andortrail::platform3ds::StartMenuPage menu_page =
        andortrail::platform3ds::START_MENU_MAIN;
    int menu_selection = 0;
    std::string menu_message = save_load_result == GAME_CORE_LOAD_ERROR
        ? "Continue is unavailable because the quicksave could not be loaded."
        : migration_error;
    std::vector<int> menu_slots;
    std::vector<std::string> menu_slot_labels;
    int pending_save_slot = -1;
    const auto initialize_world = [&]() {
        renderer_ready = false;
        hud_ready = false;
        if (!renderer.initialize("romfs:/maps", "home",
                "romfs:/gfx/tilesets.list", "romfs:/gfx",
                &resources, &core, error)) return false;
        if (!hud.initialize(system_font, text_buffer, &resources,
                &core, &renderer, error)) {
            renderer.dispose();
            return false;
        }
        renderer_ready = true;
        hud_ready = true;
        return true;
    };
    const auto replace_world = [&](const GameCore &replacement) {
        static GameCore previous;
        previous = core;
        hud.dispose();
        renderer.dispose();
        core = replacement;
        if (initialize_world()) return true;
        const std::string replacement_error = error;
        core = previous;
        if (!initialize_world()) {
            error = replacement_error + "; previous game could not be restored: " + error;
        } else {
            error = replacement_error;
        }
        return false;
    };
    const auto start_new_game = [&]() {
        static GameCore new_game;
        game_core_init(&new_game);
        if (!resources_ready
                || !andortrail::items::apply_drop_list(
                    &resources, &new_game, "startitems", error)) {
            menu_message = "Starting inventory could not be initialized: " + error;
            return false;
        }
        if (!replace_world(new_game)) {
            menu_message = "New game could not be initialized: " + error;
            return false;
        }
        save_permitted = true;
        game_started = true;
        if (!save_game()) {
            menu_message = "New game started, but Continue could not be saved: "
                + save_error;
            hud.set_notice(menu_message);
            return true;
        }
        menu_message.clear();
        return true;
    };
    const auto load_slot = [&](int slot) {
        static GameCore loaded;
        game_core_init(&loaded);
        const std::string path = slot_path(slot);
        if (game_core_load(&loaded, path.c_str()) != GAME_CORE_LOAD_OK) {
            menu_message = "The selected save slot could not be loaded.";
            return false;
        }
        if (!replace_world(loaded)) {
            menu_message = "The selected save slot could not be loaded: " + error;
            return false;
        }
        save_permitted = true;
        game_started = true;
        menu_message.clear();
        return true;
    };
    const auto save_slot = [&](int slot) {
        if (!ensure_save_directories() || !renderer.persist_state(save_error)
                || !game_core_save(&core, slot_path(slot).c_str())) {
            if (save_error.empty()) save_error = "Save file could not be written.";
            menu_message = save_error;
            return false;
        }
        menu_message = "Game saved.";
        return true;
    };
    const auto about_section = [](int section) {
        switch (section) {
        case 0: return std::string("Andor's Trail is a quest-driven fantasy RPG.");
        case 1: return std::string("Original author and contributor credits are not bundled in this 3DS build.");
        case 2: return std::string("See the upstream project for full license and copyright notices.");
        default: return std::string("Help: D-pad/Circle Pad move; A interacts; X opens Overview; Y swaps screens; L/R changes tabs.");
        }
    };
    const auto refresh_slots = [&]() {
        menu_slots = available_save_slots();
        menu_slot_labels.clear();
        static GameCore preview;
        for (int slot : menu_slots) {
            const std::string path = slot_path(slot);
            char label[96];
            if (game_core_load(&preview, path.c_str()) == GAME_CORE_LOAD_OK) {
                snprintf(label, sizeof(label), "%d. level %d, %d exp, %d gold",
                    slot, preview.level, preview.total_experience, preview.gold);
            } else {
                snprintf(label, sizeof(label), "%d. Savegame unavailable", slot);
            }
            menu_slot_labels.push_back(label);
        }
    };

    u64 next_circle_move_time = 0;
    bool map_on_bottom = preferences.map_on_bottom;
    bool exit_save_completed = false;
    ResumeSaveHook save_hook_context = {
        &renderer, &hud, &core, save_path, &game_started,
        &menu_returns_to_game, &save_permitted, &exit_save_completed,
        &save_error
    };
    aptHookCookie save_hook = {};
    aptHook(&save_hook, save_resume_on_suspend, &save_hook_context);
    bool touch_destination_active = false;
    andortrail::map::GridPosition touch_destination = {0, 0};
    std::string touch_destination_map;
    u64 next_touch_move_time = 0;
    if (!renderer_ready) {
        consoleInit(GFX_TOP, NULL);
        printf("Andor's Trail\nMap startup failed:\n%s\n", error.c_str());
        printf("Press START to exit.\n");
    }

    while (aptMainLoop()) {
        hidScanInput();
        const u32 keys_down = hidKeysDownRepeat();
        if (keys_down & KEY_START) break;
        if (renderer_ready && !game_started) {
            std::vector<std::string> menu_entries;
            if (menu_page == andortrail::platform3ds::START_MENU_MAIN) {
                if (continue_available) menu_entries.push_back("Continue current game");
                menu_entries.push_back("New game");
                menu_entries.push_back("Load");
                menu_entries.push_back("Preferences");
                menu_entries.push_back("About / Help");
            } else if (menu_page == andortrail::platform3ds::START_MENU_LOAD
                    || menu_page == andortrail::platform3ds::START_MENU_SAVE) {
                for (const std::string &label : menu_slot_labels) {
                    menu_entries.push_back(label);
                }
                if (menu_page == andortrail::platform3ds::START_MENU_SAVE) {
                    menu_entries.push_back("Create new Savegame slot");
                }
            } else if (menu_page
                    == andortrail::platform3ds::START_MENU_CONFIRM_NEW_GAME) {
                menu_entries.push_back("Cancel");
                menu_entries.push_back("Start game");
            } else if (menu_page
                    == andortrail::platform3ds::START_MENU_CONFIRM_OVERWRITE) {
                menu_entries.push_back("Cancel");
                menu_entries.push_back("Overwrite");
            } else if (menu_page
                    == andortrail::platform3ds::START_MENU_PREFERENCES) {
                menu_entries.push_back(std::string("Map screen: ")
                    + (preferences.map_on_bottom ? "Bottom" : "Top"));
                menu_entries.push_back(std::string("Circle Pad movement: ")
                    + (preferences.circle_pad_enabled ? "On" : "Off"));
                menu_entries.push_back(std::string("Touch movement: ")
                    + (preferences.touch_movement_enabled ? "On" : "Off"));
                menu_entries.push_back(std::string("Scaling factor: ")
                    + SCALE_FACTOR_LABELS[preferences.scaling_factor_index]);
                menu_entries.push_back("Back");
            } else if (menu_page == andortrail::platform3ds::START_MENU_ABOUT) {
                menu_entries.push_back("About");
                menu_entries.push_back("Authors");
                menu_entries.push_back("License");
                menu_entries.push_back("Help");
            }

            if (!menu_entries.empty()) {
                menu_selection = std::max(0, std::min(menu_selection,
                    static_cast<int>(menu_entries.size()) - 1));
                if (keys_down & KEY_UP) {
                    menu_selection = (menu_selection
                        + static_cast<int>(menu_entries.size()) - 1)
                        % static_cast<int>(menu_entries.size());
                } else if (keys_down & KEY_DOWN) {
                    menu_selection = (menu_selection + 1)
                        % static_cast<int>(menu_entries.size());
                }
            } else {
                menu_selection = 0;
            }

            bool menu_action_touch = false;
            const u32 menu_keys_pressed = hidKeysDown();
            if (menu_keys_pressed & KEY_TOUCH) {
                touchPosition touch;
                hidTouchRead(&touch);
                const int touch_x = touch.px;
                const int touch_y = touch.py;
                if (menu_page == andortrail::platform3ds::START_MENU_MAIN
                        && menu_entries.size() >= 4
                        && menu_entries.size() <= 5) {
                    const bool has_continue = menu_entries.size() == 5;
                    if (has_continue && touch_y >= 60 && touch_y < 89) {
                        menu_selection = 0;
                        menu_action_touch = true;
                    } else {
                        const int first_row = has_continue ? 101 : 88;
                        const int row = touch_y >= first_row
                                && touch_y < first_row + 79
                            ? (touch_y - first_row) / 43 : -1;
                        if (row >= 0 && row < 2) {
                            const int offset = has_continue ? 1 : 0;
                            menu_selection = offset + row * 2
                                + (touch_x >= 160 ? 1 : 0);
                            menu_action_touch = true;
                        }
                    }
                } else if (touch_y >= 98 && !menu_entries.empty()) {
                    const int first_y = 98;
                    const int available_height = 115;
                    const int row_height = std::max(1,
                        available_height / static_cast<int>(menu_entries.size()));
                    const int option = (touch_y - first_y) / row_height;
                    if (option >= 0
                            && option < static_cast<int>(menu_entries.size())) {
                        menu_selection = option;
                        if (menu_page
                                == andortrail::platform3ds::START_MENU_PREFERENCES
                                && option < 4) {
                            if (option == 0) preferences.map_on_bottom =
                                !preferences.map_on_bottom;
                            else if (option == 1) preferences.circle_pad_enabled =
                                !preferences.circle_pad_enabled;
                            else if (option == 2) preferences.touch_movement_enabled =
                                !preferences.touch_movement_enabled;
                            else preferences.scaling_factor_index =
                                (preferences.scaling_factor_index + 1)
                                % (sizeof(SCALE_FACTORS) / sizeof(SCALE_FACTORS[0]));
                            if (!save_preferences(preferences)) {
                                menu_message = "Preferences could not be saved.";
                            } else menu_message.clear();
                            if (option == 0) map_on_bottom =
                                preferences.map_on_bottom;
                        } else if (menu_page
                                != andortrail::platform3ds::START_MENU_PREFERENCES
                                && menu_page
                                != andortrail::platform3ds::START_MENU_ABOUT) {
                            menu_action_touch = true;
                        } else if (menu_page
                                == andortrail::platform3ds::START_MENU_ABOUT) {
                            menu_message = about_section(option);
                        } else if (menu_page
                                == andortrail::platform3ds::START_MENU_PREFERENCES) {
                            menu_page = andortrail::platform3ds::START_MENU_MAIN;
                            menu_selection = 0;
                            menu_message.clear();
                        }
                    }
                }
            }

            if ((keys_down & KEY_B)
                    && menu_page != andortrail::platform3ds::START_MENU_MAIN) {
                if (menu_page
                        == andortrail::platform3ds::START_MENU_CONFIRM_OVERWRITE) {
                    menu_page = andortrail::platform3ds::START_MENU_SAVE;
                    menu_selection = 0;
                    menu_message = "Select slot";
                } else if (menu_returns_to_game) {
                    game_started = true;
                    menu_returns_to_game = false;
                } else {
                    menu_page = andortrail::platform3ds::START_MENU_MAIN;
                    menu_selection = 0;
                    menu_message.clear();
                }
            } else if ((keys_down & KEY_A) || menu_action_touch) {
                if (menu_page == andortrail::platform3ds::START_MENU_MAIN) {
                    int action = menu_selection;
                    if (continue_available) {
                        if (action == 0) {
                            game_started = true;
                            menu_message.clear();
                        } else --action;
                    }
                    if (!game_started && action == 0) {
                        if (quicksave_exists) {
                            menu_page = andortrail::platform3ds::START_MENU_CONFIRM_NEW_GAME;
                            menu_selection = 0;
                            menu_message = "You will lose your current game and your current character, are you sure you want to start a new game?";
                        } else start_new_game();
                    } else if (!game_started && action == 1) {
                        menu_page = andortrail::platform3ds::START_MENU_LOAD;
                        refresh_slots();
                        menu_selection = 0;
                        menu_message = "Select slot";
                    } else if (!game_started && action == 2) {
                        menu_page = andortrail::platform3ds::START_MENU_PREFERENCES;
                        menu_selection = 0;
                        menu_message = "3DS controls and display preferences";
                    } else if (!game_started && action == 3) {
                        menu_page = andortrail::platform3ds::START_MENU_ABOUT;
                        menu_selection = 0;
                        menu_message = "Quest-driven fantasy RPG";
                    }
                } else if (menu_page
                        == andortrail::platform3ds::START_MENU_CONFIRM_NEW_GAME) {
                    if (menu_selection == 1) {
                        if (!start_new_game()) {
                            menu_page = andortrail::platform3ds::START_MENU_MAIN;
                            menu_selection = 0;
                        }
                    } else {
                        menu_page = andortrail::platform3ds::START_MENU_MAIN;
                        menu_selection = 0;
                        menu_message.clear();
                    }
                } else if (menu_page
                        == andortrail::platform3ds::START_MENU_CONFIRM_OVERWRITE) {
                    if (menu_selection == 1 && pending_save_slot > 0) {
                        if (save_slot(pending_save_slot)) {
                            game_started = true;
                            menu_returns_to_game = false;
                            pending_save_slot = -1;
                        } else {
                            menu_page = andortrail::platform3ds::START_MENU_SAVE;
                            menu_selection = 0;
                        }
                    } else {
                        menu_page = andortrail::platform3ds::START_MENU_SAVE;
                        menu_selection = 0;
                        menu_message = "Select slot";
                    }
                } else if (menu_page == andortrail::platform3ds::START_MENU_LOAD) {
                    if (menu_selection < static_cast<int>(menu_slots.size())) {
                        load_slot(menu_slots[static_cast<std::size_t>(menu_selection)]);
                    }
                } else if (menu_page == andortrail::platform3ds::START_MENU_SAVE) {
                    int slot = 0;
                    if (menu_selection < static_cast<int>(menu_slots.size())) {
                        slot = menu_slots[static_cast<std::size_t>(menu_selection)];
                        pending_save_slot = slot;
                        menu_page = andortrail::platform3ds::START_MENU_CONFIRM_OVERWRITE;
                        menu_selection = 0;
                        menu_message = "Are you sure you want to overwrite this savegame?";
                    } else if (menu_selection == static_cast<int>(menu_slots.size())) {
                        slot = menu_slots.empty() ? 1 : menu_slots.back() + 1;
                        if (slot > 0 && save_slot(slot)) {
                            game_started = true;
                            menu_returns_to_game = false;
                        }
                    }
                } else if (menu_page
                        == andortrail::platform3ds::START_MENU_PREFERENCES) {
                    if (menu_selection < 4) {
                        if (menu_selection == 0) {
                            preferences.map_on_bottom = !preferences.map_on_bottom;
                            map_on_bottom = preferences.map_on_bottom;
                        } else if (menu_selection == 1) {
                            preferences.circle_pad_enabled =
                                !preferences.circle_pad_enabled;
                        } else if (menu_selection == 2) {
                            preferences.touch_movement_enabled =
                                !preferences.touch_movement_enabled;
                        } else {
                            preferences.scaling_factor_index =
                                (preferences.scaling_factor_index + 1)
                                % (sizeof(SCALE_FACTORS) / sizeof(SCALE_FACTORS[0]));
                        }
                        if (!save_preferences(preferences)) {
                            menu_message = "Preferences could not be saved.";
                        } else menu_message.clear();
                    } else {
                        menu_page = andortrail::platform3ds::START_MENU_MAIN;
                        menu_selection = 0;
                        menu_message.clear();
                    }
                } else if (menu_page == andortrail::platform3ds::START_MENU_ABOUT) {
                    menu_message = about_section(menu_selection);
                }
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
            C2D_TextBufClear(text_buffer);
            C2D_SceneBegin(top);
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
            hud.draw_start_title();
            C2D_SceneBegin(bottom);
            char preview[96];
            std::string menu_body = menu_message;
            if (menu_page == andortrail::platform3ds::START_MENU_MAIN
                    && continue_available) {
                snprintf(preview, sizeof(preview), "level %d, %d exp, %d gold",
                    core.level, core.total_experience, core.gold);
                menu_body = preview;
            } else if (menu_page
                    == andortrail::platform3ds::START_MENU_CONFIRM_NEW_GAME) {
                menu_body = "You will lose your current game and your current character, are you sure you want to start a new game?";
            }
            hud.draw_start_screen(menu_page, menu_selection, menu_entries, menu_body);
            C3D_FrameEnd(0);
            if (game_started) {
                touch_destination_active = false;
                next_touch_move_time = 0;
            }
            continue;
        }
        if (renderer_ready) {
            renderer.update(osGetTime());
            hud.set_combat_message(renderer.interaction_message());
            andortrail::platform3ds::HudAction hud_action =
                andortrail::platform3ds::HUD_ACTION_NONE;
            const u32 keys_pressed = hidKeysDown();
            if (renderer.has_encounter()) touch_destination_active = false;
            if ((keys_pressed & KEY_TOUCH) && renderer.has_encounter()) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (renderer.has_encounter_info()) {
                    renderer.return_to_encounter();
                } else {
                    int touch_x = touch.px;
                    if (map_on_bottom) touch_x -= 40;
                    const int option = touch_x < 105 ? 0
                        : (touch_x < 215 ? 1 : 2);
                    renderer.choose_encounter_option(option);
                }
            } else if ((hidKeysHeld() & KEY_TOUCH)
                    && preferences.touch_movement_enabled && map_on_bottom
                    && !renderer.has_modal_interaction()) {
                touchPosition touch;
                hidTouchRead(&touch);
                const andortrail::map::ViewTransform view =
                    renderer.camera(320, 240, scaling_factor(preferences));
                const int map_x = touch.px - view.screen_offset_x;
                const int map_y = touch.py - view.screen_offset_y;
                if (map_x >= 0 && map_y >= 0
                        && map_x < view.visible_columns * view.tile_size
                        && map_y < view.visible_rows * view.tile_size) {
                    touch_destination.x = view.camera_x
                        + map_x / view.tile_size;
                    touch_destination.y = view.camera_y
                        + map_y / view.tile_size;
                    touch_destination_map = renderer.map_id();
                    touch_destination_active = true;
                    next_touch_move_time = 0;
                }
            } else if ((keys_pressed & KEY_TOUCH) && !map_on_bottom
                    && preferences.touch_movement_enabled) {
                touchPosition touch;
                hidTouchRead(&touch);
                hud_action = hud.handle_touch(touch.px, touch.py);
            }
            if ((hidKeysHeld() & KEY_TOUCH)
                    && preferences.touch_movement_enabled && !map_on_bottom
                    && !renderer.has_modal_interaction()) {
                touchPosition touch;
                hidTouchRead(&touch);
                hud.handle_touch_move(touch.px, touch.py);
            } else if (!(hidKeysHeld() & KEY_TOUCH)) {
                hud.end_touch();
            }
            const bool hud_consumed = renderer.has_modal_interaction()
                ? false : hud.handle_keys(keys_down, hud_action);
            if (hud_action
                    == andortrail::platform3ds::HUD_ACTION_SWAP_SCREENS
                    && (keys_pressed & KEY_Y)) {
                map_on_bottom = !map_on_bottom;
                preferences.map_on_bottom = map_on_bottom;
                if (!save_preferences(preferences)) {
                    hud.set_notice("Preferences could not be saved.");
                }
                touch_destination_active = false;
                next_touch_move_time = 0;
            }
            if (hud_action == andortrail::platform3ds::HUD_ACTION_SAVE) {
                menu_page = andortrail::platform3ds::START_MENU_SAVE;
                refresh_slots();
                menu_selection = 0;
                menu_message = "Select slot";
                menu_returns_to_game = true;
                game_started = false;
            }

            if (map_on_bottom && touch_destination_active
                    && !renderer.has_modal_interaction()
                    && osGetTime() >= next_touch_move_time) {
                if (touch_destination_map != renderer.map_id()) {
                    touch_destination_active = false;
                }
            }
            if (map_on_bottom && touch_destination_active
                    && !renderer.has_modal_interaction()
                    && osGetTime() >= next_touch_move_time) {
                const andortrail::map::GridPosition player =
                    renderer.player_position();
                if (player.x == touch_destination.x
                        && player.y == touch_destination.y) {
                    touch_destination_active = false;
                } else {
                    if (!renderer.move_player_toward(touch_destination)) {
                        touch_destination_active = false;
                    } else {
                        if (renderer.has_encounter()) {
                            touch_destination_active = false;
                        }
                        next_touch_move_time = osGetTime() + 167;
                    }
                }
            }

            if (!hud_consumed) {
                if (renderer.has_modal_interaction()) {
                    if (renderer.has_encounter_info()) {
                        if (keys_down & (KEY_A | KEY_B)) {
                            renderer.return_to_encounter();
                        }
                    } else if (renderer.has_encounter()) {
                        if (keys_down & (KEY_LEFT | KEY_UP)) {
                            renderer.move_encounter_selection(-1);
                        }
                        if (keys_down & (KEY_RIGHT | KEY_DOWN)) {
                            renderer.move_encounter_selection(1);
                        }
                        if (keys_down & KEY_A) {
                            renderer.choose_encounter_option(
                                renderer.selected_encounter_option());
                        }
                        if (keys_down & KEY_B) {
                            renderer.choose_encounter_option(2);
                        }
                    } else if (renderer.has_conversation()) {
                        if (keys_down & KEY_UP) renderer.move_conversation_selection(-1);
                        if (keys_down & KEY_DOWN) renderer.move_conversation_selection(1);
                        if (keys_down & KEY_A) {
                            std::string dialogue_error;
                            if (!renderer.choose_conversation_reply(dialogue_error)) {
                                hud.set_notice(dialogue_error.c_str());
                            }
                        }
                        if (keys_down & KEY_B) {
                            renderer.dismiss_modal_interaction();
                        }
                    } else if (renderer.has_shop()) {
                        if (keys_down & KEY_A) {
                            std::string shop_error;
                            if (!renderer.buy_shop_bread(shop_error)) {
                                hud.set_notice(shop_error.c_str());
                            }
                        }
                        if (keys_down & KEY_B) {
                            renderer.dismiss_modal_interaction();
                        }
                    } else if (keys_down & (KEY_A | KEY_B)) {
                        renderer.dismiss_modal_interaction();
                    }
                } else {
                    if (keys_down & KEY_A) {
                        if (renderer.can_attack_with_action_button()) {
                            const bool attacked = renderer.attack_nearby_enemy();
                            if (!attacked && !renderer.interaction_message().empty()) {
                                hud.set_notice(renderer.interaction_message().c_str());
                            }
                        } else {
                            renderer.interact();
                        }
                    } else if ((keys_down & KEY_B) && renderer.is_in_combat()) {
                        renderer.end_combat_turn();
                    } else if (renderer.is_in_combat()
                            && (keys_down & (KEY_L | KEY_R))) {
                        if (renderer.cycle_combat_target(
                                (keys_down & KEY_R) ? 1 : -1)) {
                            hud.set_notice(renderer.interaction_message().c_str());
                        }
                    } else {
                        int move_x = 0;
                        int move_y = 0;
                        if (keys_down & KEY_LEFT) move_x = -1;
                        else if (keys_down & KEY_RIGHT) move_x = 1;
                        if (keys_down & KEY_UP) move_y = -1;
                        else if (keys_down & KEY_DOWN) move_y = 1;

                        circlePosition circle;
                        hidCircleRead(&circle);
                        const int circle_x = !preferences.circle_pad_enabled ? 0
                            : (circle.dx > 80 ? 1
                                : (circle.dx < -80 ? -1 : 0));
                        const int circle_y = !preferences.circle_pad_enabled ? 0
                            : (circle.dy > 80 ? -1
                                : (circle.dy < -80 ? 1 : 0));
                        const bool circle_active = circle_x != 0 || circle_y != 0;
                        const u64 now = osGetTime();
                        if (circle_active && now >= next_circle_move_time) {
                            if (move_x == 0) move_x = circle_x;
                            if (move_y == 0) move_y = circle_y;
                            next_circle_move_time = now + 167;
                        } else if (!circle_active) {
                            next_circle_move_time = 0;
                        }

                        if (move_x != 0 || move_y != 0) {
                            const bool moved = renderer.move_player(move_x, move_y);
                            if (!moved && renderer.is_in_combat()
                                    && !renderer.interaction_message().empty()) {
                                hud.set_notice(
                                    renderer.interaction_message().c_str());
                            }
                        }
                    }
                }
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
            C2D_TextBufClear(text_buffer);
            C3D_RenderTarget *map_target = map_on_bottom ? bottom : top;
            C2D_SceneBegin(map_target);
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
            renderer.draw(map_on_bottom ? 320 : 400, 240,
                scaling_factor(preferences));
            hud.draw_combat_overlay();
            if (renderer.has_modal_interaction()) {
                std::string text = renderer.interaction_message();
                if (renderer.has_encounter_info()) {
                    text += "\n\nA/B: close";
                } else if (renderer.has_conversation()) {
                    const std::vector<andortrail::dialogue::Reply> &replies =
                        renderer.conversation_replies();
                    for (std::size_t index = 0; index < replies.size(); ++index) {
                        text += "\n";
                        text += index == renderer.selected_conversation_reply() ? "> " : "  ";
                        text += replies[index].text;
                    }
                    text += "\n\nUp/Down: select  A: choose  B: close";
                } else if (renderer.has_shop()) {
                    text += "\n\nA: buy bread  B: close";
                } else {
                    text += "\n\nA/B: close";
                }
                hud.draw_interaction(text.c_str());
                if (renderer.has_encounter() && !renderer.has_encounter_info()) {
                    hud.draw_encounter_options(
                        renderer.selected_encounter_option());
                }
            }
            C2D_SceneBegin(map_on_bottom ? top : bottom);
            C3D_Mtx view_matrix;
            C2D_ViewSave(&view_matrix);
            if (map_on_bottom) C2D_ViewTranslate(40.0f, 0.0f);
            hud.draw(map_on_bottom ? 40 : 0);
            if (renderer.has_encounter()) {
                std::string text = renderer.interaction_message();
                if (renderer.has_encounter_info()) {
                    text += "\n\nA/B: close";
                }
                hud.draw_interaction(text.c_str());
                if (!renderer.has_encounter_info()) {
                    hud.draw_encounter_options(
                        renderer.selected_encounter_option());
                }
            }
            C2D_ViewRestore(&view_matrix);
            C3D_FrameEnd(0);
        } else {
            gspWaitForVBlank();
        }
        if (game_started) game_core_update(&core);
    }

    aptUnhook(&save_hook);
    if (!exit_save_completed && (game_started || menu_returns_to_game)
            && save_permitted && !save_game()) {
        consoleInit(GFX_TOP, NULL);
        printf("Andor's Trail\nSave failed:\n%s\n",
            save_error.empty() ? save_path : save_error.c_str());
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
    }

    renderer.dispose();
    hud.dispose();
    if (text_buffer != NULL) C2D_TextBufDelete(text_buffer);
    if (system_font != NULL) C2D_FontFree(system_font);
    if (top != NULL) C3D_RenderTargetDelete(top);
    if (bottom != NULL) C3D_RenderTargetDelete(bottom);
    if (c2d_ready) C2D_Fini();
    if (c3d_ready) C3D_Fini();
    if (cfgu_ready) cfguExit();
    if (resources_ready) game_resources_dispose(&resources);
    if (romfs_ready) romfsExit();
    game_core_shutdown(&core);
    gfxExit();
    return 0;
}