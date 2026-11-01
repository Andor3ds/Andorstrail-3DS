#ifndef ANDORSTRAIL_GAME_ITEMS_HPP
#define ANDORSTRAIL_GAME_ITEMS_HPP

#include "game_core.h"
#include "game_resources.h"

#include <string>

namespace andortrail {
namespace items {

enum ActionType {
    ACTION_NOT_USABLE,
    ACTION_USE,
    ACTION_EQUIP
};

enum InventoryGroup {
    INVENTORY_GROUP_WEAPON,
    INVENTORY_GROUP_ARMOR,
    INVENTORY_GROUP_JEWELRY,
    INVENTORY_GROUP_POTION,
    INVENTORY_GROUP_FOOD,
    INVENTORY_GROUP_QUEST,
    INVENTORY_GROUP_OTHER
};

struct InventoryProperties {
    InventoryGroup group;
    int type_order;
    int rarity_order;
    int market_cost;
};

bool item_inventory_properties(GameResources *resources,
    const std::string &item_id, InventoryProperties &properties,
    std::string &error);
bool item_action(
    GameResources *resources,
    const std::string &item_id,
    ActionType &action,
    GameCoreEquipmentSlot &slot,
    bool &two_handed,
    std::string &error
);
bool equip_item(
    GameResources *resources,
    GameCore *core,
    const std::string &item_id,
    std::string &error
);
bool equip_item(
    GameResources *resources,
    GameCore *core,
    const std::string &item_id,
    GameCoreEquipmentSlot target_slot,
    std::string &error
);
bool unequip_item(
    GameResources *resources,
    GameCore *core,
    GameCoreEquipmentSlot slot,
    std::string &error
);
bool reconcile_equipment_conditions(
    GameResources *resources,
    GameCore *core,
    std::string &error
);
bool item_name(
    GameResources *resources,
    const std::string &item_id,
    std::string &name,
    std::string &error
);
bool item_icon(
    GameResources *resources,
    const std::string &item_id,
    std::string &sheet,
    unsigned int &index,
    std::string &error
);
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
