#ifndef ANDORSTRAIL_GAME_DIALOGUE_HPP
#define ANDORSTRAIL_GAME_DIALOGUE_HPP

#include "game_core.h"
#include "game_resources.h"

#include <cstddef>
#include <string>
#include <vector>

namespace andortrail {
namespace dialogue {

struct Reply {
    std::string text;
    std::string next_phrase_id;
    std::size_t source_index;
};

struct SpawnAreaEffect {
    std::string map_name;
    std::string area_id;
    bool active;
    bool spawn_all;
    bool remove_all;
};

class Conversation {
public:
    enum TerminalAction {
        TerminalActionNone,
        TerminalActionClose,
        TerminalActionShop,
        TerminalActionAttack,
        TerminalActionRemove
    };

    Conversation(GameResources *resources = NULL, GameCore *core = NULL);

    void set_context(GameResources *resources, GameCore *core);
    bool start(const std::string &phrase_id, std::string &error);
    bool choose(std::size_t reply_index, std::string &error);
    void close();

    bool active() const;
    TerminalAction terminal_action() const;
    const std::string &phrase_id() const;
    const std::string &message() const;
    const std::vector<Reply> &replies() const;
    std::vector<SpawnAreaEffect> take_spawn_area_effects();

private:
    bool enter_phrase_route(const std::string &phrase_id, std::string &error);
    bool enter_phrase(const std::string &phrase_id, std::string &error);
    bool reply_requirements_met(GameJsonRef reply, bool apply, std::string &error);
    bool apply_rewards(GameJsonRef phrase, std::string &error);
    bool apply_alignment_reward(GameJsonRef reward, const std::string &type,
        const std::string &id, int value, bool &applied, std::string &error);

    GameResources *resources_;
    GameCore *core_;
    std::string phrase_id_;
    std::string message_;
    std::vector<Reply> replies_;
    std::vector<SpawnAreaEffect> spawn_area_effects_;
    bool active_;
    TerminalAction terminal_action_;
};

}
}

#endif
