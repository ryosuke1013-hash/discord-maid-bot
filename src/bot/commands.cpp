#include "commands.hpp"

#include "../domain/reminder.hpp"
#include "../domain/todo.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr auto max_reminder_minutes = 60 * 24 * 30;

struct PendingReminderInput {
    dpp::snowflake user_id{};
    dpp::snowflake channel_id{};
    std::string message;
};

struct PendingHimaInput {
    dpp::snowflake user_id{};
    dpp::snowflake guild_id{};
    dpp::snowflake channel_id{};
    std::vector<dpp::snowflake> users;
};

std::mutex pending_reminders_mutex;
std::unordered_map<std::string, PendingReminderInput> pending_reminders;
uint64_t next_pending_reminder_id = 1;

std::mutex pending_hima_mutex;
std::unordered_map<std::string, PendingHimaInput> pending_hima;
uint64_t next_pending_hima_id = 1;

uint64_t get_user_id(const dpp::slashcommand_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t get_user_id(const dpp::button_click_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t get_user_id(const dpp::select_click_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t get_user_id(const dpp::message_reaction_add_t& event) {
    if (event.reacting_member.user_id != 0) {
        return static_cast<uint64_t>(event.reacting_member.user_id);
    }
    return static_cast<uint64_t>(event.reacting_user.id);
}

uint64_t get_user_id(const dpp::form_submit_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

bool has_master_role(const std::vector<dpp::snowflake>& roles) {
    for (const dpp::snowflake role_id : roles) {
        const dpp::role* role = dpp::find_role(role_id);
        if (role && role->name == "ご主人様") {
            return true;
        }
    }
    return false;
}

bool has_master_role(const dpp::guild_member& member) {
    return has_master_role(member.get_roles());
}

bool is_master(const dpp::slashcommand_t& event) {
    return has_master_role(event.command.member);
}

bool is_master(const dpp::button_click_t& event) {
    return has_master_role(event.command.member);
}

bool is_master(const dpp::message_create_t& event) {
    return has_master_role(event.msg.member);
}

std::string polite(bool master, const std::string& master_text, const std::string& other_text) {
    return master ? master_text : other_text;
}

std::string get_subcommand_name(const dpp::slashcommand_t& event) {
    const auto& data = std::get<dpp::command_interaction>(event.command.data);
    if (data.options.empty()) {
        return {};
    }
    return data.options.front().name;
}

const dpp::command_data_option* find_subcommand_option(const dpp::slashcommand_t& event, const std::string& name) {
    const auto& data = std::get<dpp::command_interaction>(event.command.data);
    if (data.options.empty()) {
        return nullptr;
    }

    const auto& options = data.options.front().options;
    const auto it = std::find_if(options.begin(), options.end(), [&name](const dpp::command_data_option& option) {
        return option.name == name;
    });
    return it == options.end() ? nullptr : &*it;
}

const dpp::command_data_option* find_top_level_option(const dpp::slashcommand_t& event, const std::string& name) {
    const auto& data = std::get<dpp::command_interaction>(event.command.data);
    const auto it = std::find_if(data.options.begin(), data.options.end(), [&name](const dpp::command_data_option& option) {
        return option.name == name;
    });
    return it == data.options.end() ? nullptr : &*it;
}

std::string get_string_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_subcommand_option(event, name);
    if (!option) {
        throw std::runtime_error("missing option: " + name);
    }
    return std::get<std::string>(option->value);
}

std::string get_top_level_string_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_top_level_option(event, name);
    if (!option) {
        throw std::runtime_error("missing option: " + name);
    }
    return std::get<std::string>(option->value);
}

std::optional<std::string> get_optional_top_level_string_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_top_level_option(event, name);
    if (!option) {
        return std::nullopt;
    }
    return std::get<std::string>(option->value);
}

std::optional<dpp::snowflake> get_optional_top_level_snowflake_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_top_level_option(event, name);
    if (!option) {
        return std::nullopt;
    }
    return std::get<dpp::snowflake>(option->value);
}

int64_t get_int_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_subcommand_option(event, name);
    if (!option) {
        throw std::runtime_error("missing option: " + name);
    }
    return std::get<int64_t>(option->value);
}

std::optional<int64_t> get_optional_int_param(const dpp::slashcommand_t& event, const std::string& name) {
    const auto* option = find_subcommand_option(event, name);
    if (!option) {
        return std::nullopt;
    }
    return std::get<int64_t>(option->value);
}

std::string todo_notification_label(int64_t interval_minutes) {
    if (interval_minutes == 30) return "30分に1回";
    if (interval_minutes == 60) return "1時間に1回";
    if (interval_minutes == 60 * 24) return "1日1回";
    return "OFF";
}

int64_t todo_notification_interval(const std::string& value) {
    if (value == "30m") return 30;
    if (value == "1h") return 60;
    if (value == "1d") return 60 * 24;
    return 0;
}

std::string complete_todo_by_id(Storage& storage, dpp::snowflake user_id, uint64_t id) {
    const auto todo = storage.get_todo(user_id, id);
    if (!todo) {
        return "そのお仕事は見つかりません。";
    }
    storage.delete_todo(user_id, id);
    return "「" + todo->task + "」が完了しました。";
}

bool is_user_private_room(Storage& storage, dpp::snowflake user_id, dpp::snowflake channel_id) {
    for (const ManagedRoom& room : storage.list_rooms()) {
        if (room.user_id == user_id && room.channel_id == channel_id) {
            return true;
        }
    }
    return false;
}

void append_unique(std::vector<dpp::snowflake>& values, dpp::snowflake value) {
    if (value == 0 || std::find(values.begin(), values.end(), value) != values.end()) {
        return;
    }
    values.push_back(value);
}

VcMentionTarget mention_target_from_slash(const dpp::slashcommand_t& event) {
    VcMentionTarget target;

    for (const std::string name : {"user1", "user2", "user3"}) {
        if (const auto user = get_optional_top_level_snowflake_param(event, name)) {
            append_unique(target.users, *user);
            if (!target.text.empty()) {
                target.text += " ";
            }
            target.text += "<@" + std::to_string(static_cast<uint64_t>(*user)) + ">";
        }
    }

    if (const auto role = get_optional_top_level_snowflake_param(event, "role")) {
        append_unique(target.roles, *role);
        if (!target.text.empty()) {
            target.text += " ";
        }
        target.text += "<@&" + std::to_string(static_cast<uint64_t>(*role)) + ">";
    }

    return target;
}

