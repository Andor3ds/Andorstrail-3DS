#include "game_items.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace andortrail {
namespace items {
namespace {

bool copy_string(GameJsonRef value, std::string &destination, const char *field,
    std::string &error)
{
    char buffer[GAME_CORE_ID_CAPACITY];
    if (!game_json_string_copy(value, buffer, sizeof(buffer))) {
        error = std::string("invalid item data field: ") + field;
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
        error = std::string("invalid item data integer: ") + field;
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

bool parse_drop_chance(const char *text, int &maximum, int &current)
{
    if (text == NULL || text[0] == '\0') return false;
    const char *separator = std::strchr(text, '/');
    char *end = NULL;
    if (separator == NULL) {
        const long parsed = std::strtol(text, &end, 10);
        if (end == text || *end != '\0' || parsed < 0 || parsed > INT_MAX) {
            return false;
        }
        if (parsed == 100) {
            maximum = 1;
            current = 1;
        } else {
            maximum = 100;
            current = static_cast<int>(parsed);
        }
    } else {
        const long parsed_current = std::strtol(text, &end, 10);
        if (end != separator || parsed_current < 0 || parsed_current > INT_MAX) {
            return false;
        }
        const long parsed_maximum = std::strtol(separator + 1, &end, 10);
        if (end == separator + 1 || *end != '\0'
                || parsed_maximum <= 0 || parsed_maximum > INT_MAX) {
            return false;
        }
        maximum = static_cast<int>(parsed_maximum);
        current = static_cast<int>(parsed_current);
    }
    return maximum > 0;
}

bool equipment_slot(const std::string &name, GameCoreEquipmentSlot &slot)
{
    if (name == "weapon") slot = GAME_CORE_EQUIP_WEAPON;
    else if (name == "shield") slot = GAME_CORE_EQUIP_SHIELD;
    else if (name == "head") slot = GAME_CORE_EQUIP_HEAD;
    else if (name == "body") slot = GAME_CORE_EQUIP_BODY;
    else if (name == "hand") slot = GAME_CORE_EQUIP_HAND;
    else if (name == "feet") slot = GAME_CORE_EQUIP_FEET;
    else if (name == "neck") slot = GAME_CORE_EQUIP_NECK;
    else if (name == "leftring") slot = GAME_CORE_EQUIP_LEFT_RING;
    else if (name == "rightring") slot = GAME_CORE_EQUIP_RIGHT_RING;
    else return false;
    return true;
}

bool item_grants_condition(GameResources *resources, const std::string &item_id,
    const char *desired_condition_id, bool immunity, bool &granted,
    std::string &error)
{
    granted = false;
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }

    const GameJsonRef added_conditions = game_json_object_get(
        game_json_object_get(item, "equipEffect"), "addedConditions"
    );
    bool success = true;
    for (std::size_t index = 0;
            success && !granted
                && index < game_json_array_size(added_conditions); ++index) {
        const GameJsonRef effect = game_json_array_get(added_conditions, index);
        char current_condition_id[GAME_CORE_ID_CAPACITY];
        int64_t magnitude = 1;
        int64_t duration = 0;
        const GameJsonRef magnitude_value = game_json_object_get(effect, "magnitude");
        const GameJsonRef duration_value = game_json_object_get(effect, "duration");
        success = game_json_string_copy(game_json_object_get(effect, "condition"),
                current_condition_id, sizeof(current_condition_id))
            && (magnitude_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(magnitude_value)
                || game_json_integer(magnitude_value, &magnitude))
            && (duration_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(duration_value)
                || game_json_integer(duration_value, &duration));
        if (!success) {
            error = "equipped item condition is invalid: " + item_id;
        } else if (std::strcmp(current_condition_id, desired_condition_id) == 0) {
            granted = immunity
                ? magnitude == -99 && duration != 0 : magnitude > 0;
        }
    }
    game_json_dispose(&document);
    return success;
}

bool equipped_item_grants_condition(GameResources *resources, const GameCore *core,
    const char *condition_id, bool immunity, bool &granted, std::string &error)
{
    granted = false;
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS && !granted; ++slot) {
        const char *item_id = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (item_id == NULL) continue;
        if (!item_grants_condition(resources, item_id, condition_id, immunity,
                granted, error)) return false;
    }
    return true;
}

bool apply_equipped_item_conditions(GameResources *resources, GameCore *core,
    const std::string &item_id, std::string &error,
    const std::vector<std::string> *already_applied = NULL,
    const char *only_condition_id = NULL)
{
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    const GameJsonRef added_conditions = game_json_object_get(
        game_json_object_get(item, "equipEffect"), "addedConditions"
    );
    bool success = true;
    for (std::size_t index = 0;
            success && index < game_json_array_size(added_conditions); ++index) {
        const GameJsonRef effect = game_json_array_get(added_conditions, index);
        char condition_id[GAME_CORE_ID_CAPACITY];
        int64_t magnitude = 1;
        int64_t duration = 0;
        const GameJsonRef magnitude_value = game_json_object_get(effect, "magnitude");
        const GameJsonRef duration_value = game_json_object_get(effect, "duration");
        success = game_json_string_copy(game_json_object_get(effect, "condition"),
                condition_id, sizeof(condition_id))
            && (magnitude_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(magnitude_value)
                || game_json_integer(magnitude_value, &magnitude))
            && (duration_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(duration_value)
                || game_json_integer(duration_value, &duration));
        if (!success) {
            error = "equipped item condition is invalid: " + item_id;
            break;
        }
        if (only_condition_id != NULL
                && std::strcmp(only_condition_id, condition_id) != 0) continue;
        if (magnitude > 0 && already_applied != NULL
                && std::find(already_applied->begin(), already_applied->end(),
                    condition_id) != already_applied->end()) continue;
        if (magnitude == -99) {
            success = duration != 0
                ? game_core_add_condition(core, condition_id, -99, 999, false, true)
                : game_core_remove_active_condition(core, condition_id);
            if (!success) {
                error = "equipped item condition could not be applied: " + item_id;
            }
            continue;
        }
        if (magnitude <= 0) continue;

        bool immune = false;
        success = equipped_item_grants_condition(resources, core, condition_id,
            true, immune, error);
        if (!success) break;
        if (immune) continue;

        const GameResourceRecordEntry *condition_record = game_resources_find(
            GAME_RESOURCE_CONDITION, condition_id, NULL
        );
        if (condition_record == NULL) {
            error = std::string("condition record not found: ") + condition_id;
            success = false;
            break;
        }
        GameJsonDocument condition_document;
        GameJsonRef condition;
        if (!game_resources_open_record(resources, condition_record,
                &condition_document, &condition)) {
            error = std::string("condition record could not be read: ") + condition_id;
            success = false;
            break;
        }
        int64_t stacking = 0;
        const GameJsonRef stacking_value = game_json_object_get(condition,
            "isStacking");
        success = stacking_value.node == GAME_JSON_NO_NODE
            || game_json_is_null(stacking_value)
            || game_json_integer(stacking_value, &stacking);
        game_json_dispose(&condition_document);
        if (!success) {
            error = std::string("condition data is invalid: ") + condition_id;
            break;
        }
        success = game_core_add_condition(core, condition_id,
            static_cast<int>(magnitude), 999, stacking != 0, false);
        if (!success) error = std::string("equipped condition could not be applied: ")
            + condition_id;
    }
    game_json_dispose(&document);
    return success;
}

bool remove_equipped_item_conditions(GameResources *resources, GameCore *core,
    const std::string &item_id, std::string &error,
    std::vector<std::string> *reapplied_conditions = NULL)
{
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    const GameJsonRef added_conditions = game_json_object_get(
        game_json_object_get(item, "equipEffect"), "addedConditions"
    );
    bool success = true;
    std::vector<std::string> removed_immunities;
    for (std::size_t index = 0;
            success && index < game_json_array_size(added_conditions); ++index) {
        const GameJsonRef effect = game_json_array_get(added_conditions, index);
        char condition_id[GAME_CORE_ID_CAPACITY];
        int64_t magnitude = 1;
        int64_t duration = 0;
        const GameJsonRef magnitude_value = game_json_object_get(effect, "magnitude");
        const GameJsonRef duration_value = game_json_object_get(effect, "duration");
        success = game_json_string_copy(game_json_object_get(effect, "condition"),
                condition_id, sizeof(condition_id))
            && (magnitude_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(magnitude_value)
                || game_json_integer(magnitude_value, &magnitude));
        success = success && (duration_value.node == GAME_JSON_NO_NODE
            || game_json_is_null(duration_value)
            || game_json_integer(duration_value, &duration));
        if (!success) {
            error = "equipped item condition is invalid: " + item_id;
            break;
        }
        if (magnitude == -99 && duration != 0) {
            bool another_immunity = false;
            success = equipped_item_grants_condition(resources, core,
                condition_id, true, another_immunity, error);
            if (!success || another_immunity) continue;
            if (std::find(removed_immunities.begin(), removed_immunities.end(),
                    condition_id) == removed_immunities.end()) {
                removed_immunities.push_back(condition_id);
            }
            continue;
        }
        if (magnitude <= 0) continue;

        const GameResourceRecordEntry *condition_record = game_resources_find(
            GAME_RESOURCE_CONDITION, condition_id, NULL
        );
        if (condition_record == NULL) {
            error = std::string("condition record not found: ") + condition_id;
            success = false;
            break;
        }
        GameJsonDocument condition_document;
        GameJsonRef condition;
        if (!game_resources_open_record(resources, condition_record,
                &condition_document, &condition)) {
            error = std::string("condition record could not be read: ") + condition_id;
            success = false;
            break;
        }
        int64_t stacking = 0;
        const GameJsonRef stacking_value = game_json_object_get(condition,
            "isStacking");
        success = stacking_value.node == GAME_JSON_NO_NODE
            || game_json_is_null(stacking_value)
            || game_json_integer(stacking_value, &stacking);
        game_json_dispose(&condition_document);
        if (!success) {
            error = std::string("condition data is invalid: ") + condition_id;
            break;
        }
        bool another_condition = false;
        success = equipped_item_grants_condition(resources, core, condition_id,
            false, another_condition, error);
        if (!success || (!stacking && another_condition)) continue;
        for (std::size_t condition_index = 0;
                condition_index < core->condition_count; ++condition_index) {
            GameCoreCondition &active = core->conditions[condition_index];
            if (active.immunity || active.duration != 999
                    || std::strcmp(active.id, condition_id) != 0) continue;
            if (stacking && active.magnitude > magnitude) {
                active.magnitude -= static_cast<int>(magnitude);
            } else {
                core->conditions[condition_index] =
                    core->conditions[--core->condition_count];
            }
            break;
        }
    }
    game_json_dispose(&document);
    for (const std::string &condition_id : removed_immunities) {
        bool still_immune = false;
        if (!equipped_item_grants_condition(resources, core, condition_id.c_str(),
                true, still_immune, error)) return false;
        if (still_immune) continue;
        if (!game_core_remove_condition_immunity(core, condition_id.c_str(), 999)) {
            error = "equipped item immunity could not be removed: " + condition_id;
            return false;
        }
        for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
            const char *equipped = game_core_equipped_item(core,
                static_cast<GameCoreEquipmentSlot>(slot));
            if (equipped == NULL) continue;
            bool grants = false;
            if (!item_grants_condition(resources, equipped, condition_id.c_str(),
                    false, grants, error)) return false;
            if (grants) {
                if (!apply_equipped_item_conditions(resources, core,
                        equipped, error, NULL, condition_id.c_str())) return false;
                if (reapplied_conditions != NULL
                        && std::find(reapplied_conditions->begin(),
                            reapplied_conditions->end(), condition_id)
                            == reapplied_conditions->end()) {
                    reapplied_conditions->push_back(condition_id);
                }
            }
        }
    }
    return success;
}

}

