#include "storage.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

struct Statement {
    sqlite3_stmt* stmt{};

    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }

    ~Statement() {
        sqlite3_finalize(stmt);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

void exec_sql(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

int64_t to_unix_seconds(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_unix_seconds(int64_t seconds) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

std::string unescape_field(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            const char next = input[++i];
            switch (next) {
                case 't': out += '\t'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case '\\': out += '\\'; break;
                default:
                    out += '\\';
                    out += next;
                    break;
            }
        } else {
            out += input[i];
        }
    }
    return out;
}

bool table_empty(sqlite3* db, const char* table) {
    const std::string sql = std::string("SELECT COUNT(*) FROM ") + table;
    Statement stmt(db, sql.c_str());
    if (sqlite3_step(stmt.stmt) != SQLITE_ROW) {
        return true;
    }
    return sqlite3_column_int64(stmt.stmt, 0) == 0;
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    Statement stmt(db, sql.c_str());
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
        if (name && column == std::string{name}) {
            return true;
        }
    }
    return false;
}

} // namespace

Storage::Storage(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir)),
      database_file_(data_dir_ / "bot.sqlite") {
    std::filesystem::create_directories(data_dir_);
    open_database();
    create_schema();
    migrate_schema();
    migrate_tsv_if_needed();
}

Storage::~Storage() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Storage::open_database() {
    if (sqlite3_open(database_file_.string().c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    exec_sql(db_, "PRAGMA foreign_keys = ON;");
}

void Storage::create_schema() {
    exec_sql(db_, R"SQL(
        CREATE TABLE IF NOT EXISTS todos (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            done INTEGER NOT NULL DEFAULT 0,
            task TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS reminders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            channel_id INTEGER NOT NULL,
            due_at INTEGER NOT NULL,
            next_notify_at INTEGER NOT NULL DEFAULT 0,
            notification_message_id INTEGER NOT NULL DEFAULT 0,
            message TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS rooms (
            user_id INTEGER PRIMARY KEY,
            guild_id INTEGER NOT NULL,
            channel_id INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS todo_notification_settings (
            user_id INTEGER PRIMARY KEY,
            interval_minutes INTEGER NOT NULL,
            next_notify_at INTEGER NOT NULL
        );
    )SQL");
}

void Storage::migrate_schema() {
    std::lock_guard lock(mutex_);
    if (!column_exists(db_, "reminders", "next_notify_at")) {
        exec_sql(db_, "ALTER TABLE reminders ADD COLUMN next_notify_at INTEGER NOT NULL DEFAULT 0;");
    }
    if (!column_exists(db_, "reminders", "notification_message_id")) {
        exec_sql(db_, "ALTER TABLE reminders ADD COLUMN notification_message_id INTEGER NOT NULL DEFAULT 0;");
    }
    exec_sql(db_, "UPDATE reminders SET next_notify_at = due_at WHERE next_notify_at = 0;");
}

void Storage::migrate_tsv_if_needed() {
    const auto todos_file = data_dir_ / "todos.tsv";
    const auto reminders_file = data_dir_ / "reminders.tsv";

    if (std::filesystem::exists(todos_file) && table_empty(db_, "todos")) {
        std::ifstream in(todos_file);
        std::string line;
        while (std::getline(in, line)) {
            const auto fields = split_tab(line);
            if (fields.size() != 4) {
                continue;
            }
            try {
                Statement stmt(db_, "INSERT INTO todos (id, user_id, done, task) VALUES (?, ?, ?, ?);");
                sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(std::stoull(fields[0])));
                sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(std::stoull(fields[1])));
                sqlite3_bind_int(stmt.stmt, 3, fields[2] == "1" ? 1 : 0);
                const auto task = unescape_field(fields[3]);
                sqlite3_bind_text(stmt.stmt, 4, task.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt.stmt);
            } catch (...) {
                continue;
            }
        }
    }

    if (std::filesystem::exists(reminders_file) && table_empty(db_, "reminders")) {
        std::ifstream in(reminders_file);
        std::string line;
        while (std::getline(in, line)) {
            const auto fields = split_tab(line);
            if (fields.size() != 5) {
                continue;
            }
            try {
                Statement stmt(db_, "INSERT INTO reminders (id, user_id, channel_id, due_at, next_notify_at, message) VALUES (?, ?, ?, ?, ?, ?);");
                sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(std::stoull(fields[0])));
                sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(std::stoull(fields[1])));
                sqlite3_bind_int64(stmt.stmt, 3, static_cast<sqlite3_int64>(std::stoull(fields[2])));
                sqlite3_bind_int64(stmt.stmt, 4, static_cast<sqlite3_int64>(std::stoll(fields[3])));
                sqlite3_bind_int64(stmt.stmt, 5, static_cast<sqlite3_int64>(std::stoll(fields[3])));
                const auto message = unescape_field(fields[4]);
                sqlite3_bind_text(stmt.stmt, 6, message.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt.stmt);
            } catch (...) {
                continue;
            }
        }
    }
}