dpp::message ephemeral_message(std::string text) {
    dpp::message message(std::move(text));
    message.flags |= dpp::m_ephemeral;
    return message;
}

dpp::message ephemeral_message(dpp::message message) {
    message.flags |= dpp::m_ephemeral;
    return message;
}

dpp::snowflake configured_announce_channel_id() {
    const char* value = std::getenv("DISCORD_ANNOUNCE_CHANNEL_ID");
    if (!value || std::string(value).empty()) {
        return {};
    }

    try {
        return dpp::snowflake{std::stoull(value)};
    } catch (...) {
        return {};
    }
}

dpp::snowflake default_announce_channel_id(dpp::snowflake guild_id, dpp::snowflake fallback_channel_id) {
    const dpp::snowflake configured = configured_announce_channel_id();
    if (configured != 0) {
        return configured;
    }

    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild && guild->system_channel_id != 0) {
        return guild->system_channel_id;
    }

    return fallback_channel_id;
}

dpp::snowflake vc_announce_channel_id(Storage& storage, dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake current_channel_id) {
    if (!is_user_private_room(storage, user_id, current_channel_id)) {
        return current_channel_id;
    }

    return default_announce_channel_id(guild_id, current_channel_id);
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string normalize_time_input(std::string input) {
    const std::vector<std::pair<std::string, std::string>> replacements{
        {"０", "0"}, {"１", "1"}, {"２", "2"}, {"３", "3"}, {"４", "4"},
        {"５", "5"}, {"６", "6"}, {"７", "7"}, {"８", "8"}, {"９", "9"},
        {"：", ":"}, {"／", "/"}, {"－", "-"},
        {"ｍ", "m"}, {"Ｍ", "m"}, {"ｈ", "h"}, {"Ｈ", "h"}, {"ｄ", "d"}, {"Ｄ", "d"},
    };
    for (const auto& [from, to] : replacements) {
        replace_all(input, from, to);
    }
    return input;
}

std::optional<int64_t> parse_duration_minutes(const std::string& input) {
    if (input.size() < 2) {
        return std::nullopt;
    }

    const char unit = input.back();
    const std::string number_text = input.substr(0, input.size() - 1);
    if (number_text.empty() || !std::all_of(number_text.begin(), number_text.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return std::nullopt;
    }

    const int64_t amount = std::stoll(number_text);
    if (amount <= 0) {
        return std::nullopt;
    }

    if (unit == 'm') return amount;
    if (unit == 'h') return amount * 60;
    if (unit == 'd') return amount * 60 * 24;
    return std::nullopt;
}

std::tm local_tm_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return tm;
}

std::chrono::system_clock::time_point from_local_tm(std::tm tm) {
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::optional<std::pair<int, int>> parse_time_text(const std::string& input) {
    std::smatch match;
    if (std::regex_search(input, match, std::regex(R"((\d{1,2})\s*[:：]\s*(\d{1,2}))"))) {
        return std::pair{std::stoi(match[1]), std::stoi(match[2])};
    }
    if (std::regex_search(input, match, std::regex(R"((\d{1,2})\s*時\s*(?:(\d{1,2})\s*分?)?)"))) {
        return std::pair{std::stoi(match[1]), match[2].matched ? std::stoi(match[2]) : 0};
    }
    if (std::regex_match(input, match, std::regex(R"(\s*(\d{1,2})\s*)"))) {
        return std::pair{std::stoi(match[1]), 0};
    }
    return std::nullopt;
}

bool valid_time(int hour, int minute) {
    return ((0 <= hour && hour <= 23) || (hour == 24 && minute == 0)) && 0 <= minute && minute <= 59;
}

std::optional<int> weekday_index(const std::string& input) {
    const std::vector<std::pair<std::string, int>> names{
        {"日曜", 0}, {"日曜日", 0},
        {"月曜", 1}, {"月曜日", 1},
        {"火曜", 2}, {"火曜日", 2},
        {"水曜", 3}, {"水曜日", 3},
        {"木曜", 4}, {"木曜日", 4},
        {"金曜", 5}, {"金曜日", 5},
        {"土曜", 6}, {"土曜日", 6},
    };
    for (const auto& [name, index] : names) {
        if (input.find(name) != std::string::npos) {
            return index;
        }
    }
    return std::nullopt;
}

bool has_date_hint(const std::string& input, const std::string& normalized) {
    if (input.find("今日") != std::string::npos ||
        input.find("明日") != std::string::npos ||
        input.find("あした") != std::string::npos ||
        input.find("明後日") != std::string::npos ||
        input.find("あさって") != std::string::npos ||
        input.find("来週") != std::string::npos ||
        weekday_index(input)) {
        return true;
    }

    return std::regex_search(normalized, std::regex(R"(\d{4}[/-]\d{1,2}[/-]\d{1,2})")) ||
           std::regex_search(normalized, std::regex(R"(\d{1,2}[/-]\d{1,2})")) ||
           std::regex_search(input, std::regex(R"(\d{1,2}\s*月\s*\d{1,2}\s*日?)"));
}

std::optional<std::chrono::system_clock::time_point> parse_reminder_time(const std::string& input) {
    const std::string normalized = normalize_time_input(input);
    if (const auto minutes = parse_duration_minutes(normalized)) {
        if (*minutes > max_reminder_minutes) {
            return std::nullopt;
        }
        return std::chrono::system_clock::now() + std::chrono::minutes{*minutes};
    }

    const auto parsed_time = parse_time_text(normalized);
    if (!parsed_time && !has_date_hint(input, normalized)) {
        return std::nullopt;
    }

    const auto time = parsed_time.value_or(std::pair{9, 0});
    if (!valid_time(time.first, time.second)) {
        return std::nullopt;
    }
    const bool time_rolls_next_day = time.first == 24 && time.second == 0;
    const int hour = time_rolls_next_day ? 0 : time.first;

    const auto now = std::chrono::system_clock::now();
    std::tm tm = local_tm_now();
    tm.tm_sec = 0;
    tm.tm_min = time.second;
    tm.tm_hour = hour;

    std::smatch match;
    if (std::regex_search(normalized, match, std::regex(R"((\d{4})[/-](\d{1,2})[/-](\d{1,2}))"))) {
        tm.tm_year = std::stoi(match[1]) - 1900;
        tm.tm_mon = std::stoi(match[2]) - 1;
        tm.tm_mday = std::stoi(match[3]);
        if (time_rolls_next_day) {
            tm.tm_mday += 1;
        }
        const auto result = from_local_tm(tm);
        return result > now ? std::optional{result} : std::nullopt;
    }

    if (std::regex_search(normalized, match, std::regex(R"((\d{1,2})[/-](\d{1,2}))")) ||
        std::regex_search(input, match, std::regex(R"((\d{1,2})\s*月\s*(\d{1,2})\s*日?)"))) {
        tm.tm_mon = std::stoi(match[1]) - 1;
        tm.tm_mday = std::stoi(match[2]);
        if (time_rolls_next_day) {
            tm.tm_mday += 1;
        }
        auto result = from_local_tm(tm);
        if (result <= now) {
            tm.tm_year += 1;
            result = from_local_tm(tm);
        }
        return result;
    }

    int add_days = 0;
    if (input.find("明後日") != std::string::npos || input.find("あさって") != std::string::npos) {
        add_days = 2;
    } else if (input.find("明日") != std::string::npos || input.find("あした") != std::string::npos) {
        add_days = 1;
    } else if (input.find("今日") != std::string::npos) {
        add_days = 0;
    } else if (const auto weekday = weekday_index(input)) {
        const bool next_week = input.find("来週") != std::string::npos;
        add_days = (*weekday - tm.tm_wday + 7) % 7;
        if (add_days == 0 || next_week) {
            add_days += 7;
        }
    }

    if (time_rolls_next_day) {
        add_days += 1;
    }
    tm.tm_mday += add_days;
    auto result = from_local_tm(tm);
    if (result <= now) {
        tm.tm_mday += 1;
        result = from_local_tm(tm);
    }
    return result;
}

std::string format_time(std::chrono::system_clock::time_point tp) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

dpp::component action_row(std::vector<dpp::component> buttons) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    for (const auto& item : buttons) {
        row.add_component(item);
    }
    return row;
}

dpp::component make_button(std::string label, std::string id, dpp::component_style style) {
    return dpp::component().set_label(label).set_id(id).set_style(style);
}

dpp::component text_input(std::string id, std::string label, std::string placeholder, bool required = true) {
    dpp::component input;
    input.set_type(dpp::cot_text)
        .set_label(std::move(label))
        .set_id(std::move(id))
        .set_placeholder(std::move(placeholder))
        .set_text_style(dpp::text_short)
        .set_min_length(required ? 1 : 0)
        .set_max_length(1000)
        .set_required(required);
    return input;
}

dpp::component select_menu(std::string id, std::string placeholder, std::vector<dpp::select_option> options) {
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu)
        .set_id(id)
        .set_placeholder(placeholder)
        .set_min_values(1)
        .set_max_values(1);

    for (const auto& option : options) {
        menu.add_select_option(option);
    }

    return dpp::component().add_component(menu);
}