bool item_action(GameResources *resources, const std::string &item_id,
    ActionType &action, GameCoreEquipmentSlot &slot, bool &two_handed,
    std::string &error)
{
    error.clear();
    action = ACTION_NOT_USABLE;
    two_handed = false;
    if (resources == NULL || item_id.empty()) {
        error = "invalid item action configuration";
        return false;
    }
    const GameResourceRecordEntry *item_record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (item_record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument item_document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, item_record, &item_document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    std::string category_id;
    const bool valid_category = copy_string(
        game_json_object_get(item, "category"), category_id, "category", error
    );
    game_json_dispose(&item_document);
    if (!valid_category) return false;

    const GameResourceRecordEntry *category_record = game_resources_find(
        GAME_RESOURCE_ITEM_CATEGORY, category_id.c_str(), NULL
    );
    if (category_record == NULL) {
        error = "item category record not found: " + category_id;
        return false;
    }
    GameJsonDocument category_document;
    GameJsonRef category;
    if (!game_resources_open_record(resources, category_record, &category_document,
            &category)) {
        error = "item category record could not be read: " + category_id;
        return false;
    }
    char action_text[32];
    bool valid = game_json_string_copy(game_json_object_get(category, "actionType"),
        action_text, sizeof(action_text));
    if (valid && std::strcmp(action_text, "use") == 0) {
        action = ACTION_USE;
    } else if (valid && std::strcmp(action_text, "equip") == 0) {
        action = ACTION_EQUIP;
        char slot_text[32];
        std::string slot_name;
        valid = game_json_string_copy(game_json_object_get(category, "inventorySlot"),
                slot_text, sizeof(slot_text));
        if (valid) {
            slot_name.assign(slot_text);
            valid = equipment_slot(slot_name, slot);
        }
        char size_text[32];
        if (valid && slot == GAME_CORE_EQUIP_WEAPON) {
            const GameJsonRef size = game_json_object_get(category, "size");
            if (size.node != GAME_JSON_NO_NODE && !game_json_is_null(size)) {
                valid = game_json_string_copy(size, size_text, sizeof(size_text));
                two_handed = valid && std::strcmp(size_text, "large") == 0;
            }
        }
    }
    game_json_dispose(&category_document);
    if (!valid) error = "item category has invalid action data: " + category_id;
    return valid;
}

bool item_inventory_properties(GameResources *resources,
    const std::string &item_id, InventoryProperties &properties,
    std::string &error)
{
    error.clear();
    if (resources == NULL || item_id.empty()) {
        error = "invalid inventory item configuration";
        return false;
    }

    const GameResourceRecordEntry *item_record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (item_record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument item_document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, item_record, &item_document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }

    std::string category_id;
    bool valid = copy_string(game_json_object_get(item, "category"),
        category_id, "category", error);
    std::string display_type("ordinary");
    const GameJsonRef display_type_value = game_json_object_get(item, "displayType");
    if (valid && display_type_value.node != GAME_JSON_NO_NODE
            && !game_json_is_null(display_type_value)) {
        valid = copy_string(display_type_value, display_type, "displayType", error);
    }

    int market_cost = 0;
    const GameJsonRef market_cost_value = game_json_object_get(item, "baseMarketCost");
    if (valid && market_cost_value.node != GAME_JSON_NO_NODE
            && !game_json_is_null(market_cost_value)) {
        valid = read_integer(market_cost_value, market_cost, "baseMarketCost", error);
    }
    game_json_dispose(&item_document);
    if (!valid) return false;

    const GameResourceRecordEntry *category_record = game_resources_find(
        GAME_RESOURCE_ITEM_CATEGORY, category_id.c_str(), NULL
    );
    if (category_record == NULL) {
        error = "item category record not found: " + category_id;
        return false;
    }
    GameJsonDocument category_document;
    GameJsonRef category;
    if (!game_resources_open_record(resources, category_record,
            &category_document, &category)) {
        error = "item category could not be read: " + category_id;
        return false;
    }

    std::string action_type;
    const GameJsonRef action_value = game_json_object_get(category, "actionType");
    if (action_value.node != GAME_JSON_NO_NODE && !game_json_is_null(action_value)) {
        valid = copy_string(action_value, action_type, "actionType", error);
    }
    std::string inventory_slot;
    std::string size;
    if (valid && action_type == "equip") {
        valid = copy_string(game_json_object_get(category, "inventorySlot"),
            inventory_slot, "inventorySlot", error);
        const GameJsonRef size_value = game_json_object_get(category, "size");
        if (valid && size_value.node != GAME_JSON_NO_NODE
                && !game_json_is_null(size_value)) {
            valid = copy_string(size_value, size, "size", error);
        }
    }
    game_json_dispose(&category_document);
    if (!valid) return false;

    if (display_type == "ordinary") properties.rarity_order = 0;
    else if (display_type == "quest") properties.rarity_order = 1;
    else if (display_type == "rare") properties.rarity_order = 2;
    else if (display_type == "extraordinary") properties.rarity_order = 3;
    else if (display_type == "legendary") properties.rarity_order = 4;
    else {
        error = "item record has an invalid displayType: " + item_id;
        return false;
    }
    properties.market_cost = market_cost;

    if (display_type == "quest") {
        properties.group = INVENTORY_GROUP_QUEST;
        properties.type_order = 400;
    } else if (action_type == "equip") {
        if (inventory_slot == "weapon") {
            properties.group = INVENTORY_GROUP_WEAPON;
            properties.type_order = size == "large" ? 100 : 110;
        } else if (inventory_slot == "shield") {
            properties.group = INVENTORY_GROUP_ARMOR;
            properties.type_order = 200;
        } else if (inventory_slot == "head") {
            properties.group = INVENTORY_GROUP_ARMOR;
            properties.type_order = 210;
        } else if (inventory_slot == "body") {
            properties.group = INVENTORY_GROUP_ARMOR;
            properties.type_order = 220;
        } else if (inventory_slot == "hand") {
            properties.group = INVENTORY_GROUP_ARMOR;
            properties.type_order = 230;
        } else if (inventory_slot == "feet") {
            properties.group = INVENTORY_GROUP_ARMOR;
            properties.type_order = 240;
        } else if (inventory_slot == "neck") {
            properties.group = INVENTORY_GROUP_JEWELRY;
            properties.type_order = 250;
        } else if (inventory_slot == "leftring"
                || inventory_slot == "rightring") {
            properties.group = INVENTORY_GROUP_JEWELRY;
            properties.type_order = 260;
        } else {
            error = "item category has an invalid inventorySlot: " + category_id;
            return false;
        }
    } else if (action_type == "use") {
        if (category_id == "pot" || category_id == "healing") {
            properties.group = INVENTORY_GROUP_POTION;
            properties.type_order = 300;
        } else {
            properties.group = INVENTORY_GROUP_FOOD;
            properties.type_order = 310;
        }
    } else if (action_type.empty()) {
        properties.group = INVENTORY_GROUP_OTHER;
        properties.type_order = 500;
    } else {
        error = "item category has an invalid actionType: " + category_id;
        return false;
    }
    return true;
}

bool equip_item(GameResources *resources, GameCore *core,
    const std::string &item_id, std::string &error)
{
    ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    if (!item_action(resources, item_id, action, slot, two_handed, error)) {
        return false;
    }
    if (action != ACTION_EQUIP) {
        error = "item cannot be equipped: " + item_id;
        return false;
    }
    const char *left_ring = game_core_equipped_item(core, GAME_CORE_EQUIP_LEFT_RING);
    if (slot == GAME_CORE_EQUIP_LEFT_RING
            && left_ring != NULL && left_ring[0] != '\0') {
        slot = GAME_CORE_EQUIP_RIGHT_RING;
    }
    if (slot == GAME_CORE_EQUIP_RIGHT_RING
            && (left_ring == NULL || left_ring[0] == '\0')) {
        slot = GAME_CORE_EQUIP_LEFT_RING;
    }
    return equip_item(resources, core, item_id, slot, error);
}

bool equip_item(GameResources *resources, GameCore *core,
    const std::string &item_id, GameCoreEquipmentSlot target_slot,
    std::string &error)
{
    error.clear();
    if (core == NULL || target_slot < GAME_CORE_EQUIP_WEAPON
            || target_slot >= GAME_CORE_EQUIPMENT_SLOTS) {
        error = "invalid equipment configuration";
        return false;
    }
    ActionType action;
    GameCoreEquipmentSlot slot = GAME_CORE_EQUIP_WEAPON;
    bool two_handed = false;
    if (!item_action(resources, item_id, action, slot, two_handed, error)) return false;
    if (action != ACTION_EQUIP) {
        error = "item cannot be equipped: " + item_id;
        return false;
    }
    if (target_slot != slot) {
        const bool ring_slot = (slot == GAME_CORE_EQUIP_LEFT_RING
                && target_slot == GAME_CORE_EQUIP_RIGHT_RING)
            || (slot == GAME_CORE_EQUIP_RIGHT_RING
                && target_slot == GAME_CORE_EQUIP_LEFT_RING);
        const bool weapon_shield_slot = slot == GAME_CORE_EQUIP_WEAPON
            && target_slot == GAME_CORE_EQUIP_SHIELD && !two_handed;
        if (!ring_slot && !weapon_shield_slot) {
            error = "item cannot be equipped in the selected slot: " + item_id;
            return false;
        }
        slot = target_slot;
    }
    if (game_core_inventory_count(core, item_id.c_str()) <= 0) {
        error = "item is not in inventory: " + item_id;
        return false;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "equipment state could not be allocated";
        return false;
    }
    *previous_state = *core;
    bool success = game_core_remove_item(core, item_id.c_str(), 1);
    std::vector<std::string> removed_items;
    const char *removed = success ? game_core_equipped_item(core, slot) : NULL;
    char replaced[GAME_CORE_ID_CAPACITY] = {};
    if (removed != NULL && removed[0] != '\0') {
        std::strncpy(replaced, removed, sizeof(replaced) - 1);
        success = game_core_set_equipped_item(core, slot, "")
            && game_core_add_item(core, replaced, 1);
        if (success) removed_items.push_back(replaced);
    }
    if (success && two_handed) {
        const char *shield = game_core_equipped_item(core, GAME_CORE_EQUIP_SHIELD);
        if (shield != NULL && shield[0] != '\0') {
            char removed_shield[GAME_CORE_ID_CAPACITY];
            std::strncpy(removed_shield, shield, sizeof(removed_shield) - 1);
            removed_shield[sizeof(removed_shield) - 1] = '\0';
            success = game_core_set_equipped_item(core, GAME_CORE_EQUIP_SHIELD, "")
                && game_core_add_item(core, removed_shield, 1);
            if (success) removed_items.push_back(removed_shield);
        }
    } else if (success && slot == GAME_CORE_EQUIP_SHIELD) {
        const char *weapon = game_core_equipped_item(core, GAME_CORE_EQUIP_WEAPON);
        if (weapon != NULL && weapon[0] != '\0') {
            ActionType weapon_action;
            GameCoreEquipmentSlot weapon_slot = GAME_CORE_EQUIP_WEAPON;
            bool weapon_two_handed = false;
            if (!item_action(resources, weapon, weapon_action, weapon_slot,
                    weapon_two_handed, error)) {
                success = false;
            } else if (weapon_two_handed) {
                char removed_weapon[GAME_CORE_ID_CAPACITY];
                std::strncpy(removed_weapon, weapon, sizeof(removed_weapon) - 1);
                removed_weapon[sizeof(removed_weapon) - 1] = '\0';
                success = game_core_set_equipped_item(core,
                    GAME_CORE_EQUIP_WEAPON, "")
                    && game_core_add_item(core, removed_weapon, 1);
                if (success) removed_items.push_back(removed_weapon);
            }
        }
    }
    if (success) {
        success = game_core_set_equipped_item(core, slot, item_id.c_str());
    }
    std::vector<std::string> reapplied_conditions;
    for (const std::string &removed_item : removed_items) {
        if (success && !remove_equipped_item_conditions(resources, core,
                removed_item, error, &reapplied_conditions)) success = false;
    }
    if (success) {
        success = apply_equipped_item_conditions(resources, core, item_id,
            error, &reapplied_conditions);
    }
    if (!success) {
        *core = *previous_state;
        if (error.empty()) error = "equipment change could not be applied";
    }
    return success;
}

bool unequip_item(GameResources *resources, GameCore *core,
    GameCoreEquipmentSlot slot, std::string &error)
{
    error.clear();
    if (resources == NULL || core == NULL || slot < GAME_CORE_EQUIP_WEAPON
            || slot >= GAME_CORE_EQUIPMENT_SLOTS) {
        error = "invalid equipment configuration";
        return false;
    }
    const char *item_id = game_core_equipped_item(core, slot);
    if (item_id == NULL || item_id[0] == '\0') {
        error = "equipment slot is empty";
        return false;
    }
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "equipment state could not be allocated";
        return false;
    }
    *previous_state = *core;
    char removed[GAME_CORE_ID_CAPACITY];
    std::strncpy(removed, item_id, sizeof(removed) - 1);
    removed[sizeof(removed) - 1] = '\0';
    if (!game_core_set_equipped_item(core, slot, "")
            || !game_core_add_item(core, removed, 1)
            || !remove_equipped_item_conditions(resources, core, removed, error)) {
        *core = *previous_state;
        if (error.empty()) error = "equipment could not be returned to inventory";
        return false;
    }
    return true;
}

