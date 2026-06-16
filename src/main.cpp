#include "bot/commands.hpp"
#include "bot/hima_voice_manager.hpp"
#include "bot/reminder_worker.hpp"
#include "bot/room_manager.hpp"
#include "config/env_loader.hpp"
#include "storage/storage.hpp"

#include <dpp/dpp.h>

#include <atomic>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return std::nullopt;
    }
    return std::string(value);
}

bool has_arg(int argc, char** argv, const std::string& arg) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == arg) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    load_env_file(".env");
#ifdef _WIN32
    _putenv_s("TZ", "JST-9");
    _tzset();
#else
    setenv("TZ", "Asia/Tokyo", 1);
    tzset();
#endif

    const auto token = get_env("DISCORD_BOT_TOKEN");
    if (!token) {
        std::cerr << "DISCORD_BOT_TOKEN is not set.\n";
        return 1;
    }

    const bool should_register_commands = has_arg(argc, argv, "--register-commands");
    const auto guild_id_env = get_env("DISCORD_GUILD_ID");
    const dpp::snowflake guild_id = guild_id_env ? dpp::snowflake{std::stoull(*guild_id_env)} : dpp::snowflake{};

    Storage storage("data");
    RoomManager rooms(storage);
    HimaVoiceManager hima;
    std::atomic_bool running{true};
    dpp::cluster bot(*token,
        dpp::i_default_intents |
        dpp::i_guild_voice_states |
        dpp::i_guild_messages |
        dpp::i_direct_messages |
        dpp::i_message_content |
        dpp::i_guild_message_reactions |
        dpp::i_direct_message_reactions);

    bot.on_log(dpp::utility::cout_logger());
    register_command_handlers(bot, storage, rooms, hima);
    bot.on_voice_state_update([&hima](const dpp::voice_state_update_t& event) {
        hima.handle_voice_state(event);
    });

    bot.on_ready([&bot, should_register_commands, guild_id](const dpp::ready_t&) {
        if (!should_register_commands || !dpp::run_once<struct register_commands_once>()) {
            return;
        }

        const auto commands = build_commands(bot.me.id);
        if (guild_id != 0) {
            bot.guild_bulk_command_create(commands, guild_id);
            std::cout << "Registered guild commands for guild " << static_cast<uint64_t>(guild_id) << "." << std::endl;
        } else {
            bot.global_bulk_command_create(commands);
            std::cout << "Registered global commands." << std::endl;
        }
    });

    start_reminder_worker(bot, storage, running);
    hima.start_cleanup_worker(bot, running);
    bot.start(dpp::st_wait);
    running.store(false);

    return 0;
}
