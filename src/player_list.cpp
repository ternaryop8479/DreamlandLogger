#include <player_list.h>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace dl {

// ============================================================================
// 辅助函数
// ============================================================================

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string remove_spaces(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t') {
            result += c;
        }
    }
    return result;
}

// 去除 ANSI 转义序列 (终端控制符)
// 格式: ESC[ ... m  或  [ 数字;数字 m
static std::string remove_ansi(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    
    size_t i = 0;
    while (i < s.size()) {
        // 检查 ESC 字符 (\x1b 或 \033，ASCII 27)
        if (s[i] == '\x1b') {
            if (i + 1 < s.size() && s[i + 1] == '[') {
                // 跳过 ESC[
                i += 2;
                // 跳过数字和分号
                while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == ';')) {
                    i++;
                }
                // 跳过结束字母 (通常是 m)
                if (i < s.size() && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
                    i++;
                }
                continue;
            }
            // 单独的 ESC，跳过
            i++;
            continue;
        }
        
        // 检查可能 ESC 被过滤后只剩 [数字;数字m] 的情况
        if (s[i] == '[') {
            size_t j = i + 1;
            // 必须以数字开头才认为是 ANSI 序列
            if (j < s.size() && s[j] >= '0' && s[j] <= '9') {
                // 跳过数字和分号
                while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')) {
                    j++;
                }
                // 必须以 m 结尾
                if (j < s.size() && s[j] == 'm') {
                    i = j + 1;
                    continue;
                }
            }
        }
        
        result += s[i];
        i++;
    }
    
    return result;
}

