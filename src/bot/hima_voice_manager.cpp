#include "hima_voice_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr auto empty_voice_grace = std::chrono::minutes(10);

std::string mention(dpp::snowflake user_id) {
    return "<@" + std::to_string(static_cast<uint64_t>(user_id)) + ">";
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

std::string polite(bool master, const std::string& master_text, const std::string& other_text) {
    return master ? master_text : other_text;
}

} // namespace

uint64_t HimaVoiceManager::slash_user_id(const dpp::slashcommand_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t HimaVoiceManager::form_user_id(const dpp::form_submit_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

uint64_t HimaVoiceManager::button_user_id(const dpp::button_click_t& event) {
    if (event.command.member.user_id != 0) {
        return static_cast<uint64_t>(event.command.member.user_id);
    }
    return static_cast<uint64_t>(event.command.usr.id);
}

std::string HimaVoiceManager::sanitize_channel_name(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == '-' || c == '_' || c == ' ') {
            out += '-';
        }
    }
    out.erase(std::unique(out.begin(), out.end(), [](char a, char b) { return a == '-' && b == '-'; }), out.end());
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) {
        return "game-room";
    }
    return out.substr(0, 90);
}

std::string HimaVoiceManager::next_id() {
    std::lock_guard lock(mutex_);
    return std::to_string(next_id_++);
}

void HimaVoiceManager::grant_access_locked(VoicePost& post, dpp::snowflake user_id) {
    post.allowed_users.insert(static_cast<uint64_t>(user_id));
    post.channel.set_permission_overwrite(
        user_id,
        dpp::ot_member,
        dpp::p_view_channel | dpp::p_connect | dpp::p_speak,
        0
    );
}

void HimaVoiceManager::remember_post(const std::string& id, VoicePost post) {
    std::lock_guard lock(mutex_);
    posts_[id] = std::move(post);
}

void HimaVoiceManager::create_post_for(dpp::cluster& bot, dpp::snowflake host_id, dpp::snowflake guild_id, dpp::snowflake announce_channel_id, const std::string& game, VcMentionTarget target, const std::optional<std::string>& start_time, const std::optional<std::string>& note) {
    const std::string id = next_id();
    dpp::channel voice;
    voice.guild_id = guild_id;
    voice.name = sanitize_channel_name(game);
    voice.set_type(dpp::CHANNEL_VOICE);
    voice.add_permission_overwrite(guild_id, dpp::ot_role, 0, dpp::p_view_channel | dpp::p_connect);
    voice.add_permission_overwrite(host_id, dpp::ot_member, dpp::p_view_channel | dpp::p_connect | dpp::p_speak, 0);
    voice.add_permission_overwrite(bot.me.id, dpp::ot_member, dpp::p_view_channel | dpp::p_connect | dpp::p_speak | dpp::p_manage_channels | dpp::p_move_members, 0);

    bot.channel_create(voice, [this, &bot, id, host_id, guild_id, announce_channel_id, game, target = std::move(target), start_time, note](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            bot.message_create(dpp::message(announce_channel_id, "募集VCの作成に失敗しました。"));
            return;
        }

        dpp::channel created = callback.get<dpp::channel>();
        VoicePost post;
        post.channel = created;
        post.host_id = host_id;
        post.guild_id = guild_id;
        post.game = game;
        post.start_time = start_time.value_or("");
        post.note = note.value_or("");
        post.empty_since = std::chrono::steady_clock::now();
        grant_access_locked(post, host_id);
        remember_post(id, std::move(post));

        std::string text;
        if (!target.text.empty()) {
            text += target.text + "\n";
        }
        text += mention(host_id) + " 様から募集でございます。\n";
        text += "ゲーム: " + game;
        if (start_time && !start_time->empty()) {
            text += "\n開始: " + *start_time;
        }
        if (note && !note->empty()) {
            text += "\nメッセージ: " + *note;
        }
        text += "\n参加ボタンでVC権限を付与します。";

        dpp::message msg(announce_channel_id, text);
        msg.set_allowed_mentions(false, false, false, false, target.users, target.roles);
        msg.add_component(dpp::component().add_component(
            dpp::component().set_label("参加").set_id("hima:join:" + id).set_style(dpp::cos_success)
        ));
        bot.message_create(msg);
    });
}