Reminder Storage::add_reminder(dpp::snowflake user_id, dpp::snowflake channel_id, int64_t minutes, std::string message) {
    const auto due_at = std::chrono::system_clock::now() + std::chrono::minutes{minutes};
    return add_reminder_at(user_id, channel_id, due_at, std::move(message));
}

Reminder Storage::add_reminder_at(dpp::snowflake user_id, dpp::snowflake channel_id, std::chrono::system_clock::time_point due_at, std::string message) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "INSERT INTO reminders (user_id, channel_id, due_at, next_notify_at, message) VALUES (?, ?, ?, ?, ?);");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(static_cast<uint64_t>(channel_id)));
    sqlite3_bind_int64(stmt.stmt, 3, static_cast<sqlite3_int64>(to_unix_seconds(due_at)));
    sqlite3_bind_int64(stmt.stmt, 4, static_cast<sqlite3_int64>(to_unix_seconds(due_at)));
    sqlite3_bind_text(stmt.stmt, 5, message.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    return Reminder{static_cast<uint64_t>(sqlite3_last_insert_rowid(db_)), user_id, channel_id, due_at, std::move(message)};
}

std::vector<Reminder> Storage::list_reminders(dpp::snowflake user_id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT id, user_id, channel_id, due_at, message FROM reminders WHERE user_id = ? ORDER BY due_at ASC;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));

    std::vector<Reminder> result;
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        result.push_back(Reminder{
            static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0)),
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 1))},
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 2))},
            from_unix_seconds(sqlite3_column_int64(stmt.stmt, 3)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 4))
        });
    }
    return result;
}

bool Storage::has_pending_reminders(dpp::snowflake user_id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT 1 FROM reminders WHERE user_id = ? LIMIT 1;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    return sqlite3_step(stmt.stmt) == SQLITE_ROW;
}

bool Storage::cancel_reminder(dpp::snowflake user_id, uint64_t id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "DELETE FROM reminders WHERE user_id = ? AND id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(id));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<Reminder> Storage::take_due_reminders_for_notification() {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    const auto now_seconds = to_unix_seconds(now);
    const auto next_seconds = to_unix_seconds(now + std::chrono::minutes{5});

    Statement select(db_, "SELECT id, user_id, channel_id, due_at, message FROM reminders WHERE due_at <= ? AND next_notify_at <= ? ORDER BY due_at ASC;");
    sqlite3_bind_int64(select.stmt, 1, static_cast<sqlite3_int64>(now_seconds));
    sqlite3_bind_int64(select.stmt, 2, static_cast<sqlite3_int64>(now_seconds));

    std::vector<Reminder> due;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) {
        due.push_back(Reminder{
            static_cast<uint64_t>(sqlite3_column_int64(select.stmt, 0)),
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(select.stmt, 1))},
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(select.stmt, 2))},
            from_unix_seconds(sqlite3_column_int64(select.stmt, 3)),
            reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 4))
        });
    }

    Statement update(db_, "UPDATE reminders SET next_notify_at = ? WHERE due_at <= ? AND next_notify_at <= ?;");
    sqlite3_bind_int64(update.stmt, 1, static_cast<sqlite3_int64>(next_seconds));
    sqlite3_bind_int64(update.stmt, 2, static_cast<sqlite3_int64>(now_seconds));
    sqlite3_bind_int64(update.stmt, 3, static_cast<sqlite3_int64>(now_seconds));
    if (sqlite3_step(update.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return due;
}

void Storage::set_reminder_notification_message(uint64_t id, dpp::snowflake message_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "UPDATE reminders SET notification_message_id = ? WHERE id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(message_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(id));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

bool Storage::acknowledge_reminder(dpp::snowflake user_id, uint64_t id) {
    return cancel_reminder(user_id, id);
}

bool Storage::acknowledge_reminder_by_message(dpp::snowflake user_id, dpp::snowflake message_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "DELETE FROM reminders WHERE user_id = ? AND notification_message_id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(static_cast<uint64_t>(message_id)));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

Todo Storage::add_todo(dpp::snowflake user_id, std::string task) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "INSERT INTO todos (user_id, done, task) VALUES (?, 0, ?);");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_text(stmt.stmt, 2, task.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    return Todo{static_cast<uint64_t>(sqlite3_last_insert_rowid(db_)), user_id, false, std::move(task)};
}

std::optional<Todo> Storage::get_todo(dpp::snowflake user_id, uint64_t id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT id, user_id, done, task FROM todos WHERE user_id = ? AND id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(id));

    if (sqlite3_step(stmt.stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    return Todo{
        static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0)),
        dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 1))},
        sqlite3_column_int(stmt.stmt, 2) != 0,
        reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3))
    };
}

