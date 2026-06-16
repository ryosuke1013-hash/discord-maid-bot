#pragma once

#include <dpp/dpp.h>

#include <cstdint>
#include <string>

struct Todo {
    uint64_t id{};
    dpp::snowflake user_id{};
    bool done{};
    std::string task;
};
