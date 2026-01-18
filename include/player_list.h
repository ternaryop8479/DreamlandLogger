#ifndef DREAMLAND_LOGGER_INCLUDE_PLAYER_LIST_H
#define DREAMLAND_LOGGER_INCLUDE_PLAYER_LIST_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <io/program.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dl {

// 在线玩家信息
struct OnlinePlayerInfo {
	std::string name;
	std::chrono::system_clock::time_point join_time;
	std::string client_info;
};

// 被封禁玩家信息
struct BannedPlayerInfo {
	std::string name;
	std::string reason;
	std::chrono::system_clock::time_point ban_time;
	std::chrono::system_clock::time_point unban_time;
	bool is_permanent = false;
	std::string get_ban_time_string() const;
	std::string get_unban_time_string() const;
};

// 禁止指令信息
struct ForbiddenCommand {
	std::string command;
	uint64_t ban_hours;
};

// 日志事件类型
enum class LogEventType {
	NONE,
	PLAYER_JOIN,
	PLAYER_LEAVE,
	PLAYER_COMMAND,
	PLAYER_CHAT
};

// 日志事件
struct LogEvent {
	LogEventType type = LogEventType::NONE;
	std::string player_name;
	std::string content;
	std::string client_info;
	std::chrono::system_clock::time_point timestamp;
};

class PlayerList {
public:
	PlayerList(const std::string &player_file,
		const std::string &banned_file,
		const std::string &forbidden_cmd_file,
		const Program &program);
	~PlayerList();

	PlayerList(const PlayerList &) = delete;
	PlayerList &operator=(const PlayerList &) = delete;

	LogEvent process_log_line(const std::string &log_line);

	bool ban(const std::string &player, const std::string &reason, uint64_t banned_hours);
	bool pardon(const std::string &player);

	std::vector<std::string> list_player() const;
	std::vector<std::string> list_banned_player() const;
	std::vector<OnlinePlayerInfo> list_online_player() const;

	bool is_banned(const std::string &player) const;
	bool is_online(const std::string &player) const;
	bool save() const;

	// 在 PlayerList 类中添加:
	std::vector<BannedPlayerInfo> list_banned_player_info() const;

	void set_program(const Program &program);

private:
	void load_files();
	void save_files() const;
	void ban_checker_thread_func();

	std::string player_file_;
	std::string banned_file_;
	std::string forbidden_file_;
	const Program &program_;

	std::unordered_set<std::string> all_players_;
	std::unordered_map<std::string, OnlinePlayerInfo> online_players_;
	std::unordered_map<std::string, BannedPlayerInfo> banned_players_;
	std::vector<ForbiddenCommand> forbidden_commands_;

	mutable std::mutex mutex_;
	std::thread checker_thread_;
	std::atomic<bool> stop_checker_ { false };
	std::condition_variable checker_cv_;
	std::mutex checker_mutex_;
};

} // namespace dl

#endif
