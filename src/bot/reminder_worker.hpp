#pragma once

#include "../storage/storage.hpp"

#include <atomic>
#include <dpp/dpp.h>

void start_reminder_worker(dpp::cluster& bot, Storage& storage, std::atomic_bool& running);