static std::string time_to_string(const std::chrono::system_clock::time_point& tp, bool permanent = false) {
    if (permanent) return "0000-00-00 00:00:00";
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::chrono::system_clock::time_point string_to_time(const std::string& s) {
    if (s == "0000-00-00 00:00:00") {
        return std::chrono::system_clock::time_point::max();
    }
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::chrono::system_clock::from_time_t(mktime(&tm));
}

static std::chrono::system_clock::time_point parse_log_time(const std::string& line) {
    auto now = std::chrono::system_clock::now();
    size_t start = line.find('[');
    size_t end = line.find(' ', start);
    if (start == std::string::npos || end == std::string::npos) return now;
    
    std::string time_str = line.substr(start + 1, end - start - 1);
    int h, m, s;
    if (sscanf(time_str.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return now;
    
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = s;
    return std::chrono::system_clock::from_time_t(mktime(&tm));
}

std::string BannedPlayerInfo::get_ban_time_string() const {
    auto time_t_val = std::chrono::system_clock::to_time_t(ban_time);
    std::tm tm_val{};
    localtime_r(&time_t_val, &tm_val);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string BannedPlayerInfo::get_unban_time_string() const {
    auto time_t_val = std::chrono::system_clock::to_time_t(unban_time);
    std::tm tm_val{};
    localtime_r(&time_t_val, &tm_val);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ============================================================================
// PlayerList 实现
// ============================================================================

std::vector<BannedPlayerInfo> PlayerList::list_banned_player_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BannedPlayerInfo> result;
    result.reserve(banned_players_.size());
    for (const auto& p : banned_players_) {
        result.push_back(p.second);
    }
    return result;
}

PlayerList::PlayerList(const std::string& player_file,
                       const std::string& banned_file,
                       const std::string& forbidden_cmd_file,
                       const Program& program)
    : player_file_(player_file)
    , banned_file_(banned_file)
    , forbidden_file_(forbidden_cmd_file)
    , program_(program) {
    load_files();
    checker_thread_ = std::thread(&PlayerList::ban_checker_thread_func, this);
}

PlayerList::~PlayerList() {
    {
        std::lock_guard<std::mutex> lock(checker_mutex_);
        stop_checker_ = true;
    }
    checker_cv_.notify_all();
    if (checker_thread_.joinable()) {
        checker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    online_players_.clear();
    save_files();
}

// ============================================================================
// 日志处理
// ============================================================================

LogEvent PlayerList::process_log_line(const std::string& log_line) {
    LogEvent event;
    
    // 首先清理 ANSI 转义序列
    std::string clean_line = remove_ansi(log_line);
    
    event.timestamp = parse_log_time(clean_line);
    
    size_t content_start = clean_line.find("]: ");
    if (content_start == std::string::npos) return event;
    std::string content = clean_line.substr(content_start + 3);
    
    // =========== 玩家加入 (Leaves/Carpet等) ===========
    size_t pos = content.find(" joined with ");
    if (pos != std::string::npos) {
        size_t player_pos = content.rfind("Player ", pos);
        if (player_pos != std::string::npos) {
            size_t name_start = player_pos + 7;
            event.player_name = trim(content.substr(name_start, pos - name_start));
            event.client_info = trim(content.substr(pos + 13));
            event.type = LogEventType::PLAYER_JOIN;
            
            // 去除结尾 \n
            while (!event.client_info.empty() && event.client_info.back() == '\n') {
                event.client_info.pop_back();
            }
            
            std::lock_guard<std::mutex> lock(mutex_);
            all_players_.insert(event.player_name);
            online_players_[event.player_name] = {event.player_name, event.timestamp, event.client_info};
            return event;
        }
    }
    
    // =========== 玩家加入 (原版) ===========
    pos = content.find(" joined the game");
    if (pos != std::string::npos) {
        event.player_name = trim(content.substr(0, pos));
        event.client_info = "vanilla";
        event.type = LogEventType::PLAYER_JOIN;
        
        std::lock_guard<std::mutex> lock(mutex_);
        all_players_.insert(event.player_name);
        online_players_[event.player_name] = {event.player_name, event.timestamp, event.client_info};
        return event;
    }
    
    // =========== 玩家离开 ===========
    pos = content.find(" left the game");
    if (pos != std::string::npos) {
        event.player_name = trim(content.substr(0, pos));
        event.type = LogEventType::PLAYER_LEAVE;
        
        std::lock_guard<std::mutex> lock(mutex_);
        online_players_.erase(event.player_name);
        return event;
    }
    
    // =========== 玩家执行指令 ===========
    pos = content.find(" issued server command: /");
    if (pos != std::string::npos) {
        event.player_name = trim(content.substr(0, pos));
        event.content = content.substr(pos + 25);
        event.type = LogEventType::PLAYER_COMMAND;
        
        // 去除结尾 \n
        while (!event.content.empty() && event.content.back() == '\n') {
            event.content.pop_back();
        }
        
        // 去掉空格并转小写后匹配关键词
        std::string match_str = to_lower(remove_spaces(content));
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& fc : forbidden_commands_) {
            std::string keyword = to_lower(remove_spaces(fc.command));
            if (match_str.find(keyword) != std::string::npos) {
                auto unban_time = std::chrono::system_clock::now() + std::chrono::hours(fc.ban_hours);
                std::string time_str = time_to_string(unban_time);
                
                std::string reason = "执行被禁止的指令: /" + event.content + 
                    ", 将被" + 
                    (fc.ban_hours != 0 ? "封禁至" + time_str + "。" : "永久封禁。") + 
                    "有异议请在服务器管理网站提出解封申请。";
                
                mutex_.unlock();
                ban(event.player_name, reason, fc.ban_hours);
                mutex_.lock();
                break;
            }
        }
		event.content = '/' + event.content;
        return event;
    }
    
    // =========== 玩家操作行（F3+F4等） ===========
    if (!content.empty() && content[0] == '[') {
        size_t end_bracket = content.find(']');
        size_t colon_pos = content.find(':');
        if (end_bracket != std::string::npos && colon_pos != std::string::npos && colon_pos < end_bracket) {
            std::string bracket_content = content.substr(1, end_bracket - 1);
            
            // 去掉空格并转小写后匹配关键词
            std::string match_str = to_lower(remove_spaces(bracket_content));
            
            // 查找所有玩家中第一个出现在这行的玩家
            std::string found_player;
            size_t earliest_pos = std::string::npos;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& player : all_players_) {
                    size_t player_pos = content.find(player);
                    if (player_pos != std::string::npos && player_pos < earliest_pos) {
                        earliest_pos = player_pos;
                        found_player = player;
                    }
                }
            }
            
            // 无论是否匹配禁止关键词，都返回事件
            event.type = LogEventType::PLAYER_COMMAND;
            event.player_name = found_player;
            event.content = bracket_content;
            
            // 去除结尾 \n
            while (!event.content.empty() && event.content.back() == '\n') {
                event.content.pop_back();
            }
            
            // 检查禁止关键词并封禁
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& fc : forbidden_commands_) {
                std::string keyword = to_lower(remove_spaces(fc.command));
                if (match_str.find(keyword) != std::string::npos && !found_player.empty()) {
                    auto unban_time = std::chrono::system_clock::now() + std::chrono::hours(fc.ban_hours);
                    std::string time_str = time_to_string(unban_time);
                    
                    std::string reason = "执行被禁止的操作: [" + event.content + "], 将被" + 
                        (fc.ban_hours != 0 ? "封禁至" + time_str + "。" : "永久封禁。") + 
                        "有异议请在服务器管理网站提出解封申请。";
                    
                    mutex_.unlock();
                    ban(found_player, reason, fc.ban_hours);
                    mutex_.lock();
                    break;
                }
            }
			event.content = '[' + event.content + ']';
            return event;
        }
    }
    
    // =========== 玩家聊天 ===========
    if (!content.empty() && content[0] == '<') {
        size_t end = content.find('>');
        if (end != std::string::npos) {
            event.player_name = content.substr(1, end - 1);
            event.content = trim(content.substr(end + 1));
            event.type = LogEventType::PLAYER_CHAT;
            
            // 去除结尾 \n
            while (!event.content.empty() && event.content.back() == '\n') {
                event.content.pop_back();
            }
            
            return event;
        }
    }
    
    return event;
}

// ============================================================================
// 封禁/解封
// ============================================================================

bool PlayerList::ban(const std::string& player, const std::string& reason, uint64_t banned_hours) {
    BannedPlayerInfo info;
    info.name = player;
    info.reason = reason;
    info.ban_time = std::chrono::system_clock::now();
    info.is_permanent = (banned_hours == 0);
    
    if (info.is_permanent) {
        info.unban_time = std::chrono::system_clock::time_point::max();
    } else {
        info.unban_time = info.ban_time + std::chrono::hours(banned_hours);
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        banned_players_[player] = info;
    }

	std::cout << "[PlayerList] 封禁玩家: " << player 
			  << ", 原因: " << reason 
			  << ", 时长: " << (banned_hours == 0 ? "永久" : std::to_string(banned_hours) + "小时") 
			  << std::endl;
    
    const_cast<Program&>(program_).send_string("ban " + player + " " + reason + "\n");
    save_files();
    return true;
}

bool PlayerList::pardon(const std::string& player) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (banned_players_.erase(player) == 0) return false;
    }
    
    const_cast<Program&>(program_).send_string("pardon " + player + "\n");
    save_files();
    return true;
}

