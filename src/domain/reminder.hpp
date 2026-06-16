#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <cstdint>
#include <string>

struct Reminder {
    uint64_t id{};
    dpp::snowflake user_id{};
    dpp::snowflake channel_id{};
    std::chrono::system_clock::time_point due_at{};
    std::string message;
};
