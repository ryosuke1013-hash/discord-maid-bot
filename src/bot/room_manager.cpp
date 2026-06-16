#include "room_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace {

uint64_t slash_user_id(const dpp::slashcommand_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t button_user_id(const dpp::button_click_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

std::string sanitize_channel_name(const std::string& value) {
    std::string out;
    for (char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) {
            continue;
        }
        if (c < 0x80 && std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == '-' || c == '_') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '-';
        } else {
            out += ch;
        }
    }

    out.erase(std::unique(out.begin(), out.end(), [](char a, char b) { return a == '-' && b == '-'; }), out.end());
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) {
        return "master";
    }
    return out.substr(0, 70);
}

std::string room_name_for(const std::string& username) {
    return sanitize_channel_name(username) + "の部屋";
}

std::string welcome_message_for(const std::string& username) {
    return "こちらが" + username + "様の専用部屋です。";
}

void fill_room_permissions(dpp::channel& room, dpp::snowflake guild_id, dpp::snowflake user_id, dpp::snowflake bot_id) {
    room.add_permission_overwrite(guild_id, dpp::ot_role, 0, dpp::p_view_channel);
    room.add_permission_overwrite(user_id, dpp::ot_member, dpp::p_view_channel | dpp::p_send_messages | dpp::p_read_message_history, 0);
    room.add_permission_overwrite(bot_id, dpp::ot_member, dpp::p_view_channel | dpp::p_send_messages | dpp::p_read_message_history | dpp::p_manage_channels, 0);
}

} // namespace

RoomManager::RoomManager(Storage& storage) : storage_(storage) {
    load_rooms();
}

void RoomManager::remember_room(dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake channel_id) {
    std::lock_guard lock(mutex_);
    rooms_[static_cast<uint64_t>(user_id)] = RoomInfo{channel_id, guild_id, user_id};
    storage_.save_room(user_id, guild_id, channel_id);
}

void RoomManager::forget_room(dpp::snowflake user_id) {
    std::lock_guard lock(mutex_);
    rooms_.erase(static_cast<uint64_t>(user_id));
    storage_.delete_room(user_id);
}

void RoomManager::load_rooms() {
    std::lock_guard lock(mutex_);
    for (const ManagedRoom& room : storage_.list_rooms()) {
        rooms_[static_cast<uint64_t>(room.user_id)] = RoomInfo{room.channel_id, room.guild_id, room.user_id};
    }
}

void RoomManager::create_room(dpp::cluster& bot, dpp::slashcommand_t event, dpp::snowflake user_id, dpp::snowflake guild_id, std::string username, bool edit_response) {
    dpp::channel room;
    room.guild_id = guild_id;
    room.name = room_name_for(username);
    room.set_type(dpp::CHANNEL_TEXT);
    fill_room_permissions(room, guild_id, user_id, bot.me.id);

    if (edit_response) {
        event.edit_response("作成します。");
    } else {
        event.reply("作成します。");
    }

    bot.channel_create(room, [this, user_id, guild_id, username, &bot](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            return;
        }
        const dpp::channel created = callback.get<dpp::channel>();
        remember_room(user_id, guild_id, created.id);
        bot.message_create(dpp::message(created.id, welcome_message_for(username)));
    });
}

void RoomManager::create_room(dpp::cluster& bot, dpp::button_click_t event, dpp::snowflake user_id, dpp::snowflake guild_id, std::string username, bool edit_response) {
    dpp::channel room;
    room.guild_id = guild_id;
    room.name = room_name_for(username);
    room.set_type(dpp::CHANNEL_TEXT);
    fill_room_permissions(room, guild_id, user_id, bot.me.id);

    if (edit_response) {
        event.edit_response("作成します。");
    } else {
        event.reply("作成します。");
    }

    bot.channel_create(room, [this, user_id, guild_id, username, &bot](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            return;
        }
        const dpp::channel created = callback.get<dpp::channel>();
        remember_room(user_id, guild_id, created.id);
        bot.message_create(dpp::message(created.id, welcome_message_for(username)));
    });
}

void RoomManager::open_room(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    const dpp::snowflake user_id{slash_user_id(event)};
    const dpp::snowflake guild_id{event.command.guild_id};
    const std::string username = event.command.usr.username;
    if (guild_id == 0) {
        event.reply("専用部屋はサーバー内でのみ作成できます。");
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto it = rooms_.find(static_cast<uint64_t>(user_id));
        if (it != rooms_.end()) {
            const dpp::snowflake existing_channel_id = it->second.channel_id;
            event.thinking(true);
            bot.channel_get(existing_channel_id, [this, &bot, event, user_id, guild_id, username](const dpp::confirmation_callback_t& callback) {
                if (!callback.is_error()) {
                    event.edit_response(welcome_message_for(username));
                    return;
                }

                forget_room(user_id);
                create_room(bot, event, user_id, guild_id, username, true);
            });
            return;
        }
    }

    create_room(bot, event, user_id, guild_id, username, false);
}

void RoomManager::open_room(dpp::cluster& bot, const dpp::button_click_t& event) {
    const dpp::snowflake user_id{button_user_id(event)};
    const dpp::snowflake guild_id{event.command.guild_id};
    const std::string username = event.command.usr.username;
    if (guild_id == 0) {
        event.reply("専用部屋はサーバー内でのみ作成できます。");
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto it = rooms_.find(static_cast<uint64_t>(user_id));
        if (it != rooms_.end()) {
            const dpp::snowflake existing_channel_id = it->second.channel_id;
            event.thinking(true);
            bot.channel_get(existing_channel_id, [this, &bot, event, user_id, guild_id, username](const dpp::confirmation_callback_t& callback) {
                if (!callback.is_error()) {
                    event.edit_response(welcome_message_for(username));
                    return;
                }

                forget_room(user_id);
                create_room(bot, event, user_id, guild_id, username, true);
            });
            return;
        }
    }

    create_room(bot, event, user_id, guild_id, username, false);
}
