#pragma once

#include "../storage/storage.hpp"

#include <dpp/dpp.h>
#include <mutex>
#include <string>
#include <unordered_map>

class RoomManager {
public:
    explicit RoomManager(Storage& storage);

    void open_room(dpp::cluster& bot, const dpp::slashcommand_t& event);
    void open_room(dpp::cluster& bot, const dpp::button_click_t& event);

private:
    struct RoomInfo {
        dpp::snowflake channel_id{};
        dpp::snowflake guild_id{};
        dpp::snowflake user_id{};
    };

    void remember_room(dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake channel_id);
    void forget_room(dpp::snowflake user_id);
    void create_room(dpp::cluster& bot, dpp::slashcommand_t event, dpp::snowflake user_id, dpp::snowflake guild_id, std::string username, bool edit_response);
    void create_room(dpp::cluster& bot, dpp::button_click_t event, dpp::snowflake user_id, dpp::snowflake guild_id, std::string username, bool edit_response);
    void load_rooms();

    Storage& storage_;
    std::mutex mutex_;
    std::unordered_map<uint64_t, RoomInfo> rooms_;
};
