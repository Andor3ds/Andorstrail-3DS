#ifndef ANDORSTRAIL_GAME_ITEMS_HPP
#define ANDORSTRAIL_GAME_ITEMS_HPP

#include "game_core.h"
#include "game_resources.h"

#include <string>

namespace andortrail {
namespace items {

bool apply_drop_list(
    GameResources *resources,
    GameCore *core,
    const std::string &drop_list_id,
    std::string &error
);
bool item_price(
    GameResources *resources,
    const std::string &item_id,
    int &price,
    std::string &error
);
bool buy_item(
    GameResources *resources,
    GameCore *core,
    const std::string &item_id,
    int quantity,
    std::string &error
);

}
}

#endif