bool reconcile_equipment_conditions(GameResources *resources, GameCore *core,
    std::string &error)
{
    error.clear();
    if (resources == NULL || core == NULL
            || core->condition_count > GAME_CORE_MAX_CONDITIONS) {
        error = "invalid equipment-condition configuration";
        return false;
    }

    struct ConditionGrant {
        std::string id;
        int magnitude;
        bool stacking;
    };
    std::vector<ConditionGrant> grants;
    std::vector<std::string> immunities;
    std::vector<std::string> removals;
    for (int slot = 0; slot < GAME_CORE_EQUIPMENT_SLOTS; ++slot) {
        const char *equipped = game_core_equipped_item(core,
            static_cast<GameCoreEquipmentSlot>(slot));
        if (equipped == NULL || equipped[0] == '\0') continue;
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_ITEM, equipped, NULL
        );
        if (record == NULL) {
            error = std::string("equipped item record not found: ") + equipped;
            return false;
        }
        GameJsonDocument document;
        GameJsonRef item;
        if (!game_resources_open_record(resources, record, &document, &item)) {
            error = std::string("equipped item record could not be read: ")
                + equipped;
            return false;
        }
        const GameJsonRef added_conditions = game_json_object_get(
            game_json_object_get(item, "equipEffect"), "addedConditions"
        );
        bool success = true;
        for (std::size_t index = 0;
                success && index < game_json_array_size(added_conditions); ++index) {
            const GameJsonRef effect = game_json_array_get(added_conditions, index);
            char condition_id[GAME_CORE_ID_CAPACITY];
            int64_t magnitude = 1;
            int64_t duration = 0;
            const GameJsonRef magnitude_value = game_json_object_get(effect,
                "magnitude");
            const GameJsonRef duration_value = game_json_object_get(effect,
                "duration");
            success = game_json_string_copy(game_json_object_get(effect,
                    "condition"), condition_id, sizeof(condition_id))
                && (magnitude_value.node == GAME_JSON_NO_NODE
                    || game_json_is_null(magnitude_value)
                    || game_json_integer(magnitude_value, &magnitude));
            success = success && (duration_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(duration_value)
                || game_json_integer(duration_value, &duration));
            if (!success) {
                error = std::string("equipped item condition is invalid: ")
                    + equipped;
                break;
            }
            if (magnitude == -99 && duration != 0) {
                if (std::find(immunities.begin(), immunities.end(), condition_id)
                        == immunities.end()) {
                    immunities.push_back(condition_id);
                }
                continue;
            }
            if (magnitude == -99 && duration == 0) {
                if (std::find(removals.begin(), removals.end(), condition_id)
                        == removals.end()) {
                    removals.push_back(condition_id);
                }
                continue;
            }
            if (magnitude <= 0) continue;

            const GameResourceRecordEntry *condition_record = game_resources_find(
                GAME_RESOURCE_CONDITION, condition_id, NULL
            );
            if (condition_record == NULL) {
                error = std::string("condition record not found: ") + condition_id;
                success = false;
                break;
            }
            GameJsonDocument condition_document;
            GameJsonRef condition;
            if (!game_resources_open_record(resources, condition_record,
                    &condition_document, &condition)) {
                error = std::string("condition record could not be read: ")
                    + condition_id;
                success = false;
                break;
            }
            int64_t stacking = 0;
            const GameJsonRef stacking_value = game_json_object_get(condition,
                "isStacking");
            success = stacking_value.node == GAME_JSON_NO_NODE
                || game_json_is_null(stacking_value)
                || game_json_integer(stacking_value, &stacking);
            game_json_dispose(&condition_document);
            if (!success) {
                error = std::string("condition data is invalid: ") + condition_id;
                break;
            }

            std::size_t grant_index = 0;
            while (grant_index < grants.size()
                    && grants[grant_index].id != condition_id) ++grant_index;
            if (grant_index == grants.size()) {
                ConditionGrant grant = {condition_id,
                    static_cast<int>(magnitude), stacking != 0};
                grants.push_back(grant);
            } else if (grants[grant_index].stacking != (stacking != 0)) {
                error = std::string("condition stacking data is inconsistent: ")
                    + condition_id;
                success = false;
            } else if (stacking != 0) {
                if (magnitude > INT_MAX - grants[grant_index].magnitude) {
                    error = std::string("equipped condition magnitude is too large: ")
                        + condition_id;
                    success = false;
                } else {
                    grants[grant_index].magnitude += static_cast<int>(magnitude);
                }
            } else {
                grants[grant_index].magnitude = std::max(
                    grants[grant_index].magnitude, static_cast<int>(magnitude));
            }
        }
        game_json_dispose(&document);
        if (!success) return false;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "equipment-condition state could not be allocated";
        return false;
    }
    *previous_state = *core;
    for (const std::string &condition_id : immunities) {
        if (!game_core_add_condition(core, condition_id.c_str(), -99, 999,
                false, true)) {
            error = "equipped item immunity could not be applied: " + condition_id;
            *core = *previous_state;
            return false;
        }
    }
    for (const std::string &condition_id : removals) {
        if (!game_core_remove_active_condition(core, condition_id.c_str())) {
            error = "equipped item condition could not be removed: " + condition_id;
            *core = *previous_state;
            return false;
        }
    }

    for (const ConditionGrant &grant : grants) {
        if (std::find(immunities.begin(), immunities.end(), grant.id)
                != immunities.end()) continue;
        int permanent_magnitude = 0;
        for (std::size_t index = 0; index < core->condition_count; ++index) {
            const GameCoreCondition &active = core->conditions[index];
            if (!active.immunity && active.duration == 999
                    && grant.id == active.id) {
                if (grant.stacking) {
                    if (active.magnitude > INT_MAX - permanent_magnitude) {
                        permanent_magnitude = INT_MAX;
                    } else {
                        permanent_magnitude += active.magnitude;
                    }
                } else {
                    permanent_magnitude = std::max(permanent_magnitude,
                        active.magnitude);
                }
            }
        }
        if (permanent_magnitude >= grant.magnitude) continue;
        const int missing_magnitude = grant.magnitude - permanent_magnitude;
        if (!game_core_add_condition(core, grant.id.c_str(), missing_magnitude,
                999, grant.stacking, false)) {
            error = "equipped item condition could not be restored: " + grant.id;
            *core = *previous_state;
            return false;
        }
    }
    return true;
}

