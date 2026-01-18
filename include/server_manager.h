#ifndef DREAMLAND_LOGGER_INCLUDE_SERVER_MANAGER_H
#define DREAMLAND_LOGGER_INCLUDE_SERVER_MANAGER_H

#include <atomic>
#include <chrono>
#include <deque>
#include <io/program.h>
#include <mutex>
#include <player_list.h>
#include <string>
#include <thread>
#include <vector>

namespace dl {

// 日志条目（用于缓存）
struct ServerLogEntry {
	std::string timestamp;
	std::string type; // "join", "leave", "command", "chat"
	std::string player;
	std::string content;
	std::chrono::system_clock::time_point time_point;
};

// OP 信息
struct OpInfo {
	std::string uuid;
	std::string name;
	int level = 4;
	bool bypasses_player_limit = false;
};

// 服务器管理器
class ServerManager {
public:
	// 构造函数
	// @param program: MC服务器
	// @param ops_file: ops.json 文件路径
	// @param player_list: 玩家列表管理器引用
	ServerManager(Program *program,
		const std::string &ops_file,
		PlayerList &player_list);

	~ServerManager();

	// 禁用拷贝
	ServerManager(const ServerManager &) = delete;
	ServerManager &operator=(const ServerManager &) = delete;

	// 启动服务器
	bool start();

	// 停止服务器
	void stop();

	// 是否正在运行
	bool is_running() const {
		return running_.load();
	}

	// 执行命令
	// @param command: 命令内容（不需要以/开头）
	void execute_command(const std::string &command);

	// 获取日志缓存
	// @param limit: 最大返回数量，0表示全部
	std::vector<ServerLogEntry> get_logs(size_t limit = 0) const;

	// 获取 OP 列表
	std::vector<std::string> get_ops() const;

	// 获取 OP 详细信息
	std::vector<OpInfo> get_ops_info() const;

	// 重新加载 ops.json
	void reload_ops();

private:
	// 日志读取线程函数
	void log_reader_thread_func();

	// 加载 ops.json
	void load_ops();

	// 解析 JSON 简单实现（仅用于解析 ops.json）
	static std::vector<OpInfo> parse_ops_json(const std::string &json_content);

	// 添加日志到缓存
	void add_log_entry(const ServerLogEntry &entry);

private:
	std::string server_command_;
	std::string ops_file_;
	PlayerList &player_list_;

	Program *program_;
	std::atomic<bool> running_ { false };

	// 日志缓存
	std::deque<ServerLogEntry> log_cache_;
	mutable std::mutex log_mutex_;
	static constexpr size_t MAX_LOG_CACHE = 1000;

	// OP 列表
	std::vector<OpInfo> ops_;
	mutable std::mutex ops_mutex_;

	// 日志读取线程
	std::thread log_thread_;
	std::atomic<bool> stop_log_thread_ { false };
};

} // namespace dl

#endif // DREAMLAND_LOGGER_INCLUDE_SERVER_MANAGER_H