dpp::message main_menu_response() {
    dpp::message response("ご用件をお選びください。");
    response.add_component(action_row({
        make_button("TODO", "hi:todo", dpp::cos_primary),
        make_button("リマインダー", "hi:remind", dpp::cos_primary),
        make_button("部屋", "hi:room", dpp::cos_secondary),
        make_button("募集", "hi:hima", dpp::cos_success)
    }));
    return response;
}

dpp::message todo_menu_response() {
    dpp::message response("TODO操作をお選びください。");
    response.add_component(action_row({
        make_button("追加", "hi:todo:add", dpp::cos_success),
        make_button("一覧", "hi:todo:list", dpp::cos_primary),
        make_button("通知設定", "hi:todo:notify", dpp::cos_secondary)
    }));
    return response;
}

dpp::message todo_notify_menu_response() {
    dpp::message response("TODO通知の間隔をお選びください。");
    response.add_component(action_row({
        make_button("OFF", "hi:todo:notify:off", dpp::cos_secondary),
        make_button("30分", "hi:todo:notify:30m", dpp::cos_primary),
        make_button("1時間", "hi:todo:notify:1h", dpp::cos_primary),
        make_button("1日", "hi:todo:notify:1d", dpp::cos_primary)
    }));
    return response;
}

dpp::message reminder_menu_response() {
    dpp::message response("リマインダー操作をお選びください。");
    response.add_component(action_row({
        make_button("追加", "hi:remind:add", dpp::cos_success),
        make_button("一覧", "hi:remind:list", dpp::cos_primary),
        make_button("取消", "hi:remind:cancel", dpp::cos_danger)
    }));
    return response;
}

dpp::interaction_modal_response todo_add_modal() {
    dpp::interaction_modal_response modal("hi:todo:add_modal", "TODO追加");
    modal.add_component(text_input("task", "内容", "例: レポートを書く"));
    return modal;
}

dpp::interaction_modal_response reminder_add_modal() {
    dpp::interaction_modal_response modal("hi:remind:add_modal", "リマインダー追加");
    modal.add_component(text_input("message", "内容", "例: 薬を飲む"));
    modal.add_component(text_input("when", "時間", "例: 18:30 / 明日 9時"));
    return modal;
}

struct HimaContext {
    dpp::snowflake guild_id{};
    dpp::snowflake channel_id{};
};

std::string hima_context_suffix(dpp::snowflake guild_id, dpp::snowflake channel_id) {
    return std::to_string(static_cast<uint64_t>(guild_id)) + ":" + std::to_string(static_cast<uint64_t>(channel_id));
}