bool item_name(
    GameResources *resources,
    const std::string &item_id,
    std::string &name,
    std::string &error
)
{
    error.clear();
    name.clear();
    if (resources == NULL || item_id.empty()) {
        error = "invalid item-name configuration";
        return false;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    char source_name[128];
    const bool valid_name = game_json_string_copy(
        game_json_object_get(item, "name"), source_name, sizeof(source_name)
    );
    if (valid_name) name.assign(source_name);
    game_json_dispose(&document);
    if (!valid_name) error = "item record has an invalid name: " + item_id;
    return valid_name;
}

bool item_icon(
    GameResources *resources,
    const std::string &item_id,
    std::string &sheet,
    unsigned int &index,
    std::string &error
)
{
    error.clear();
    sheet.clear();
    index = 0;
    if (resources == NULL || item_id.empty()) {
        error = "invalid item-icon configuration";
        return false;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "item record not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "item record could not be read: " + item_id;
        return false;
    }
    char icon_id[GAME_CORE_ID_CAPACITY];
    bool valid = game_json_string_copy(
        game_json_object_get(item, "iconID"), icon_id, sizeof(icon_id)
    );
    if (valid) {
        char *separator = std::strchr(icon_id, ':');
        if (separator == NULL || separator == icon_id || separator[1] == '\0') {
            valid = false;
        } else {
            *separator = '\0';
            char *end = NULL;
            const unsigned long parsed = std::strtoul(separator + 1, &end, 10);
            if (end == separator + 1 || *end != '\0' || parsed > UINT_MAX) {
                valid = false;
            } else {
                sheet.assign(icon_id);
                index = static_cast<unsigned int>(parsed);
            }
        }
    }
    game_json_dispose(&document);
    if (!valid) error = "item record has an invalid iconID: " + item_id;
    return valid;
}

bool apply_drop_list(
    GameResources *resources,
    GameCore *core,
    const std::string &drop_list_id,
    std::string &error
)
{
    error.clear();
    if (resources == NULL || core == NULL || drop_list_id.empty()) {
        error = "invalid drop-list configuration";
        return false;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_DROP_LIST, drop_list_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "drop list not found: " + drop_list_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef drop_list;
    if (!game_resources_open_record(resources, record, &document, &drop_list)) {
        error = "drop list could not be read: " + drop_list_id;
        return false;
    }

    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        game_json_dispose(&document);
        error = "drop-list state could not be allocated";
        return false;
    }
    *previous_state = *core;
    bool success = true;
    const GameJsonRef items = game_json_object_get(drop_list, "items");
    const std::size_t count = game_json_array_size(items);
    for (std::size_t index = 0; success && index < count; ++index) {
        const GameJsonRef item = game_json_array_get(items, index);
        std::string item_id;
        if (!copy_string(game_json_object_get(item, "itemID"), item_id,
                "itemID", error)) {
            success = false;
            break;
        }
        if (!item_id.empty() && item_id[0] == '#') {
            const GameResourceRecordEntry *filter_record = game_resources_find(
                GAME_RESOURCE_ITEM_FILTER, item_id.c_str() + 1, NULL
            );
            if (filter_record == NULL) {
                error = "drop-list item filter not found: " + item_id;
                success = false;
                break;
            }
            GameJsonDocument filter_document;
            GameJsonRef filter;
            if (!game_resources_open_record(resources, filter_record,
                    &filter_document, &filter)) {
                error = "drop-list item filter could not be read: " + item_id;
                success = false;
                break;
            }
            const GameJsonRef include = game_json_object_get(filter, "include");
            const std::size_t include_count = game_json_array_size(include);
            if (include_count == 0) {
                error = "drop-list item filter is empty: " + item_id;
                success = false;
            } else {
                const GameJsonRef selected = game_json_array_get(include,
                    game_core_random(core, static_cast<std::uint32_t>(include_count)));
                if (!copy_string(selected, item_id, "item filter entry", error)) {
                    success = false;
                }
            }
            game_json_dispose(&filter_document);
            if (!success) break;
        }
        char chance_text[32];
        if (!game_json_string_copy(game_json_object_get(item, "chance"),
                chance_text, sizeof(chance_text))) {
            error = "invalid drop-list chance";
            success = false;
            break;
        }
        int chance_maximum = 0;
        int chance_current = 0;
        if (!parse_drop_chance(chance_text, chance_maximum, chance_current)) {
            error = "invalid drop-list chance";
            success = false;
            break;
        }
        int minimum = 0;
        int maximum = 0;
        const GameJsonRef quantity = game_json_object_get(item, "quantity");
        if (!read_integer(game_json_object_get(quantity, "min"), minimum,
                    "quantity minimum", error)
                || !read_integer(game_json_object_get(quantity, "max"), maximum,
                    "quantity maximum", error)
                || maximum < minimum) {
            success = false;
            break;
        }

        int skill_level = 0;
        int skill_bonus_percent = 0;
        if (item_id == "gold") {
            skill_level = game_core_skill_level(core, "coinfinder");
            skill_bonus_percent = 30;
        } else {
            const GameResourceRecordEntry *item_record = game_resources_find(
                GAME_RESOURCE_ITEM, item_id.c_str(), NULL
            );
            if (item_record == NULL) {
                error = "drop-list item not found: " + item_id;
                success = false;
                break;
            }
            GameJsonDocument item_document;
            GameJsonRef item_record_json;
            if (!game_resources_open_record(resources, item_record,
                    &item_document, &item_record_json)) {
                error = "drop-list item could not be read: " + item_id;
                success = false;
                break;
            }
            char display_type[32] = "ordinary";
            const GameJsonRef display_type_value = game_json_object_get(
                item_record_json, "displayType");
            const bool valid_display_type = display_type_value.node
                    == GAME_JSON_NO_NODE || game_json_is_null(display_type_value)
                || game_json_string_copy(display_type_value, display_type,
                    sizeof(display_type));
            game_json_dispose(&item_document);
            if (!valid_display_type) {
                error = "drop-list item has an invalid displayType: " + item_id;
                success = false;
                break;
            }
            if (std::strcmp(display_type, "ordinary") != 0) {
                skill_level = game_core_skill_level(core, "magicfinder");
                skill_bonus_percent = 50;
            }
        }

        const std::int64_t chance_total =
            static_cast<std::int64_t>(chance_maximum) * 100;
        if (chance_total > UINT32_MAX) {
            error = "drop-list chance range exceeds supported bounds";
            success = false;
            break;
        }
        std::int64_t chance_bias = 0;
        if (skill_level > 0 && skill_bonus_percent > 0) {
            const std::int64_t unit = static_cast<std::int64_t>(chance_current)
                * skill_bonus_percent;
            if (unit > INT64_MAX / skill_level) {
                chance_bias = INT64_MAX;
            } else {
                chance_bias = unit * skill_level;
            }
        }
        const std::int64_t base_probability =
            static_cast<std::int64_t>(chance_current) * 100;
        const std::int64_t biased_probability =
            chance_bias > INT64_MAX - base_probability
                ? INT64_MAX : base_probability + chance_bias;
        const std::int64_t chance_threshold = std::min<std::int64_t>(
            chance_total, biased_probability);
        const std::uint32_t chance_roll = game_core_random(core,
            static_cast<std::uint32_t>(chance_total));
        if (chance_roll >= static_cast<std::uint32_t>(chance_threshold)) continue;

        std::int64_t quantity_minimum = static_cast<std::int64_t>(minimum) * 100;
        const std::int64_t quantity_maximum =
            (static_cast<std::int64_t>(maximum) + 1) * 100 - 1;
        if (item_id == "gold" && skill_level > 0) {
            const std::int64_t quantity_cap = quantity_maximum - quantity_minimum;
            const std::int64_t per_level = static_cast<std::int64_t>(minimum) * 50;
            const std::int64_t quantity_bias = per_level > 0
                    && skill_level > quantity_cap / per_level
                ? quantity_cap : std::min<std::int64_t>(quantity_cap,
                    per_level * skill_level);
            quantity_minimum += quantity_bias;
        }
        std::int64_t rolled_quantity = quantity_maximum;
        if (quantity_maximum > quantity_minimum) {
            const std::int64_t quantity_range =
                quantity_maximum - quantity_minimum + 1;
            if (quantity_range > UINT32_MAX) {
                error = "drop-list quantity range exceeds supported bounds";
                success = false;
                break;
            }
            rolled_quantity = quantity_minimum
                + game_core_random(core,
                    static_cast<std::uint32_t>(quantity_range));
        }
        const std::int64_t amount = rolled_quantity / 100;
        if (amount > INT_MAX) {
            error = "drop-list item quantity exceeds supported bounds";
            success = false;
            break;
        }
        if (amount > 0 && !game_core_add_item(core, item_id.c_str(),
                static_cast<int>(amount))) {
            error = "drop-list item could not be added: " + item_id;
            success = false;
        }
    }
    if (count == 0 && success) {
        error = "drop list is empty: " + drop_list_id;
        success = false;
    }
    game_json_dispose(&document);
    if (!success) *core = *previous_state;
    return success;
}

bool buy_item(
    GameResources *resources,
    GameCore *core,
    const std::string &item_id,
    int quantity,
    std::string &error
)
{
    error.clear();
    if (resources == NULL || core == NULL || item_id.empty() || quantity <= 0) {
        error = "invalid shop purchase";
        return false;
    }
    int unit_price = 0;
    if (!item_price(resources, item_id, unit_price, error)) return false;
    if (unit_price <= 0 || quantity > INT_MAX / unit_price) {
        error = "shop purchase price is invalid or too large";
        return false;
    }

    const int total_price = unit_price * quantity;
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        error = "purchase state could not be allocated";
        return false;
    }
    *previous_state = *core;
    if (!game_core_remove_item(core, "gold", total_price)) {
        error = "not enough gold";
        return false;
    }
    if (!game_core_add_item(core, item_id.c_str(), quantity)) {
        *core = *previous_state;
        error = "purchased item could not be added to inventory";
        return false;
    }
    return true;
}

bool item_price(
    GameResources *resources,
    const std::string &item_id,
    int &price,
    std::string &error
)
{
    error.clear();
    if (resources == NULL || item_id.empty()) {
        error = "invalid shop item";
        return false;
    }
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_ITEM, item_id.c_str(), NULL
    );
    if (record == NULL) {
        error = "shop item not found: " + item_id;
        return false;
    }
    GameJsonDocument document;
    GameJsonRef item;
    if (!game_resources_open_record(resources, record, &document, &item)) {
        error = "shop item could not be read: " + item_id;
        return false;
    }
    const bool valid_price = read_integer(
        game_json_object_get(item, "baseMarketCost"), price, "baseMarketCost", error
    );
    game_json_dispose(&document);
    return valid_price;
}

}
}
