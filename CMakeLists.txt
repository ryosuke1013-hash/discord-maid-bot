cmake_minimum_required(VERSION 3.20)

project(discord_productivity_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(dpp CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(discord_productivity_bot
    src/bot/commands.cpp
    src/bot/hima_voice_manager.cpp
    src/bot/reminder_worker.cpp
    src/bot/room_manager.cpp
    src/config/env_loader.cpp
    src/main.cpp
    src/storage/storage.cpp
)

target_link_libraries(discord_productivity_bot PRIVATE dpp::dpp SQLite::SQLite3)

if (MSVC)
    target_compile_options(discord_productivity_bot PRIVATE /W4 /permissive-)
else()
    target_compile_options(discord_productivity_bot PRIVATE -Wall -Wextra -Wpedantic)
endif()
