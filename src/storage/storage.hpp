#pragma once

#include "../domain/reminder.hpp"
#include "../domain/todo.hpp"

#include <dpp/dpp.h>
#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct TodoNotificationSetting {
    dpp::snowflake user_id{};
    int64_t interval_minutes{};
    std::chrono::system_clock::time_point next_notify_at{};
};

struct TodoNotificationTarget {
    dpp::snowflake user_id{};
    dpp::snowflake channel_id{};
    int64_t interval_minutes{};
};

struct ManagedRoom {
    dpp::snowflake channel_id{};
    dpp::snowflake guild_id{};
    dpp::snowflake user_id{};
};

class Storage {
public:
    explicit Storage(std::filesystem::path data_dir);
    ~Storage();

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    Reminder add_reminder(dpp::snowflake user_id, dpp::snowflake channel_id, int64_t minutes, std::string message);
    Reminder add_reminder_at(dpp::snowflake user_id, dpp::snowflake channel_id, std::chrono::system_clock::time_point due_at, std::string message);
    std::vector<Reminder> list_reminders(dpp::snowflake user_id) const;
    bool has_pending_reminders(dpp::snowflake user_id) const;
    bool cancel_reminder(dpp::snowflake user_id, uint64_t id);
    std::vector<Reminder> take_due_reminders_for_notification();
    void set_reminder_notification_message(uint64_t id, dpp::snowflake message_id);
    bool acknowledge_reminder(dpp::snowflake user_id, uint64_t id);
    bool acknowledge_reminder_by_message(dpp::snowflake user_id, dpp::snowflake message_id);

    Todo add_todo(dpp::snowflake user_id, std::string task);
    std::optional<Todo> get_todo(dpp::snowflake user_id, uint64_t id) const;
    std::vector<Todo> list_todos(dpp::snowflake user_id) const;
    bool has_open_todos(dpp::snowflake user_id) const;
    bool complete_todo(dpp::snowflake user_id, uint64_t id);
    bool delete_todo(dpp::snowflake user_id, uint64_t id);
    size_t clear_done_todos(dpp::snowflake user_id);
    void set_todo_notification(dpp::snowflake user_id, int64_t interval_minutes);
    std::optional<TodoNotificationSetting> get_todo_notification(dpp::snowflake user_id) const;
    std::vector<TodoNotificationTarget> due_todo_notifications();
    void mark_todo_notification_checked(dpp::snowflake user_id);

    void save_room(dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake channel_id);
    void delete_room(dpp::snowflake user_id);
    std::vector<ManagedRoom> list_rooms() const;

private:
    void open_database();
    void create_schema();
    void migrate_schema();
    void migrate_tsv_if_needed();

    std::filesystem::path data_dir_;
    std::filesystem::path database_file_;
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
};