dpp::message hima_target_response(dpp::snowflake guild_id, dpp::snowflake channel_id) {
    dpp::message response("通知するユーザーをお選びください。");
    dpp::component menu;
    menu.set_type(dpp::cot_user_selectmenu)
        .set_id("hi:hima:users:" + hima_context_suffix(guild_id, channel_id))
        .set_placeholder("通知するユーザーを選択")
        .set_min_values(1)
        .set_max_values(3);
    response.add_component(dpp::component().add_component(menu));
    response.add_component(action_row({
        make_button("指定なし", "hi:hima:none:" + hima_context_suffix(guild_id, channel_id), dpp::cos_secondary)
    }));
    return response;
}

dpp::interaction_modal_response hima_modal(const std::string& pending_id) {
    std::string id = "hi:hima:modal:" + pending_id;
    dpp::interaction_modal_response modal(id, "募集");
    modal.add_component(text_input("game", "ゲーム名", "例: valorant"));
    modal.add_component(text_input("start", "開始時刻", "例: 21:00 / 今から / 未定", false));
    modal.add_component(text_input("note", "メッセージ", "例: あと1人", false));
    return modal;
}

std::string create_pending_hima(dpp::snowflake user_id, dpp::snowflake guild_id, dpp::snowflake channel_id, std::vector<dpp::snowflake> users = {}) {
    std::lock_guard lock(pending_hima_mutex);
    const std::string id = std::to_string(next_pending_hima_id++);
    pending_hima[id] = PendingHimaInput{user_id, guild_id, channel_id, std::move(users)};
    return id;
}

std::optional<PendingHimaInput> take_pending_hima(const std::string& id, dpp::snowflake user_id) {
    std::lock_guard lock(pending_hima_mutex);
    const auto it = pending_hima.find(id);
    if (it == pending_hima.end() || it->second.user_id != user_id) {
        return std::nullopt;
    }
    PendingHimaInput input = std::move(it->second);
    pending_hima.erase(it);
    return input;
}

std::string pending_hima_id_from_modal_id(const std::string& custom_id) {
    const std::string prefix = "hi:hima:modal:";
    if (custom_id.rfind(prefix, 0) != 0) {
        return {};
    }
    return custom_id.substr(prefix.size());
}

HimaContext hima_context_from_id(const std::string& custom_id, const std::string& prefix, dpp::snowflake fallback_guild_id, dpp::snowflake fallback_channel_id) {
    if (custom_id.rfind(prefix, 0) != 0) {
        return HimaContext{fallback_guild_id, fallback_channel_id};
    }

    std::stringstream stream(custom_id.substr(prefix.size()));
    std::string guild_text;
    std::string channel_text;
    if (!std::getline(stream, guild_text, ':') || !std::getline(stream, channel_text, ':')) {
        return HimaContext{fallback_guild_id, fallback_channel_id};
    }

    try {
        return HimaContext{dpp::snowflake{std::stoull(guild_text)}, dpp::snowflake{std::stoull(channel_text)}};
    } catch (...) {
        return HimaContext{fallback_guild_id, fallback_channel_id};
    }
}

VcMentionTarget mention_target_from_users(const std::vector<dpp::snowflake>& users) {
    VcMentionTarget target;
    for (const dpp::snowflake user : users) {
        append_unique(target.users, user);
        if (!target.text.empty()) {
            target.text += " ";
        }
        target.text += "<@" + std::to_string(static_cast<uint64_t>(user)) + ">";
    }
    return target;
}

std::string create_pending_reminder(dpp::snowflake user_id, dpp::snowflake channel_id, std::string message) {
    std::lock_guard lock(pending_reminders_mutex);
    const std::string id = "remind:time:" + std::to_string(next_pending_reminder_id++);
    pending_reminders[id] = PendingReminderInput{user_id, channel_id, std::move(message)};
    return id;
}

std::optional<PendingReminderInput> take_pending_reminder(const std::string& id, dpp::snowflake user_id) {
    std::lock_guard lock(pending_reminders_mutex);
    const auto it = pending_reminders.find(id);
    if (it == pending_reminders.end() || it->second.user_id != user_id) {
        return std::nullopt;
    }
    PendingReminderInput input = std::move(it->second);
    pending_reminders.erase(it);
    return input;
}

dpp::interaction_modal_response reminder_time_modal(const std::string& id) {
    dpp::component input;
    input.set_label("時間")
        .set_id("when")
        .set_placeholder("例: 18:30 / １８：３０ / 明日 9時")
        .set_text_style(dpp::text_short)
        .set_min_length(1)
        .set_max_length(50)
        .set_required(true);

    return dpp::interaction_modal_response(id, "リマインダーの時間", {input});
}

std::optional<std::string> modal_value(const dpp::form_submit_t& event, const std::string& id) {
    for (const dpp::component& component : event.components) {
        if (component.custom_id == id && std::holds_alternative<std::string>(component.value)) {
            return std::get<std::string>(component.value);
        }

        for (const dpp::component& child : component.components) {
            if (child.custom_id == id && std::holds_alternative<std::string>(child.value)) {
                return std::get<std::string>(child.value);
            }

            for (const dpp::component& component : child.components) {
            if (component.custom_id == id && std::holds_alternative<std::string>(component.value)) {
                return std::get<std::string>(component.value);
            }
        }
    }
    }
    return std::nullopt;
}

void show_dialog(const dpp::button_click_t& event, const dpp::interaction_modal_response& modal) {
    event.dialog(modal, [](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Modal error: " << callback.get_error().message << '\n';
        }
    });
}

void show_dialog(const dpp::select_click_t& event, const dpp::interaction_modal_response& modal) {
    event.dialog(modal, [](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Modal error: " << callback.get_error().message << '\n';
        }
    });
}

void show_dialog(const dpp::slashcommand_t& event, const dpp::interaction_modal_response& modal) {
    event.dialog(modal, [](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Modal error: " << callback.get_error().message << '\n';
        }
    });
}

void show_dialog(const dpp::form_submit_t& event, const dpp::interaction_modal_response& modal) {
    event.dialog(modal, [](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Modal error: " << callback.get_error().message << '\n';
        }
    });
}