void HimaVoiceManager::create_post(dpp::cluster& bot, const dpp::slashcommand_t& event, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id) {
    const dpp::snowflake host_id{slash_user_id(event)};
    const dpp::snowflake guild_id{event.command.guild_id};
    const bool master = has_master_role(event.command.member);
    if (guild_id == 0) {
        event.reply(polite(master, "募集VCはサーバー内でのみ作成できます。", "ここでは無理。"));
        return;
    }

    event.reply(polite(master, "募集VCを作成します。", "作る。"));
    create_post_for(bot, host_id, guild_id, announce_channel_id == 0 ? event.command.channel_id : announce_channel_id, game, target, start_time, note);
}

void HimaVoiceManager::create_post(dpp::cluster& bot, const dpp::form_submit_t& event, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id) {
    const dpp::snowflake host_id{form_user_id(event)};
    const dpp::snowflake guild_id{event.command.guild_id};
    const bool master = has_master_role(event.command.member);
    if (guild_id == 0) {
        event.reply(polite(master, "募集VCはサーバー内でのみ作成できます。", "ここでは無理。"));
        return;
    }

    event.reply(polite(master, "募集VCを作成します。", "作る。"));
    create_post_for(bot, host_id, guild_id, announce_channel_id == 0 ? event.command.channel_id : announce_channel_id, game, target, start_time, note);
}

void HimaVoiceManager::create_post(dpp::cluster& bot, const dpp::message& message, const std::string& game, const VcMentionTarget& target, const std::optional<std::string>& start_time, const std::optional<std::string>& note, dpp::snowflake announce_channel_id) {
    const dpp::snowflake host_id{message.author.id};
    const dpp::snowflake guild_id{message.guild_id};
    const bool master = has_master_role(message.member);
    if (guild_id == 0) {
        bot.message_create(dpp::message(message.channel_id, polite(master, "募集VCはサーバー内でのみ作成できます。", "ここでは無理。")));
        return;
    }

    bot.message_create(dpp::message(message.channel_id, polite(master, "募集VCを作成します。", "作る。")));
    create_post_for(bot, host_id, guild_id, announce_channel_id == 0 ? message.channel_id : announce_channel_id, game, target, start_time, note);
}

void HimaVoiceManager::join_post(dpp::cluster& bot, const dpp::button_click_t& event, const std::string& id) {
    const dpp::snowflake user_id{button_user_id(event)};
    const bool master = has_master_role(event.command.member);
    dpp::channel updated_channel;

    {
        std::lock_guard lock(mutex_);
        const auto it = posts_.find(id);
        if (it == posts_.end()) {
            event.reply(polite(master, "この募集は見つかりません。", "もうない。"));
            return;
        }

        grant_access_locked(it->second, user_id);
        updated_channel = it->second.channel;
    }

    bot.channel_edit(updated_channel);
    event.reply(polite(master, mention(user_id) + " 様、VC権限を付与しました。", "入れるようにした。"));
}

void HimaVoiceManager::handle_voice_state(const dpp::voice_state_update_t& event) {
    const uint64_t user_id = static_cast<uint64_t>(event.state.user_id);
    const dpp::snowflake new_channel_id = event.state.channel_id;
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);
    const auto old_it = user_voice_channels_.find(user_id);
    if (old_it != user_voice_channels_.end()) {
        for (auto& [_, post] : posts_) {
            if (post.channel.id == old_it->second) {
                post.occupants.erase(user_id);
                if (post.occupants.empty()) {
                    post.empty_since = now;
                }
                break;
            }
        }
    }

    if (new_channel_id != 0) {
        user_voice_channels_[user_id] = new_channel_id;
        for (auto& [_, post] : posts_) {
            if (post.channel.id == new_channel_id) {
                post.occupants.insert(user_id);
                break;
            }
        }
    } else {
        user_voice_channels_.erase(user_id);
    }
}

void HimaVoiceManager::start_cleanup_worker(dpp::cluster& bot, std::atomic_bool& running) {
    std::thread([this, &bot, &running] {
        while (running.load()) {
            std::vector<dpp::snowflake> channels_to_delete;
            const auto now = std::chrono::steady_clock::now();

            {
                std::lock_guard lock(mutex_);
                for (auto it = posts_.begin(); it != posts_.end();) {
                    if (it->second.occupants.empty() && now - it->second.empty_since >= empty_voice_grace) {
                        channels_to_delete.push_back(it->second.channel.id);
                        it = posts_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            for (const dpp::snowflake channel_id : channels_to_delete) {
                bot.channel_delete(channel_id);
            }

            for (int i = 0; i < 60 && running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }).detach();
}
