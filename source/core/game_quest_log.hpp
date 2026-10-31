#ifndef ANDORSTRAIL_GAME_QUEST_LOG_HPP
#define ANDORSTRAIL_GAME_QUEST_LOG_HPP

#include "game_core.h"
#include "game_resources.h"

#include <string>
#include <vector>

namespace andortrail {
namespace quest_log {

struct Entry {
    std::string id;
    std::string name;
    std::string objective;
    int progress;
};

bool active_quests(GameResources *resources, const GameCore *core,
    std::vector<Entry> &entries, std::string &error);

}
}

#endif
