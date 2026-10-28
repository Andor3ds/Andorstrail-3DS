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

class Conversation {
public:
    Conversation(GameResources *resources = NULL, GameCore *core = NULL);

    void set_context(GameResources *resources, GameCore *core);
    bool start(const std::string &phrase_id, std::string &error);
    bool choose(std::size_t reply_index, std::string &error);
    void close();

    bool active() const;
    const std::string &phrase_id() const;
    const std::string &message() const;
    const std::vector<Reply> &replies() const;

private:
    bool enter_phrase(const std::string &phrase_id, std::string &error);
    bool reply_requirements_met(GameJsonRef reply, bool apply, std::string &error);
    bool apply_rewards(GameJsonRef phrase, std::string &error);

    GameResources *resources_;
    GameCore *core_;
    std::string phrase_id_;
    std::string message_;
    std::vector<Reply> replies_;
    bool active_;
};

}
}

#endif