// ============================================================================
// 查询
// ============================================================================

std::vector<std::string> PlayerList::list_player() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {all_players_.begin(), all_players_.end()};
}

std::vector<std::string> PlayerList::list_banned_player() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& p : banned_players_) {
        result.push_back(p.first);
    }
    return result;
}

std::vector<OnlinePlayerInfo> PlayerList::list_online_player() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OnlinePlayerInfo> result;
    for (const auto& p : online_players_) {
        result.push_back(p.second);
    }
    return result;
}

bool PlayerList::is_banned(const std::string& player) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return banned_players_.count(player) > 0;
}

bool PlayerList::is_online(const std::string& player) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return online_players_.count(player) > 0;
}

bool PlayerList::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    save_files();
    return true;
}

// ============================================================================
// 文件操作
// ============================================================================

void PlayerList::load_files() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream pf(player_file_);
    if (pf) {
        std::string line;
        while (std::getline(pf, line)) {
            line = trim(line);
            if (!line.empty()) all_players_.insert(line);
        }
    } else {
        std::ofstream(player_file_);
    }
    
    std::ifstream bf(banned_file_);
    if (bf) {
        std::string line;
        while (std::getline(bf, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            size_t p3 = line.find('|', p2 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) continue;
            
            BannedPlayerInfo info;
            info.name = line.substr(0, p1);
            info.reason = line.substr(p1 + 1, p2 - p1 - 1);
            info.ban_time = string_to_time(line.substr(p2 + 1, p3 - p2 - 1));
            std::string unban_str = line.substr(p3 + 1);
            info.is_permanent = (unban_str == "0000-00-00 00:00:00");
            info.unban_time = string_to_time(unban_str);
            banned_players_[info.name] = info;
        }
    } else {
        std::ofstream(banned_file_);
    }
    
    std::ifstream ff(forbidden_file_);
    if (ff) {
        std::string line;
        while (std::getline(ff, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            std::string keyword;
            uint64_t hours = 0;
            if (!(iss >> keyword >> hours)) continue;
            
            if (!keyword.empty() && keyword[0] == '/') keyword = keyword.substr(1);
            
            forbidden_commands_.push_back({keyword, hours});
        }
    } else {
        std::ofstream(forbidden_file_);
    }
}

void PlayerList::save_files() const {
    std::ofstream pf(player_file_);
    for (const auto& p : all_players_) {
        pf << p << "\n";
    }
    
    std::ofstream bf(banned_file_);
    bf << "# name|reason|ban_time|unban_time\n";
    for (const auto& p : banned_players_) {
        const auto& info = p.second;
        bf << info.name << "|" << info.reason << "|"
           << time_to_string(info.ban_time) << "|"
           << time_to_string(info.unban_time, info.is_permanent) << "\n";
    }
}

// ============================================================================
// 封禁检查线程
// ============================================================================

void PlayerList::ban_checker_thread_func() {
    while (true) {
        std::unique_lock<std::mutex> lock(checker_mutex_);
        checker_cv_.wait_for(lock, std::chrono::seconds(30), [this] {
            return stop_checker_.load();
        });
        
        if (stop_checker_) break;
        
        std::vector<std::string> to_unban;
        {
            std::lock_guard<std::mutex> data_lock(mutex_);
            auto now = std::chrono::system_clock::now();
            for (const auto& p : banned_players_) {
                if (!p.second.is_permanent && now >= p.second.unban_time) {
                    to_unban.push_back(p.first);
                }
            }
        }
        
        for (const auto& player : to_unban) {
            pardon(player);
            std::cout << "[PlayerList] 自动解封: " << player << std::endl;
        }
    }
}

} // namespace dl