void clean_bot_messages_in_private_room(dpp::cluster& bot, Storage& storage, const dpp::slashcommand_t& event, dpp::snowflake user_id) {
    if (!is_user_private_room(storage, user_id, event.command.channel_id)) {
        event.reply("このコマンドは専用部屋でのみ使えます。");
        return;
    }

    event.thinking(true);
    bot.messages_get(event.command.channel_id, 0, 0, 0, 100, [&bot, event](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            event.edit_response("メッセージ取得に失敗しました。");
            return;
        }

        const auto messages = std::get<dpp::message_map>(callback.value);
        const double now = std::time(nullptr);
        constexpr double max_bulk_delete_age = 13.5 * 24 * 60 * 60;
        std::vector<dpp::snowflake> message_ids;
        for (const auto& [id, message] : messages) {
            if (message.author.id == bot.me.id && now - id.get_creation_time() < max_bulk_delete_age) {
                message_ids.push_back(id);
            }
        }

        if (message_ids.empty()) {
            event.edit_response("削除できるBotメッセージはありません。");
            return;
        }

        if (message_ids.size() == 1) {
            bot.message_delete(message_ids.front(), event.command.channel_id, [event](const dpp::confirmation_callback_t& delete_callback) {
                event.edit_response(delete_callback.is_error() ? "削除に失敗しました。" : "Botのメッセージを1件削除しました。");
            });
            return;
        }

        bot.message_delete_bulk(message_ids, event.command.channel_id, [event, count = message_ids.size()](const dpp::confirmation_callback_t& delete_callback) {
            event.edit_response(delete_callback.is_error() ? "削除に失敗しました。" : "Botのメッセージを " + std::to_string(count) + " 件削除しました。");
        });
    });
}

std::string truncate_label(const std::string& text, size_t max_length) {
    if (text.size() <= max_length) {
        return text;
    }
    return text.substr(0, max_length - 3) + "...";
}

std::string todo_list_text(const std::vector<Todo>& todos) {
    if (todos.empty()) {
        return "お仕事はございません。";
    }

    std::ostringstream oss;
    oss << "お仕事一覧です。\n";
    for (const Todo& todo : todos) {
        oss << (todo.done ? "[x] " : "[ ] ")
            << "`#" << todo.id << "` "
            << todo.task << '\n';
    }
    return oss.str();
}

std::vector<dpp::select_option> todo_options(const std::vector<Todo>& todos, bool include_done) {
    std::vector<dpp::select_option> options;
    for (const Todo& todo : todos) {
        if (!include_done && todo.done) {
            continue;
        }
        options.emplace_back("#" + std::to_string(todo.id) + " " + truncate_label(todo.task, 70), std::to_string(todo.id), todo.done ? "完了済みでございます" : "未完了でございます");
        if (options.size() == 25) {
            break;
        }
    }
    return options;
}

std::vector<dpp::select_option> reminder_options(const std::vector<Reminder>& reminders) {
    std::vector<dpp::select_option> options;
    for (const Reminder& reminder : reminders) {
        options.emplace_back("#" + std::to_string(reminder.id) + " " + truncate_label(reminder.message, 70), std::to_string(reminder.id), format_time(reminder.due_at));
        if (options.size() == 25) {
            break;
        }
    }
    return options;
}

dpp::message todo_list_response(const std::vector<Todo>& todos) {
    dpp::message response(todo_list_text(todos));
    const auto open_options = todo_options(todos, false);
    const auto all_options = todo_options(todos, true);
    if (!open_options.empty()) {
        response.add_component(select_menu("todo:done_select", "完了にするお仕事をお選びください", open_options));
    }
    if (!all_options.empty()) {
        response.add_component(select_menu("todo:delete_select", "削除するお仕事をお選びください", all_options));
    }
    return response;
}

std::string reminder_list_text(const std::vector<Reminder>& reminders) {
    if (reminders.empty()) {
        return "ご予定はございません。";
    }

    std::ostringstream oss;
    oss << "ご予定一覧です。\n";
    for (const Reminder& reminder : reminders) {
        oss << "`#" << reminder.id << "` " << format_time(reminder.due_at) << " - " << reminder.message << '\n';
    }
    return oss.str();
}

dpp::message reminder_list_response(const std::vector<Reminder>& reminders) {
    dpp::message response(reminder_list_text(reminders));
    const auto options = reminder_options(reminders);
    if (!options.empty()) {
        response.add_component(select_menu("remind:cancel_select", "取り消すご予定をお選びください", options));
    }
    return response;
}

dpp::message reminder_cancel_response(const std::vector<Reminder>& reminders) {
    dpp::message response(reminder_list_text(reminders));
    if (reminders.empty()) {
        return response;
    }

    std::vector<dpp::component> row_buttons;
    size_t count = 0;
    for (const Reminder& reminder : reminders) {
        row_buttons.push_back(make_button(
            "#" + std::to_string(reminder.id) + " " + truncate_label(reminder.message, 60),
            "remind:cancel:" + std::to_string(reminder.id),
            dpp::cos_danger
        ));
        ++count;

        if (row_buttons.size() == 5) {
            response.add_component(action_row(row_buttons));
            row_buttons.clear();
        }
        if (count == 25) {
            break;
        }
    }

    if (!row_buttons.empty()) {
        response.add_component(action_row(row_buttons));
    }
    return response;
}

uint64_t selected_id(const std::vector<std::string>& values) {
    if (values.empty()) {
        throw std::runtime_error("no selection");
    }
    return std::stoull(values.front());
}

std::string reminder_help_text() {
    return "`when` の指定例です。\n"
           "`10m` = 10分後\n"
           "`2h` = 2時間後\n"
           "`1d` = 1日後\n"
           "`今日 18:30`\n"
           "`明日 9時`\n"
           "`明後日 14時`\n"
           "`6/20 21:00`\n"
           "`2026/6/20 9:00`\n"
           "`月曜 8時`\n"
           "`来週月曜 8時`\n"
           "例: `/remind add when:明日 9時 message:燃えるゴミ`";
}