std::vector<Todo> Storage::list_todos(dpp::snowflake user_id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT id, user_id, done, task FROM todos WHERE user_id = ? ORDER BY id ASC;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));

    std::vector<Todo> result;
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        result.push_back(Todo{
            static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0)),
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 1))},
            sqlite3_column_int(stmt.stmt, 2) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3))
        });
    }
    return result;
}

bool Storage::has_open_todos(dpp::snowflake user_id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT 1 FROM todos WHERE user_id = ? AND done = 0 LIMIT 1;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    return sqlite3_step(stmt.stmt) == SQLITE_ROW;
}

bool Storage::complete_todo(dpp::snowflake user_id, uint64_t id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "UPDATE todos SET done = 1 WHERE user_id = ? AND id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(id));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

bool Storage::delete_todo(dpp::snowflake user_id, uint64_t id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "DELETE FROM todos WHERE user_id = ? AND id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(id));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

size_t Storage::clear_done_todos(dpp::snowflake user_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "DELETE FROM todos WHERE user_id = ? AND done = 1;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return static_cast<size_t>(sqlite3_changes(db_));
}

void Storage::set_todo_notification(dpp::snowflake user_id, int64_t interval_minutes) {
    std::lock_guard lock(mutex_);
    if (interval_minutes <= 0) {
        Statement stmt(db_, "DELETE FROM todo_notification_settings WHERE user_id = ?;");
        sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
        if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
        return;
    }

    const auto next = to_unix_seconds(std::chrono::system_clock::now() + std::chrono::minutes{interval_minutes});
    Statement stmt(db_, "INSERT OR REPLACE INTO todo_notification_settings (user_id, interval_minutes, next_notify_at) VALUES (?, ?, ?);");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(interval_minutes));
    sqlite3_bind_int64(stmt.stmt, 3, static_cast<sqlite3_int64>(next));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<TodoNotificationSetting> Storage::get_todo_notification(dpp::snowflake user_id) const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT user_id, interval_minutes, next_notify_at FROM todo_notification_settings WHERE user_id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    if (sqlite3_step(stmt.stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    return TodoNotificationSetting{
        dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0))},
        sqlite3_column_int64(stmt.stmt, 1),
        from_unix_seconds(sqlite3_column_int64(stmt.stmt, 2))
    };
}

std::vector<TodoNotificationTarget> Storage::due_todo_notifications() {
    std::lock_guard lock(mutex_);
    const auto now_seconds = to_unix_seconds(std::chrono::system_clock::now());
    Statement stmt(db_, R"SQL(
        SELECT s.user_id, r.channel_id, s.interval_minutes
        FROM todo_notification_settings s
        JOIN rooms r ON r.user_id = s.user_id
        WHERE s.next_notify_at <= ?
          AND EXISTS (SELECT 1 FROM todos t WHERE t.user_id = s.user_id AND t.done = 0)
        ORDER BY s.next_notify_at ASC;
    )SQL");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(now_seconds));

    std::vector<TodoNotificationTarget> result;
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        result.push_back(TodoNotificationTarget{
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0))},
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 1))},
            sqlite3_column_int64(stmt.stmt, 2)
        });
    }
    return result;
}

void Storage::mark_todo_notification_checked(dpp::snowflake user_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, R"SQL(
        UPDATE todo_notification_settings
        SET next_notify_at = ? + interval_minutes * 60
        WHERE user_id = ?;
    )SQL");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(to_unix_seconds(std::chrono::system_clock::now())));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

void Storage::save_room(dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake channel_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "INSERT OR REPLACE INTO rooms (user_id, guild_id, channel_id) VALUES (?, ?, ?);");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_bind_int64(stmt.stmt, 2, static_cast<sqlite3_int64>(static_cast<uint64_t>(guild_id)));
    sqlite3_bind_int64(stmt.stmt, 3, static_cast<sqlite3_int64>(static_cast<uint64_t>(channel_id)));
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

void Storage::delete_room(dpp::snowflake user_id) {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "DELETE FROM rooms WHERE user_id = ?;");
    sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(static_cast<uint64_t>(user_id)));
    sqlite3_step(stmt.stmt);
}

std::vector<ManagedRoom> Storage::list_rooms() const {
    std::lock_guard lock(mutex_);
    Statement stmt(db_, "SELECT user_id, guild_id, channel_id FROM rooms;");

    std::vector<ManagedRoom> result;
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        result.push_back(ManagedRoom{
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 2))},
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 1))},
            dpp::snowflake{static_cast<uint64_t>(sqlite3_column_int64(stmt.stmt, 0))}
        });
    }
    return result;
}
