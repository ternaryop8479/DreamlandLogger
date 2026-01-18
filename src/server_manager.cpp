#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <server_manager.h>
#include <sstream>

namespace dl {

// ============================================================================
// 辅助函数
// ============================================================================

static std::string get_current_time_string() {
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);
	std::tm tm {};
	localtime_r(&t, &tm);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

static std::string trim(const std::string &s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// ============================================================================
// ServerManager 实现
// ============================================================================

ServerManager::ServerManager(Program *program,
	const std::string &ops_file,
	PlayerList &player_list) : ops_file_(ops_file)
							 , player_list_(player_list)
							 , program_(program) {

	// 加载 ops.json
	load_ops();
}

ServerManager::~ServerManager() {
	stop();
}

bool ServerManager::start() {
	if (running_)
		return false;

	std::cout << "[ServerManager] 正在启动 MC 服务器..." << std::endl;

	if (!program_->run()) {
		std::cerr << "[ServerManager] 启动失败" << std::endl;
		return false;
	}

	running_ = true;

	// 启动日志读取线程
	stop_log_thread_ = false;
	log_thread_ = std::thread(&ServerManager::log_reader_thread_func, this);

	std::cout << "[ServerManager] MC 服务器已启动" << std::endl;
	return true;
}

void ServerManager::stop() {
	if (!running_)
		return;

	std::cout << "[ServerManager] 正在停止 MC 服务器..." << std::endl;

	// 停止日志线程
	stop_log_thread_ = true;
	if (log_thread_.joinable()) {
		log_thread_.join();
	}

	// 停止服务器
	program_->stop();
	running_ = false;

	std::cout << "[ServerManager] MC 服务器已停止" << std::endl;
}

void ServerManager::execute_command(const std::string &command) {
	if (!running_) {
		std::cerr << "[ServerManager] 服务器未运行，无法执行命令" << std::endl;
		return;
	}

	std::string cmd = command;
	// 移除开头的 / （如果有）
	if (!cmd.empty() && cmd[0] == '/') {
		cmd = cmd.substr(1);
	}

	program_->send_string(cmd + "\n");
	std::cout << "[ServerManager] 执行命令: " << cmd << std::endl;
}

std::vector<ServerLogEntry> ServerManager::get_logs(size_t limit) const {
	std::lock_guard<std::mutex> lock(log_mutex_);

	if (limit == 0 || limit >= log_cache_.size()) {
		return std::vector<ServerLogEntry>(log_cache_.begin(), log_cache_.end());
	}

	// 返回最新的 limit 条
	auto start_it = log_cache_.end() - limit;
	return std::vector<ServerLogEntry>(start_it, log_cache_.end());
}

std::vector<std::string> ServerManager::get_ops() const {
	std::lock_guard<std::mutex> lock(ops_mutex_);

	std::vector<std::string> result;
	result.reserve(ops_.size());
	for (const auto &op : ops_) {
		result.push_back(op.name);
	}
	return result;
}

std::vector<OpInfo> ServerManager::get_ops_info() const {
	std::lock_guard<std::mutex> lock(ops_mutex_);
	return ops_;
}

void ServerManager::reload_ops() {
	load_ops();
	std::cout << "[ServerManager] 重新加载 ops.json，共 " << ops_.size() << " 个 OP" << std::endl;
}

// ============================================================================
// 日志读取线程
// ============================================================================

void ServerManager::log_reader_thread_func() {
	while (!stop_log_thread_) {
		if (!program_->is_running()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		// 读取一行日志
		std::string line = program_->read_string(true, Program::IOStreamType::STDOUT);

		if (line.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}

		// 处理日志行
		auto event = player_list_.process_log_line(line);

		// 创建日志条目
		ServerLogEntry entry;
		entry.time_point = event.timestamp;
		entry.timestamp = get_current_time_string();

		switch (event.type) {
		case LogEventType::PLAYER_JOIN:
			entry.type = "join";
			entry.player = event.player_name;
			entry.content = event.client_info;

			std::cout << "[" << entry.timestamp << "] 玩家 [" << event.player_name
					  << "] 加入了服务器，客户端为 [" << event.client_info << "]" << std::endl;

			add_log_entry(entry);
			break;

		case LogEventType::PLAYER_LEAVE:
			entry.type = "leave";
			entry.player = event.player_name;
			entry.content = "";

			std::cout << "[" << entry.timestamp << "] 玩家 [" << event.player_name
					  << "] 退出了服务器" << std::endl;

			add_log_entry(entry);
			break;

		case LogEventType::PLAYER_COMMAND:
			entry.type = "command";
			entry.player = event.player_name;
			entry.content = event.content;

			std::cout << "[" << entry.timestamp << "] 玩家 [" << event.player_name
					  << "] 执行了操作 [" << event.content << "]" << std::endl;

			add_log_entry(entry);
			break;

		case LogEventType::PLAYER_CHAT:
			entry.type = "chat";
			entry.player = event.player_name;
			entry.content = event.content;

			std::cout << "[" << entry.timestamp << "] <" << event.player_name << "> "
					  << event.content << std::endl;

			add_log_entry(entry);
			break;

		default:
			// 其他类型的日志，直接输出但不缓存
			if (!line.empty()) {
				std::cout << line << std::endl;
			}
			break;
		}
	}
}

void ServerManager::add_log_entry(const ServerLogEntry &entry) {
	std::lock_guard<std::mutex> lock(log_mutex_);

	log_cache_.push_back(entry);

	// 限制缓存大小
	if (log_cache_.size() > MAX_LOG_CACHE) {
		log_cache_.pop_front();
	}
}

// ============================================================================
// ops.json 解析
// ============================================================================

void ServerManager::load_ops() {
	std::ifstream file(ops_file_);
	if (!file) {
		std::cerr << "[ServerManager] 无法打开 ops.json: " << ops_file_ << std::endl;
		return;
	}

	std::ostringstream oss;
	oss << file.rdbuf();
	std::string json_content = oss.str();

	std::lock_guard<std::mutex> lock(ops_mutex_);
	ops_ = parse_ops_json(json_content);

	std::cout << "[ServerManager] 加载了 " << ops_.size() << " 个 OP" << std::endl;
}

std::vector<OpInfo> ServerManager::parse_ops_json(const std::string &json_content) {
	std::vector<OpInfo> result;

	// 简单的 JSON 解析（仅适用于 ops.json 格式）
	// 格式: [{"uuid":"...", "name":"...", "level":4, "bypassesPlayerLimit":false}, ...]

	size_t pos = 0;
	while (true) {
		// 查找下一个对象
		pos = json_content.find("{", pos);
		if (pos == std::string::npos)
			break;

		size_t end = json_content.find("}", pos);
		if (end == std::string::npos)
			break;

		std::string obj = json_content.substr(pos, end - pos + 1);

		OpInfo info;

		// 提取 uuid
		size_t uuid_pos = obj.find("\"uuid\"");
		if (uuid_pos != std::string::npos) {
			size_t start = obj.find("\"", uuid_pos + 6);
			size_t end_pos = obj.find("\"", start + 1);
			if (start != std::string::npos && end_pos != std::string::npos) {
				info.uuid = obj.substr(start + 1, end_pos - start - 1);
			}
		}

		// 提取 name
		size_t name_pos = obj.find("\"name\"");
		if (name_pos != std::string::npos) {
			size_t start = obj.find("\"", name_pos + 6);
			size_t end_pos = obj.find("\"", start + 1);
			if (start != std::string::npos && end_pos != std::string::npos) {
				info.name = obj.substr(start + 1, end_pos - start - 1);
			}
		}

		// 提取 level
		size_t level_pos = obj.find("\"level\"");
		if (level_pos != std::string::npos) {
			size_t colon = obj.find(":", level_pos);
			if (colon != std::string::npos) {
				std::string level_str;
				for (size_t i = colon + 1; i < obj.size(); ++i) {
					if (isdigit(obj[i])) {
						level_str += obj[i];
					} else if (!level_str.empty()) {
						break;
					}
				}
				if (!level_str.empty()) {
					info.level = std::stoi(level_str);
				}
			}
		}

		// 提取 bypassesPlayerLimit
		size_t bypass_pos = obj.find("\"bypassesPlayerLimit\"");
		if (bypass_pos != std::string::npos) {
			size_t true_pos = obj.find("true", bypass_pos);
			size_t false_pos = obj.find("false", bypass_pos);
			if (true_pos != std::string::npos && (false_pos == std::string::npos || true_pos < false_pos)) {
				info.bypasses_player_limit = true;
			}
		}

		if (!info.name.empty()) {
			result.push_back(info);
		}

		pos = end + 1;
	}

	return result;
}

} // namespace dl
