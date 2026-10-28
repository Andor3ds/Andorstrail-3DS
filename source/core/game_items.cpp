#include "game_items.hpp"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>

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

    const GameCore previous_state = *core;
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
        char chance_text[32];
        if (!game_json_string_copy(game_json_object_get(item, "chance"),
                chance_text, sizeof(chance_text))) {
            error = "invalid drop-list chance";
            success = false;
            break;
        }
        char *chance_end = NULL;
        const long chance = std::strtol(chance_text, &chance_end, 10);
        if (chance_end == chance_text || *chance_end != '\0' || chance < 0 || chance > 100) {
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
        if (game_core_random(core, 100) >= static_cast<std::uint32_t>(chance)) continue;
        const uint64_t range = static_cast<uint64_t>(maximum) - minimum + 1;
        if (range > UINT32_MAX) {
            error = "drop-list quantity range exceeds supported bounds";
            success = false;
            break;
        }
        const int amount = minimum + static_cast<int>(game_core_random(
            core, static_cast<std::uint32_t>(range)
        ));
        if (amount > 0 && !game_core_add_item(core, item_id.c_str(), amount)) {
            error = "drop-list item could not be added: " + item_id;
            success = false;
        }
    }
    if (count == 0 && success) {
        error = "drop list is empty: " + drop_list_id;
        success = false;
    }
    game_json_dispose(&document);
    if (!success) *core = previous_state;
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
    const GameCore previous_state = *core;
    if (!game_core_remove_item(core, "gold", total_price)) {
        error = "not enough gold";
        return false;
    }
    if (!game_core_add_item(core, item_id.c_str(), quantity)) {
        *core = previous_state;
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
