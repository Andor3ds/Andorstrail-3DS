#include "game_quest_log.hpp"

#include <climits>
#include <cstdint>
#include <cstdio>

namespace andortrail {
namespace quest_log {
namespace {

bool read_progress(GameJsonRef value, int &progress)
{
    int64_t parsed = 0;
    if (!game_json_integer(value, &parsed) || parsed < 0 || parsed > INT_MAX) {
        return false;
    }
    progress = static_cast<int>(parsed);
    return true;
}

bool read_completion(GameJsonRef stage, bool &finished)
{
    const GameJsonRef value = game_json_object_get(stage, "finishesQuest");
    if (value.node == GAME_JSON_NO_NODE || game_json_is_null(value)) {
        finished = false;
        return true;
    }
    int progress = 0;
    if (!read_progress(value, progress)) return false;
    finished = progress != 0;
    return true;
}

}

bool active_quests(GameResources *resources, const GameCore *core,
    std::vector<Entry> &entries, std::string &error)
{
    entries.clear();
    error.clear();
    if (resources == NULL || resources->pack == NULL || core == NULL
            || core->quest_count > GAME_CORE_MAX_VALUES) {
        error = "invalid quest log configuration";
        return false;
    }

    for (std::size_t quest_index = 0; quest_index < core->quest_count; ++quest_index) {
        const GameCoreValue &progress_value = core->quests[quest_index];
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_QUEST, progress_value.id, NULL
        );
        if (record == NULL) continue;

        GameJsonDocument document;
        GameJsonRef quest;
        if (!game_resources_open_record(resources, record, &document, &quest)) {
            error = "could not read original quest record: ";
            error += progress_value.id;
            return false;
        }

        int show_in_log = 0;
        if (!read_progress(game_json_object_get(quest, "showInLog"), show_in_log)) {
            game_json_dispose(&document);
            error = "invalid showInLog value in quest: ";
            error += progress_value.id;
            return false;
        }
        if (show_in_log == 0) {
            game_json_dispose(&document);
            continue;
        }

        Entry entry;
        entry.id = progress_value.id;
        entry.progress = progress_value.value;
        char name[2048];
        if (!game_json_string_copy(game_json_object_get(quest, "name"),
                    name, sizeof(name))) {
            game_json_dispose(&document);
            error = "invalid name in quest: ";
            error += progress_value.id;
            return false;
        }
        entry.name = name;

        const GameJsonRef stages = game_json_object_get(quest, "stages");
        int selected_progress = -1;
        bool selected_stage_finishes_quest = false;
        char objective[2048] = "";
        for (std::size_t stage_index = 0;
                stage_index < game_json_array_size(stages); ++stage_index) {
            const GameJsonRef stage = game_json_array_get(stages, stage_index);
            int stage_progress = 0;
            if (!read_progress(game_json_object_get(stage, "progress"),
                        stage_progress)) {
                game_json_dispose(&document);
                error = "invalid stage progress in quest: ";
                error += progress_value.id;
                return false;
            }
            if (stage_progress > progress_value.value
                    || stage_progress < selected_progress) continue;

            char stage_objective[sizeof(objective)];
            if (!game_json_string_copy(game_json_object_get(stage, "logText"),
                        stage_objective, sizeof(stage_objective))) continue;
            bool finishes_quest = false;
            if (!read_completion(stage, finishes_quest)) {
                game_json_dispose(&document);
                error = "invalid completion marker in quest: ";
                error += progress_value.id;
                return false;
            }
            selected_progress = stage_progress;
            selected_stage_finishes_quest = finishes_quest;
            std::snprintf(objective, sizeof(objective), "%s", stage_objective);
        }

        game_json_dispose(&document);
        if (selected_progress < 0 || selected_stage_finishes_quest
                || objective[0] == '\0') continue;
        entry.objective = objective;
        entries.push_back(entry);
    }
    return true;
}

}
}