std::string full_help_text() {
    return "ご利用いただけるコマンドです。\n"
           "`/todo add task:...` お仕事を追加\n"
           "`/todo list` お仕事一覧と選択メニュー\n"
           "`/todo done id:...` お仕事を完了\n"
           "`/todo delete id:...` お仕事を削除\n"
           "`/todo notify interval:...` 未完了TODOの定期通知を設定\n"
           "`/remind add when:明日 9時 message:...` ご予定を追加\n"
           "`/remind list` ご予定一覧と取消メニュー\n"
           "`/remind help` 時間指定の説明\n"
           "`/vc game:valorant user1:@相手 role:@ロール start:21:00 note:あと1人` 募集用プライベートVCを作成\n"
           "`/room` ご主人様専用のお部屋を作成\n"
           "`/ping` 動作確認\n"
           "`/clean` 専用部屋内のBotメッセージを削除\n"
           "`/hi` メニューを表示\n"
           "`/help` この一覧を表示";
}

} // namespace

std::vector<dpp::slashcommand> build_commands(dpp::snowflake application_id) {
    dpp::slashcommand todo("todo", "お仕事を管理いたします", application_id);
    todo.add_option(dpp::command_option(dpp::co_sub_command, "add", "お仕事を追加いたします").add_option(dpp::command_option(dpp::co_string, "task", "お仕事の内容", true)));
    todo.add_option(dpp::command_option(dpp::co_sub_command, "list", "お仕事一覧を表示いたします"));
    todo.add_option(dpp::command_option(dpp::co_sub_command, "done", "お仕事を完了にいたします").add_option(dpp::command_option(dpp::co_integer, "id", "お仕事ID", true).set_min_value(1)));
    todo.add_option(dpp::command_option(dpp::co_sub_command, "delete", "お仕事を削除いたします").add_option(dpp::command_option(dpp::co_integer, "id", "お仕事ID", true).set_min_value(1)));
    todo.add_option(dpp::command_option(dpp::co_sub_command, "notify", "未完了TODOの定期通知を設定いたします")
        .add_option(dpp::command_option(dpp::co_string, "interval", "通知間隔", true)
            .add_choice(dpp::command_option_choice("OFF", std::string("off")))
            .add_choice(dpp::command_option_choice("30分に1回", std::string("30m")))
            .add_choice(dpp::command_option_choice("1時間に1回", std::string("1h")))
            .add_choice(dpp::command_option_choice("1日1回", std::string("1d")))));

    dpp::slashcommand remind("remind", "ご予定を管理いたします", application_id);
    remind.add_option(dpp::command_option(dpp::co_sub_command, "add", "ご予定を追加いたします")
        .add_option(dpp::command_option(dpp::co_string, "when", "例: 10m / 明日 9時 / 6/20 21:00 / 月曜 8時", true))
        .add_option(dpp::command_option(dpp::co_string, "message", "お知らせする内容", true)));
    remind.add_option(dpp::command_option(dpp::co_sub_command, "list", "ご予定一覧を表示いたします"));
    remind.add_option(dpp::command_option(dpp::co_sub_command, "cancel", "ご予定を取り消します").add_option(dpp::command_option(dpp::co_integer, "id", "ご予定ID", false).set_min_value(1)));
    remind.add_option(dpp::command_option(dpp::co_sub_command, "help", "時間指定の例を表示いたします"));

    dpp::slashcommand vc("vc", "募集用プライベートVCを作成いたします", application_id);
    vc.add_option(dpp::command_option(dpp::co_string, "game", "ゲーム名。例: valorant", true));
    vc.add_option(dpp::command_option(dpp::co_user, "user1", "メンションするユーザー1", false));
    vc.add_option(dpp::command_option(dpp::co_user, "user2", "メンションするユーザー2", false));
    vc.add_option(dpp::command_option(dpp::co_user, "user3", "メンションするユーザー3", false));
    vc.add_option(dpp::command_option(dpp::co_role, "role", "メンションするロール", false));
    vc.add_option(dpp::command_option(dpp::co_string, "start", "開始時刻。例: 21:00、今から、未定", false));
    vc.add_option(dpp::command_option(dpp::co_string, "note", "任意メッセージ。例: あと1人、アンレ", false));

    dpp::slashcommand room("room", "ご主人様専用のお部屋を開きます", application_id);

    dpp::slashcommand hi("hi", "操作メニューを表示いたします", application_id);
    dpp::slashcommand ping("ping", "Botの応答を確認します", application_id);
    dpp::slashcommand clean("clean", "専用部屋内のBotメッセージを削除します", application_id);
    dpp::slashcommand help("help", "使い方を表示いたします", application_id);
    return {todo, remind, vc, room, hi, ping, clean, help};
}

