#include "game_dialogue.hpp"
#include "game_items.hpp"

#include <cctype>
#include <climits>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

namespace andortrail {
namespace dialogue {
namespace {

bool copy_string(GameJsonRef value, std::string &destination, const char *field,
    std::string &error)
{
    char buffer[2048];
    if (!game_json_string_copy(value, buffer, sizeof(buffer))) {
        error = std::string("invalid conversation field: ") + field;
        return false;
    }
    destination.assign(buffer);
    return true;
}

bool read_integer(GameJsonRef value, int &destination, const char *field,
    std::string &error)
{
    int64_t parsed = 0;
    if (!game_json_integer(value, &parsed) || parsed < 0 || parsed > INT_MAX) {
        error = std::string("invalid conversation integer: ") + field;
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

bool read_signed_integer(GameJsonRef value, int &destination, const char *field,
    std::string &error)
{
    int64_t parsed = 0;
    if (!game_json_integer(value, &parsed) || parsed < INT_MIN || parsed > INT_MAX) {
        error = std::string("invalid conversation integer: ") + field;
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

int java_int_wrap(int64_t value)
{
    const uint32_t bits = static_cast<uint32_t>(static_cast<uint64_t>(value));
    if (bits <= static_cast<uint32_t>(INT_MAX)) return static_cast<int>(bits);
    return -1 - static_cast<int>(UINT32_MAX - bits);
}

Conversation::TerminalAction terminal_action_for_phrase(const std::string &phrase_id)
{
    if (phrase_id.size() != 1) return Conversation::TerminalActionNone;
    switch (std::toupper(static_cast<unsigned char>(phrase_id[0]))) {
    case 'X': return Conversation::TerminalActionClose;
    case 'S': return Conversation::TerminalActionShop;
    case 'F': return Conversation::TerminalActionAttack;
    case 'R': return Conversation::TerminalActionRemove;
    default: return Conversation::TerminalActionNone;
    }
}

bool read_boolean(GameJsonRef value, bool fallback, bool &destination,
    const char *field, std::string &error)
{
    if (value.node == GAME_JSON_NO_NODE) {
        destination = fallback;
        return true;
    }
    if (!game_json_boolean(value, &destination)) {
        error = std::string("invalid conversation boolean: ") + field;
        return false;
    }
    return true;
}

}

Conversation::Conversation(GameResources *resources, GameCore *core)
    : resources_(resources), core_(core), active_(false),
      terminal_action_(TerminalActionNone)
{
}

void Conversation::set_context(GameResources *resources, GameCore *core)
{
    close();
    resources_ = resources;
    core_ = core;
}

bool Conversation::start(const std::string &phrase_id, std::string &error)
{
    error.clear();
    close();
    if (resources_ == NULL || core_ == NULL || phrase_id.empty()) {
        error = "invalid conversation configuration";
        return false;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "conversation state could not be allocated";
        return false;
    }
    *previous_state = *core_;
    if (!enter_phrase_route(phrase_id, error)) {
        *core_ = *previous_state;
        close();
        return false;
    }
    return true;
}

bool Conversation::enter_phrase_route(const std::string &phrase_id,
    std::string &error)
{
    std::string current_phrase = phrase_id;
    for (int route_count = 0; route_count < 16; ++route_count) {
        terminal_action_ = terminal_action_for_phrase(current_phrase);
        if (terminal_action_ != TerminalActionNone) {
            active_ = false;
            phrase_id_.clear();
            message_.clear();
            replies_.clear();
            error.clear();
            return true;
        }
        if (!enter_phrase(current_phrase, error)) return false;
        bool has_visible_reply_text = false;
        for (const Reply &reply : replies_) {
            if (!reply.text.empty()) {
                has_visible_reply_text = true;
                break;
            }
        }
        if (!message_.empty() || replies_.empty() || has_visible_reply_text) {
            active_ = true;
            error.clear();
            return true;
        }
        current_phrase = replies_[0].next_phrase_id;
    }
    error = "conversation exceeded automatic routing limit";
    return false;
}

bool Conversation::enter_phrase(const std::string &phrase_id, std::string &error)
{
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONVERSATION, phrase_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "conversation phrase not found: " + phrase_id;
        return false;
    }

    GameJsonDocument document;
    GameJsonRef phrase;
    if (!game_resources_open_record(resources_, record, &document, &phrase)) {
        error = "conversation phrase could not be read: " + phrase_id;
        return false;
    }

    const GameJsonRef message = game_json_object_get(phrase, "message");
    std::string next_message;
    if (message.node != GAME_JSON_NO_NODE
            && !copy_string(message, next_message, "message", error)) {
        game_json_dispose(&document);
        return false;
    }

    std::vector<Reply> available_replies;
    const GameJsonRef source_replies = game_json_object_get(phrase, "replies");
    const std::size_t reply_count = game_json_array_size(source_replies);
    for (std::size_t index = 0; index < reply_count; ++index) {
        const GameJsonRef reply = game_json_array_get(source_replies, index);
        bool eligible = false;
        if (!reply_requirements_met(reply, false, error)) {
            if (!error.empty()) {
                game_json_dispose(&document);
                return false;
            }
            continue;
        }
        eligible = true;
        if (!eligible) continue;

        Reply parsed = {"", "", index};
        const GameJsonRef text = game_json_object_get(reply, "text");
        if (text.node != GAME_JSON_NO_NODE
                && !copy_string(text, parsed.text, "reply text", error)) {
            game_json_dispose(&document);
            return false;
        }
        if (parsed.text == "N") parsed.text = "Continue";
        if (!copy_string(game_json_object_get(reply, "nextPhraseID"),
                parsed.next_phrase_id, "nextPhraseID", error)) {
            game_json_dispose(&document);
            return false;
        }
        available_replies.push_back(parsed);
    }

    const bool rewards_applied = apply_rewards(phrase, error);
    game_json_dispose(&document);
    if (!rewards_applied) {
        return false;
    }
    phrase_id_ = phrase_id;
    message_.swap(next_message);
    replies_.swap(available_replies);
    return true;
}

bool Conversation::reply_requirements_met(
    GameJsonRef reply,
    bool apply,
    std::string &error
)
{
    error.clear();
    const GameJsonRef requirements = game_json_object_get(reply, "requires");
    const std::size_t count = game_json_array_size(requirements);
    for (std::size_t index = 0; index < count; ++index) {
        const GameJsonRef requirement = game_json_array_get(requirements, index);
        std::string type;
        std::string id;
        if (!copy_string(game_json_object_get(requirement, "requireType"), type,
                "requireType", error)
                || !copy_string(game_json_object_get(requirement, "requireID"), id,
                    "requireID", error)) return false;

        int value = 0;
        const bool faction_requirement = type == "factionScore"
            || type == "factionScoreEquals";
        const GameJsonRef requirement_value = game_json_object_get(requirement, "value");
        if (!(faction_requirement
                    ? read_signed_integer(requirement_value, value,
                        "requirement value", error)
                    : read_integer(requirement_value, value,
                        "requirement value", error))) return false;
        bool negate = false;
        if (!read_boolean(game_json_object_get(requirement, "negate"), false,
                negate, "negate", error)) return false;

        bool met = false;
        if (type == "questProgress" || type == "questLatestProgress") {
            met = game_core_quest_progress(core_, id.c_str()) == value;
        } else if (type == "inventoryRemove") {
            met = game_core_inventory_count(core_, id.c_str()) >= value;
        } else if (type == "factionScore") {
            met = game_core_alignment(core_, id.c_str()) >= value;
        } else if (type == "factionScoreEquals") {
            met = game_core_alignment(core_, id.c_str()) == value;
        } else {
            error = "unsupported conversation requirement: " + type;
            return false;
        }
        if (negate) met = !met;
        if (!met) return false;
    }

    if (!apply) return true;
    for (std::size_t index = 0; index < count; ++index) {
        const GameJsonRef requirement = game_json_array_get(requirements, index);
        if (!game_json_string_equals(
                    game_json_object_get(requirement, "requireType"), "inventoryRemove")
                ) continue;
        bool negate = false;
        if (!read_boolean(game_json_object_get(requirement, "negate"), false,
                negate, "negate", error)) return false;
        if (negate) continue;
        char id[GAME_CORE_ID_CAPACITY];
        int quantity = 0;
        if (!game_json_string_copy(game_json_object_get(requirement, "requireID"),
                    id, sizeof(id))
                || !read_integer(game_json_object_get(requirement, "value"), quantity,
                    "requirement value", error)
                || !game_core_remove_item(core_, id, quantity)) {
            if (error.empty()) error = "required inventory item could not be consumed";
            return false;
        }
    }
    return true;
}

bool Conversation::apply_rewards(GameJsonRef phrase, std::string &error)
{
    const GameJsonRef rewards = game_json_object_get(phrase, "rewards");
    const std::size_t count = game_json_array_size(rewards);
    for (std::size_t index = 0; index < count; ++index) {
        const GameJsonRef reward = game_json_array_get(rewards, index);
        std::string type;
        std::string id;
        if (!copy_string(game_json_object_get(reward, "rewardType"), type,
                "rewardType", error)
                || !copy_string(game_json_object_get(reward, "rewardID"), id,
                    "rewardID", error)) return false;
        int value = 0;
        const bool alignment_reward = type == "alignmentChange"
            || type == "alignmentSet" || type == "alignmentToReg1"
            || type == "alignmentToReg2" || type == "alignmentToReg3"
            || type == "alignmentFromReg1" || type == "alignmentAdd"
            || type == "alignmentFromReg2" || type == "alignmentFromReg3"
            || type == "alignmentSub" || type == "alignmentMult"
            || type == "alignmentDiv";
        const bool spawn_area_reward = type == "spawnAll"
            || type == "removeSpawnArea" || type == "deactivateSpawnArea";
        const GameJsonRef reward_value = game_json_object_get(reward, "value");
        if (alignment_reward) {
            if (reward_value.node != GAME_JSON_NO_NODE
                    && !read_signed_integer(reward_value, value,
                        "reward value", error)) return false;
        } else if (type != "dropList" && !spawn_area_reward
                && !read_integer(reward_value, value, "reward value", error)) {
            return false;
        }

        bool applied = false;
        if (alignment_reward) {
            if (!apply_alignment_reward(reward, type, id, value, applied, error)) {
                return false;
            }
            if (!applied) continue;
        } else if (spawn_area_reward) {
            std::string map_name;
            const GameJsonRef map_value = game_json_object_get(reward, "mapName");
            if (map_value.node != GAME_JSON_NO_NODE
                    && !copy_string(map_value, map_name, "mapName", error)) {
                return false;
            }
            SpawnAreaEffect effect = {map_name, id,
                type == "spawnAll", type == "spawnAll",
                type == "removeSpawnArea"};
            spawn_area_effects_.push_back(effect);
            continue;
        } else if (type == "questProgress") {
            applied = game_core_set_quest_progress(core_, id.c_str(), value);
        } else if (type == "giveItem") {
            applied = game_core_add_item(core_, id.c_str(), value);
        } else if (type == "dropList") {
            applied = items::apply_drop_list(resources_, core_, id, error);
        } else {
            error = "unsupported conversation reward: " + type;
            return false;
        }
        if (!applied && error.empty()) {
            error = "conversation reward could not be applied: " + id;
            return false;
        }
        if (!applied) return false;
    }
    return true;
}

bool Conversation::apply_alignment_reward(GameJsonRef reward,
    const std::string &type, const std::string &id, int value, bool &applied,
    std::string &error)
{
    applied = false;
    const GameJsonRef requirements = game_json_object_get(reward, "requires");
    const std::size_t requirement_count = game_json_array_size(requirements);
    for (std::size_t index = 0; index < requirement_count; ++index) {
        const GameJsonRef requirement = game_json_array_get(requirements, index);
        std::string requirement_type;
        std::string requirement_id;
        if (!copy_string(game_json_object_get(requirement, "requireType"),
                    requirement_type, "requireType", error)
                || !copy_string(game_json_object_get(requirement, "requireID"),
                    requirement_id, "requireID", error)) return false;
        int requirement_value = 0;
        const GameJsonRef source_value = game_json_object_get(requirement, "value");
        if (source_value.node != GAME_JSON_NO_NODE
                && !read_signed_integer(source_value, requirement_value,
                    "requirement value", error)) return false;
        bool negate = false;
        if (!read_boolean(game_json_object_get(requirement, "negate"), false,
                negate, "negate", error)) return false;

        bool met = false;
        if (requirement_type == "questProgress"
                || requirement_type == "questLatestProgress") {
            met = game_core_quest_progress(core_, requirement_id.c_str())
                == requirement_value;
        } else if (requirement_type == "factionScore") {
            met = game_core_alignment(core_, requirement_id.c_str())
                >= requirement_value;
        } else if (requirement_type == "factionScoreEquals") {
            met = game_core_alignment(core_, requirement_id.c_str())
                == requirement_value;
        } else if (requirement_type == "inventoryRemove") {
            met = requirement_value >= 0
                && game_core_inventory_count(core_, requirement_id.c_str())
                    >= requirement_value;
        } else {
            error = "unsupported alignment reward requirement: "
                + requirement_type;
            return false;
        }
        if (negate) met = !met;
        if (!met) return true;
    }

    if (type == "alignmentChange") {
        applied = game_core_add_alignment(core_, id.c_str(), value);
    } else if (type == "alignmentSet") {
        applied = game_core_set_alignment(core_, id.c_str(), value);
    } else if (type == "alignmentToReg1" || type == "alignmentToReg2"
            || type == "alignmentToReg3") {
        const char *register_id = type == "alignmentToReg1" ? "reg1"
            : type == "alignmentToReg2" ? "reg2" : "reg3";
        applied = game_core_set_alignment(core_, register_id,
            game_core_alignment(core_, id.c_str()));
    } else if (type == "alignmentFromReg1") {
        applied = game_core_set_alignment(core_, id.c_str(),
            game_core_alignment(core_, "reg1"));
    } else if (type == "alignmentFromReg2" || type == "alignmentFromReg3") {
        applied = true;
    } else if (type == "alignmentAdd" || type == "alignmentSub") {
        int delta = game_core_alignment(core_, id.c_str());
        if (type == "alignmentSub") delta = java_int_wrap(-(int64_t)delta);
        applied = game_core_add_alignment(core_, "reg1", delta);
    } else if (type == "alignmentDiv") {
        int divisor = game_core_alignment(core_, id.c_str());
        if (value == 0) value = 1;
        if (divisor == 0) {
            applied = true;
        } else {
            const int numerator = java_int_wrap(
                (int64_t)game_core_alignment(core_, "reg1") * value);
            applied = game_core_set_alignment(core_, "reg1",
                java_int_wrap((int64_t)numerator / divisor));
        }
    } else if (type == "alignmentMult") {
        const int multiplier = value == 0
            ? game_core_alignment(core_, "reg1") : value;
        applied = game_core_set_alignment(core_, "reg1",
            java_int_wrap((int64_t)game_core_alignment(core_, id.c_str())
                * multiplier));
    }
    if (!applied && error.empty()) {
        error = "alignment reward could not be applied: " + id;
    }
    return applied;
}

bool Conversation::choose(std::size_t reply_index, std::string &error)
{
    if (!active_ || reply_index >= replies_.size()) {
        error = "invalid conversation reply selection";
        return false;
    }
    const Reply selected = replies_[reply_index];
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONVERSATION, phrase_id_.c_str(), NULL
    );
    if (record == NULL) {
        error = "conversation phrase disappeared: " + phrase_id_;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef phrase;
    if (!game_resources_open_record(resources_, record, &document, &phrase)) {
        error = "conversation phrase could not be read: " + phrase_id_;
        return false;
    }
    const GameJsonRef reply = game_json_array_get(
        game_json_object_get(phrase, "replies"), selected.source_index
    );
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        game_json_dispose(&document);
        error = "conversation state could not be allocated";
        return false;
    }
    *previous_state = *core_;
    const std::string previous_phrase_id = phrase_id_;
    const std::string previous_message = message_;
    const std::vector<Reply> previous_replies = replies_;
    const std::vector<SpawnAreaEffect> previous_spawn_area_effects =
        spawn_area_effects_;
    const bool previous_active = active_;
    const TerminalAction previous_terminal_action = terminal_action_;
    const bool requirements_met = reply_requirements_met(reply, true, error);
    game_json_dispose(&document);
    if (!requirements_met) {
        *core_ = *previous_state;
        if (error.empty()) error = "conversation reply requirements are no longer met";
        return false;
    }

    if (!enter_phrase_route(selected.next_phrase_id, error)) {
        *core_ = *previous_state;
        phrase_id_ = previous_phrase_id;
        message_ = previous_message;
        replies_ = previous_replies;
        spawn_area_effects_ = previous_spawn_area_effects;
        active_ = previous_active;
        terminal_action_ = previous_terminal_action;
        return false;
    }
    return true;
}

void Conversation::close()
{
    phrase_id_.clear();
    message_.clear();
    replies_.clear();
    spawn_area_effects_.clear();
    active_ = false;
    terminal_action_ = TerminalActionNone;
}

bool Conversation::active() const
{
    return active_;
}

Conversation::TerminalAction Conversation::terminal_action() const
{
    return terminal_action_;
}

const std::string &Conversation::phrase_id() const
{
    return phrase_id_;
}

const std::string &Conversation::message() const
{
    return message_;
}

const std::vector<Reply> &Conversation::replies() const
{
    return replies_;
}

std::vector<SpawnAreaEffect> Conversation::take_spawn_area_effects()
{
    std::vector<SpawnAreaEffect> effects;
    effects.swap(spawn_area_effects_);
    return effects;
}

}
}
