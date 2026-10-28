#include "game_dialogue.hpp"
#include "game_items.hpp"

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
    : resources_(resources), core_(core), active_(false)
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
    std::string current_phrase = phrase_id;
    for (int route_count = 0; route_count < 16; ++route_count) {
        if (!enter_phrase(current_phrase, error)) {
            *core_ = *previous_state;
            close();
            return false;
        }
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
    *core_ = *previous_state;
    close();
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
        if (!read_integer(game_json_object_get(requirement, "value"), value,
                "requirement value", error)) return false;
        bool negate = false;
        if (!read_boolean(game_json_object_get(requirement, "negate"), false,
                negate, "negate", error)) return false;

        bool met = false;
        if (type == "questProgress" || type == "questLatestProgress") {
            met = game_core_quest_progress(core_, id.c_str()) == value;
        } else if (type == "inventoryRemove") {
            met = game_core_inventory_count(core_, id.c_str()) >= value;
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
        if (type != "dropList"
                && !read_integer(game_json_object_get(reward, "value"), value,
                    "reward value", error)) return false;

        bool applied = false;
        if (type == "questProgress") {
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
    const bool requirements_met = reply_requirements_met(reply, true, error);
    game_json_dispose(&document);
    if (!requirements_met) {
        *core_ = *previous_state;
        if (error.empty()) error = "conversation reply requirements are no longer met";
        return false;
    }

    if (!enter_phrase(selected.next_phrase_id, error)) {
        *core_ = *previous_state;
        return false;
    }
    active_ = true;
    error.clear();
    return true;
}

void Conversation::close()
{
    phrase_id_.clear();
    message_.clear();
    replies_.clear();
    active_ = false;
}

bool Conversation::active() const
{
    return active_;
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

}
}