void register_command_handlers(dpp::cluster& bot, Storage& storage, RoomManager& rooms, HimaVoiceManager& hima) {
    bot.on_slashcommand([&bot, &storage, &rooms, &hima](const dpp::slashcommand_t& event) {
        const std::string command = event.command.get_command_name();
        const std::string subcommand = get_subcommand_name(event);
        const dpp::snowflake user_id{get_user_id(event)};
        const bool master = is_master(event);

        try {
            if (command == "todo") {
                if (subcommand == "add") {
                    const Todo todo = storage.add_todo(user_id, get_string_param(event, "task"));
                    event.reply(polite(master, "承りました。お仕事 `#" + std::to_string(todo.id) + "` を追加しました。", "追加した。"));
                } else if (subcommand == "list") {
                    event.reply(todo_list_response(storage.list_todos(user_id)));
                } else if (subcommand == "done") {
                    const uint64_t id = static_cast<uint64_t>(get_int_param(event, "id"));
                    const std::string result = complete_todo_by_id(storage, user_id, id);
                    event.reply(polite(master, result, result == "そのお仕事は見つかりません。" ? "ない。" : "完了。"));
                } else if (subcommand == "delete") {
                    const uint64_t id = static_cast<uint64_t>(get_int_param(event, "id"));
                    event.reply(storage.delete_todo(user_id, id) ? "お仕事 `#" + std::to_string(id) + "` を削除しました。" : "そのお仕事は見つかりません。");
                } else if (subcommand == "clear") {
                    const size_t removed = storage.clear_done_todos(user_id);
                    event.reply("完了済みを " + std::to_string(removed) + " 件片付けました。");
                } else if (subcommand == "notify") {
                    const std::string interval_text = get_string_param(event, "interval");
                    const int64_t interval = todo_notification_interval(interval_text);
                    storage.set_todo_notification(user_id, interval);
                    if (interval <= 0) {
                        event.reply(polite(master, "TODO通知を止めました。", "止めた。"));
                    } else {
                        event.reply(polite(master, "TODO通知を " + todo_notification_label(interval) + " に設定しました。", "設定した。"));
                    }
                }
            } else if (command == "remind") {
                if (subcommand == "add") {
                    const std::string message = get_string_param(event, "message");
                    const auto due_at = parse_reminder_time(get_string_param(event, "when"));
                    if (!due_at) {
                        const std::string modal_id = create_pending_reminder(user_id, event.command.channel_id, message);
                        show_dialog(event, reminder_time_modal(modal_id));
                        return;
                    }
                    const Reminder reminder = storage.add_reminder_at(user_id, event.command.channel_id, *due_at, message);
                    event.reply(polite(master, "「" + reminder.message + "」を " + format_time(reminder.due_at) + " にお知らせします。", "登録した。"));
                } else if (subcommand == "list") {
                    event.reply(reminder_list_response(storage.list_reminders(user_id)));
                } else if (subcommand == "cancel") {
                    const auto id = get_optional_int_param(event, "id");
                    if (!id) {
                        event.reply(reminder_cancel_response(storage.list_reminders(user_id)));
                    } else {
                        const uint64_t reminder_id = static_cast<uint64_t>(*id);
                        event.reply(storage.cancel_reminder(user_id, reminder_id) ? polite(master, "リマインダーを取り消しました。", "消した。") : polite(master, "そのリマインダーは見つかりません。", "ない。"));
                    }
                } else if (subcommand == "help") {
                    event.reply(reminder_help_text());
                }
            } else if (command == "vc") {
                hima.create_post(
                    bot,
                    event,
                    get_top_level_string_param(event, "game"),
                    mention_target_from_slash(event),
                    get_optional_top_level_string_param(event, "start"),
                    get_optional_top_level_string_param(event, "note"),
                    vc_announce_channel_id(storage, user_id, event.command.guild_id, event.command.channel_id)
                );
            } else if (command == "room") {
                rooms.open_room(bot, event);
            } else if (command == "hi") {
                event.reply(ephemeral_message(main_menu_response()));
            } else if (command == "ping") {
                event.reply(polite(master, "pong!", "pong."));
            } else if (command == "clean") {
                clean_bot_messages_in_private_room(bot, storage, event, user_id);
            } else if (command == "help") {
                event.reply(full_help_text());
            }
        } catch (const std::exception& e) {
            event.reply(std::string("処理に失敗しました: ") + e.what());
        }
    });

    bot.on_button_click([&storage, &bot, &rooms, &hima](const dpp::button_click_t& event) {
        const dpp::snowflake user_id{get_user_id(event)};
        const bool master = is_master(event);
        try {
            if (event.custom_id == "hi:todo") {
                event.reply(ephemeral_message(todo_menu_response()));
            } else if (event.custom_id == "hi:remind") {
                event.reply(ephemeral_message(reminder_menu_response()));
            } else if (event.custom_id == "hi:room") {
                rooms.open_room(bot, event);
            } else if (event.custom_id == "hi:hima") {
                event.reply(ephemeral_message(hima_target_response(event.command.guild_id, event.command.channel_id)));
            } else if (event.custom_id.rfind("hi:hima:none:", 0) == 0) {
                const HimaContext context = hima_context_from_id(event.custom_id, "hi:hima:none:", event.command.guild_id, event.command.channel_id);
                const std::string pending_id = create_pending_hima(user_id, context.guild_id, context.channel_id);
                show_dialog(event, hima_modal(pending_id));
            } else if (event.custom_id == "hi:todo:add") {
                show_dialog(event, todo_add_modal());
            } else if (event.custom_id == "hi:todo:list") {
                event.reply(ephemeral_message(todo_list_response(storage.list_todos(user_id))));
            } else if (event.custom_id == "hi:todo:notify") {
                event.reply(ephemeral_message(todo_notify_menu_response()));
            } else if (event.custom_id.rfind("hi:todo:notify:", 0) == 0) {
                const std::string interval_text = event.custom_id.substr(15);
                const int64_t interval = todo_notification_interval(interval_text);
                storage.set_todo_notification(user_id, interval);
                event.reply(ephemeral_message(interval <= 0 ? "TODO通知を止めました。" : "TODO通知を " + todo_notification_label(interval) + " に設定しました。"));
            } else if (event.custom_id == "hi:remind:add") {
                show_dialog(event, reminder_add_modal());
            } else if (event.custom_id == "hi:remind:list") {
                event.reply(ephemeral_message(reminder_list_response(storage.list_reminders(user_id))));
            } else if (event.custom_id == "hi:remind:cancel") {
                event.reply(ephemeral_message(reminder_cancel_response(storage.list_reminders(user_id))));
            } else if (event.custom_id == "todo:clear_done") {
                const size_t removed = storage.clear_done_todos(user_id);
                event.reply("完了済みを " + std::to_string(removed) + " 件片付けました。");
            } else if (event.custom_id.rfind("todo:done:", 0) == 0) {
                const uint64_t id = std::stoull(event.custom_id.substr(10));
                const std::string result = complete_todo_by_id(storage, user_id, id);
                event.reply(polite(master, result, result == "そのお仕事は見つかりません。" ? "ない。" : "完了。"));
            } else if (event.custom_id.rfind("remind:ack:", 0) == 0) {
                const uint64_t id = std::stoull(event.custom_id.substr(11));
                event.reply(storage.acknowledge_reminder(user_id, id) ? "確認しました。再通知を止めます。" : "確認できるご予定が見つかりません。");
            } else if (event.custom_id.rfind("remind:cancel:", 0) == 0) {
                const uint64_t id = std::stoull(event.custom_id.substr(14));
                event.reply(storage.cancel_reminder(user_id, id) ? "リマインダーを取り消しました。" : "そのリマインダーは見つかりません。");
            } else if (event.custom_id.rfind("hima:join:", 0) == 0) {
                hima.join_post(bot, event, event.custom_id.substr(10));
            }
        } catch (const std::exception& e) {
            event.reply(std::string("ボタン処理に失敗しました: ") + e.what());
        }
    });

    bot.on_select_click([&storage](const dpp::select_click_t& event) {
        const dpp::snowflake user_id{get_user_id(event)};
        try {
            if (event.custom_id.rfind("hi:hima:users:", 0) == 0) {
                const HimaContext context = hima_context_from_id(event.custom_id, "hi:hima:users:", event.command.guild_id, event.command.channel_id);
                std::vector<dpp::snowflake> users;
                for (const std::string& value : event.values) {
                    append_unique(users, dpp::snowflake{std::stoull(value)});
                }
                const std::string pending_id = create_pending_hima(user_id, context.guild_id, context.channel_id, std::move(users));
                show_dialog(event, hima_modal(pending_id));
                return;
            }

            const uint64_t id = selected_id(event.values);
            if (event.custom_id == "todo:done_select") {
                event.reply(complete_todo_by_id(storage, user_id, id));
            } else if (event.custom_id == "todo:delete_select") {
                event.reply(storage.delete_todo(user_id, id) ? "お仕事 `#" + std::to_string(id) + "` を削除しました。" : "そのお仕事は見つかりません。");
            } else if (event.custom_id == "remind:cancel_select") {
                event.reply(storage.cancel_reminder(user_id, id) ? "ご予定 `#" + std::to_string(id) + "` を取り消しました。" : "そのご予定は見つかりません。");
            }
        } catch (const std::exception& e) {
            event.reply(std::string("選択処理に失敗しました: ") + e.what());
        }
    });

    bot.on_form_submit([&storage, &bot, &hima](const dpp::form_submit_t& event) {
        const dpp::snowflake user_id{get_user_id(event)};

        if (event.custom_id == "hi:todo:add_modal") {
            const auto task = modal_value(event, "task");
            if (!task || task->empty()) {
                event.reply(ephemeral_message("内容を入力してください。"));
                return;
            }
            const Todo todo = storage.add_todo(user_id, *task);
            event.reply(ephemeral_message("承りました。お仕事 `#" + std::to_string(todo.id) + "` を追加しました。"));
            return;
        }

        if (event.custom_id == "hi:remind:add_modal") {
            const auto message = modal_value(event, "message");
            const auto when = modal_value(event, "when");
            if (!message || message->empty()) {
                event.reply(ephemeral_message("内容を入力してください。"));
                return;
            }

            const auto due_at = when ? parse_reminder_time(*when) : std::nullopt;
            if (!due_at) {
                const std::string modal_id = create_pending_reminder(user_id, event.command.channel_id, *message);
                show_dialog(event, reminder_time_modal(modal_id));
                return;
            }

            const Reminder reminder = storage.add_reminder_at(user_id, event.command.channel_id, *due_at, *message);
            event.reply(ephemeral_message("「" + reminder.message + "」を " + format_time(reminder.due_at) + " にお知らせします。"));
            return;
        }

        if (event.custom_id.rfind("hi:hima:modal:", 0) == 0) {
            const std::string pending_id = pending_hima_id_from_modal_id(event.custom_id);
            const auto pending = take_pending_hima(pending_id, user_id);
            if (!pending) {
                event.reply(ephemeral_message("入力の有効期限が切れました。もう一度お試しください。"));
                return;
            }
            std::cerr << "Hima modal submit: pending=" << pending_id
                      << " guild=" << static_cast<uint64_t>(pending->guild_id)
                      << " channel=" << static_cast<uint64_t>(pending->channel_id)
                      << " users=" << pending->users.size() << '\n';
            const auto game = modal_value(event, "game");
            const auto start = modal_value(event, "start");
            const auto note = modal_value(event, "note");
            if (!game || game->empty()) {
                event.reply(ephemeral_message("ゲーム名を入力してください。"));
                return;
            }
            std::optional<std::string> start_value;
            if (start && !start->empty() && *start != "なし" && *start != "無し") {
                start_value = *start;
            }
            std::optional<std::string> note_value;
            if (note && !note->empty()) {
                note_value = *note;
            }
            hima.create_post(
                bot,
                event,
                *game,
                mention_target_from_users(pending->users),
                start_value,
                note_value,
                vc_announce_channel_id(storage, user_id, pending->guild_id, pending->channel_id),
                pending->guild_id
            );
            return;
        }

        if (event.custom_id.rfind("remind:time:", 0) != 0) {
            return;
        }

        const auto pending = take_pending_reminder(event.custom_id, user_id);
        if (!pending) {
            event.reply("入力の有効期限が切れました。もう一度お試しください。");
            return;
        }

        const auto when = modal_value(event, "when");
        const auto due_at = when ? parse_reminder_time(*when) : std::nullopt;
        if (!due_at) {
            const std::string modal_id = create_pending_reminder(pending->user_id, pending->channel_id, pending->message);
            event.dialog(reminder_time_modal(modal_id));
            return;
        }

        const Reminder reminder = storage.add_reminder_at(pending->user_id, pending->channel_id, *due_at, pending->message);
        event.reply("「" + reminder.message + "」を " + format_time(reminder.due_at) + " にお知らせします。");
    });

    bot.on_message_create([](const dpp::message_create_t& event) {
        if (event.msg.author.is_bot()) {
            return;
        }

        const bool master = is_master(event);
        const std::string text = event.msg.content;
        if (text == "おはよう" || text == "おはようございます") {
            event.reply(polite(master, "おはようございます、ご主人様。", "おはよう。"));
        } else if (text == "こんにちは" || text == "こんにちわ") {
            event.reply(polite(master, "こんにちは、ご主人様。", "用件は？"));
        } else if (text == "こんばんは") {
            event.reply(polite(master, "こんばんは、ご主人様。", "こんばんは。"));
        } else if (text == "ただいま") {
            event.reply(polite(master, "おかえりなさいませ、ご主人様。", "戻ったの。"));
        } else if (text == "おやすみ") {
            event.reply(polite(master, "おやすみなさいませ、ご主人様。", "はい。"));
        }
    });

    bot.on_message_reaction_add([&storage](const dpp::message_reaction_add_t& event) {
        if (event.reacting_emoji.name != "✅") {
            return;
        }

        const dpp::snowflake user_id{get_user_id(event)};
        storage.acknowledge_reminder_by_message(user_id, event.message_id);
    });
}
