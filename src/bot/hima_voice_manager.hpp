#pragma once

#include <atomic>
#include <chrono>
#include <dpp/dpp.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct VcMentionTarget {
    std::string text;
    std::vector<dpp::snowflake> users;
    std::vector<dpp::snowflake> roles;
};

class HimaVoiceManager {
public:
    void create_post(dpp::cluster& bot, const dpp::slashcommand_t& event, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id = {});
    void create_post(dpp::cluster& bot, const dpp::form_submit_t& event, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id = {});
    void create_post(dpp::cluster& bot, const dpp::message& message, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id = {});
    void join_post(dpp::cluster& bot, const dpp::button_click_t& event, const std::string& id);
    void handle_voice_state(const dpp::voice_state_update_t& event);
    void start_cleanup_worker(dpp::cluster& bot, std::atomic_bool& running);

private:
    struct VoicePost {
        dpp::channel channel;
        dpp::snowflake host_id{};
        dpp::snowflake guild_id{};
        std::string game;
        std::string start_time;
        std::string note;
        std::unordered_set<uint64_t> allowed_users;
        std::unordered_set<uint64_t> occupants;
        std::chrono::steady_clock::time_point empty_since{};
    };

    static std::string sanitize_channel_name(const std::string& value);
    static uint64_t slash_user_id(const dpp::slashcommand_t& event);
    static uint64_t form_user_id(const dpp::form_submit_t& event);
    static uint64_t button_user_id(const dpp::button_click_t& event);

    std::string next_id();
    void create_post_for(dpp::cluster& bot, dpp::snowflake host_id, dpp::snowflake guild_id, dpp::snowflake announce_channel_id, const std::string& game, VcMentionTarget target, const std::optional<std::string>& start_time, const std::optional<std::string>& note);
    void remember_post(const std::string& id, VoicePost post);
    void grant_access_locked(VoicePost& post, dpp::snowflake user_id);

    std::mutex mutex_;
    std::unordered_map<std::string, VoicePost> posts_;
    std::unordered_map<uint64_t, dpp::snowflake> user_voice_channels_;
    uint64_t next_id_{1};
};
