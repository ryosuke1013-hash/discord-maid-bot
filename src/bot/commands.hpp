#pragma once

#include "../storage/storage.hpp"
#include "hima_voice_manager.hpp"
#include "room_manager.hpp"

#include <dpp/dpp.h>

#include <vector>

std::vector<dpp::slashcommand> build_commands(dpp::snowflake application_id);
void register_command_handlers(dpp::cluster& bot, Storage& storage, RoomManager& rooms, HimaVoiceManager& hima);
