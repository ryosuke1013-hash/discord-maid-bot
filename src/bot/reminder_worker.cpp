#include "reminder_worker.hpp"

#include "../domain/reminder.hpp"
#include "../domain/todo.hpp"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

dpp::component action_row(std::vector<dpp::component> buttons) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    for (const auto& item : buttons) {
        row.add_component(item);
    }
    return row;
}

dpp::component make_button(std::string label, std::string id, dpp::component_style style) {
    return dpp::component().set_label(std::move(label)).set_id(std::move(id)).set_style(style);
}

std::string open_todo_text(const std::vector<Todo>& todos) {
    std::ostringstream oss;
    oss << "未完了のお仕事です。\n";
    for (const Todo& todo : todos) {
        if (!todo.done) {
            oss << "[ ] `#" << todo.id << "` " << todo.task << '\n';
        }
    }
    return oss.str();
}

std::string truncate_label(const std::string& value, size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    return value.substr(0, max_length - 3) + "...";
}

void add_todo_done_buttons(dpp::message& message, const std::vector<Todo>& todos) {
    std::vector<dpp::component> row_buttons;
    size_t button_count = 0;

    for (const Todo& todo : todos) {
        if (todo.done || button_count >= 25) {
            continue;
        }

        row_buttons.push_back(make_button(
            "完了: " + truncate_label(todo.task, 70),
            "todo:done:" + std::to_string(todo.id),
            dpp::cos_success
        ));
        ++button_count;

        if (row_buttons.size() == 5) {
            message.add_component(action_row(row_buttons));
            row_buttons.clear();
        }
    }

    if (!row_buttons.empty()) {
        message.add_component(action_row(row_buttons));
    }
}

} // namespace

void start_reminder_worker(dpp::cluster& bot, Storage& storage, std::atomic_bool& running) {
    std::thread([&bot, &storage, &running] {
        while (running.load()) {
            for (const Reminder& reminder : storage.take_due_reminders_for_notification()) {
                dpp::message message(reminder.channel_id,
                    "<@" + std::to_string(static_cast<uint64_t>(reminder.user_id)) + "> ご主人様、" + reminder.message + "のお時間です。\n"
                    "確認後、✅ またはボタンをお願いします。");
                message.add_component(action_row({
                    make_button("確認", "remind:ack:" + std::to_string(reminder.id), dpp::cos_success)
                }));

                bot.message_create(message, [&bot, &storage, id = reminder.id](const dpp::confirmation_callback_t& callback) {
                    if (callback.is_error()) {
                        return;
                    }

                    const auto sent = std::get<dpp::message>(callback.value);
                    storage.set_reminder_notification_message(id, sent.id);
                    bot.message_add_reaction(sent.id, sent.channel_id, "✅");
                });
            }

            for (const TodoNotificationTarget& target : storage.due_todo_notifications()) {
                const auto todos = storage.list_todos(target.user_id);
                dpp::message message(target.channel_id,
                    "<@" + std::to_string(static_cast<uint64_t>(target.user_id)) + "> " + open_todo_text(todos));
                add_todo_done_buttons(message, todos);
                bot.message_create(message);
                storage.mark_todo_notification_checked(target.user_id);
            }

            for (int i = 0; i < 10 && running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }).detach();
}
