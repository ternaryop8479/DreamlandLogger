#include <command_request.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace dl {

// ============================================================================
// 辅助函数
// ============================================================================

static std::string time_to_string(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::chrono::system_clock::time_point string_to_time(const std::string& s) {
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return std::chrono::system_clock::now();
    }
    return std::chrono::system_clock::from_time_t(mktime(&tm));
}

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

// ============================================================================
// RequestInfo 实现
// ============================================================================

std::string RequestInfo::get_created_time_string() const {
    return time_to_string(created_at);
}

std::string RequestInfo::get_executed_time_string() const {
    if (!executed) return "";
    return time_to_string(executed_at);
}

// ============================================================================
// CommandRequestManager 实现
// ============================================================================

CommandRequestManager::CommandRequestManager(const std::string& data_file,
                                             const std::string& upload_dir,
                                             size_t vote_threshold,
                                             CommandExecuteCallback execute_callback)
    : data_file_(data_file)
    , upload_dir_(upload_dir)
    , vote_threshold_(vote_threshold)
    , execute_callback_(std::move(execute_callback)) {
    
    // 确保上传目录存在
    std::filesystem::create_directories(upload_dir_);
    
    // 加载数据
    load_data();
    
    // 启动检查线程
    checker_thread_ = std::thread(&CommandRequestManager::checker_thread_func, this);
}

CommandRequestManager::~CommandRequestManager() {
    // 停止检查线程
    {
        std::lock_guard<std::mutex> lock(checker_mutex_);
        stop_checker_ = true;
    }
    checker_cv_.notify_all();
    
    if (checker_thread_.joinable()) {
        checker_thread_.join();
    }
    
    // 保存数据
    save_data();
}

std::string CommandRequestManager::generate_id() {
    // 使用时间戳 + 随机数生成ID
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::ostringstream oss;
    oss << std::hex << ms << "-" << dis(gen);
    return oss.str();
}

bool CommandRequestManager::is_self_pardon(const std::string& applicant, const std::string& command) {
    // 检查命令是否为 /pardon <applicant>
    std::string cmd_lower = to_lower(remove_spaces(command));
    std::string applicant_lower = to_lower(applicant);
    
    // 移除开头的 /
    if (!cmd_lower.empty() && cmd_lower[0] == '/') {
        cmd_lower = cmd_lower.substr(1);
    }
    
    // 检查是否以 "pardon" 开头并包含申请人名字
    if (cmd_lower.find("pardon") == 0) {
        std::string rest = cmd_lower.substr(6); // "pardon" 长度为6
        if (rest.find(applicant_lower) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string CommandRequestManager::create_request(const std::string& applicant,
                                                  const std::string& command,
                                                  const std::string& reason,
                                                  const std::string& image_data,
                                                  const std::string& image_ext) {
    RequestInfo info;
    info.id = generate_id();
    info.applicant = trim(applicant);
    info.command = trim(command);
    info.reason = trim(reason);
    info.created_at = std::chrono::system_clock::now();
    info.executed = false;
    
    // 保存图片（如果有）
    if (!image_data.empty()) {
        std::string filename = info.id + image_ext;
        std::string filepath = upload_dir_ + "/" + filename;
        
        std::ofstream file(filepath, std::ios::binary);
        if (file) {
            file.write(image_data.data(), image_data.size());
            file.close();
            info.image_path = filename; // 只存储文件名
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_[info.id] = info;
    }
    
    save_data();
    
    return info.id;
}

int CommandRequestManager::vote(const std::string& request_id, const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = requests_.find(request_id);
    if (it == requests_.end()) {
        return 2; // 申请不存在
    }
    
    if (it->second.executed) {
        return 3; // 已执行
    }
    
    if (it->second.voted_ips.count(ip) > 0) {
        return 1; // 已投过票
    }
    
    it->second.voted_ips.insert(ip);
    
    // 不在这里执行，让 checker 线程来处理
    // 这样可以避免在锁内执行回调
    
    return 0; // 成功
}

std::vector<RequestInfo> CommandRequestManager::list_requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<RequestInfo> result;
    result.reserve(requests_.size());
    
    for (const auto& pair : requests_) {
        result.push_back(pair.second);
    }
    
    // 按创建时间排序（新的在前）
    std::sort(result.begin(), result.end(), 
        [](const RequestInfo& a, const RequestInfo& b) {
            return a.created_at > b.created_at;
        });
    
    return result;
}

bool CommandRequestManager::get_request(const std::string& request_id, RequestInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = requests_.find(request_id);
    if (it == requests_.end()) {
        return false;
    }
    
    out = it->second;
    return true;
}

bool CommandRequestManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    save_data();
    return true;
}

// ============================================================================
// 数据持久化
// ============================================================================

void CommandRequestManager::load_data() {
    std::ifstream file(data_file_);
    if (!file) {
        return; // 文件不存在，跳过
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
    
    /*
     * 文件格式（简单文本格式，每个申请多行）:
     * === REQUEST ===
     * id|申请ID
     * applicant|申请人
     * command|命令
     * reason|原因
     * image|图片路径
     * created|创建时间
     * executed|是否执行 (0/1)
     * executed_at|执行时间
     * votes|IP1,IP2,IP3...
     * === END ===
     */
    
    std::string line;
    RequestInfo current;
    bool in_request = false;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        if (line == "=== REQUEST ===") {
            in_request = true;
            current = RequestInfo();
            continue;
        }
        
        if (line == "=== END ===") {
            if (in_request && !current.id.empty()) {
                requests_[current.id] = current;
            }
            in_request = false;
            continue;
        }
        
        if (!in_request) continue;
        
        size_t sep = line.find('|');
        if (sep == std::string::npos) continue;
        
        std::string key = line.substr(0, sep);
        std::string value = line.substr(sep + 1);
        
        if (key == "id") {
            current.id = value;
        } else if (key == "applicant") {
            current.applicant = value;
        } else if (key == "command") {
            current.command = value;
        } else if (key == "reason") {
            current.reason = value;
        } else if (key == "image") {
            current.image_path = value;
        } else if (key == "created") {
            current.created_at = string_to_time(value);
        } else if (key == "executed") {
            current.executed = (value == "1");
        } else if (key == "executed_at") {
            if (!value.empty()) {
                current.executed_at = string_to_time(value);
            }
        } else if (key == "votes") {
            // 解析IP列表
            if (!value.empty()) {
                std::istringstream iss(value);
                std::string ip;
                while (std::getline(iss, ip, ',')) {
                    ip = trim(ip);
                    if (!ip.empty()) {
                        current.voted_ips.insert(ip);
                    }
                }
            }
        }
    }
}

void CommandRequestManager::save_data() const {
    std::ofstream file(data_file_);
    if (!file) {
        std::cerr << "[CommandRequest] 无法保存数据文件: " << data_file_ << std::endl;
        return;
    }
    
    for (const auto& pair : requests_) {
        const auto& req = pair.second;
        
        file << "=== REQUEST ===" << "\n";
        file << "id|" << req.id << "\n";
        file << "applicant|" << req.applicant << "\n";
        file << "command|" << req.command << "\n";
        file << "reason|" << req.reason << "\n";
        file << "image|" << req.image_path << "\n";
        file << "created|" << time_to_string(req.created_at) << "\n";
        file << "executed|" << (req.executed ? "1" : "0") << "\n";
        file << "executed_at|" << (req.executed ? time_to_string(req.executed_at) : "") << "\n";
        
        // 保存投票IP列表
        file << "votes|";
        bool first = true;
        for (const auto& ip : req.voted_ips) {
            if (!first) file << ",";
            file << ip;
            first = false;
        }
        file << "\n";
        
        file << "=== END ===" << "\n";
    }
}

// ============================================================================
// 检查线程
// ============================================================================

void CommandRequestManager::checker_thread_func() {
    while (true) {
        std::unique_lock<std::mutex> lock(checker_mutex_);
        
        // 每10秒检查一次
        checker_cv_.wait_for(lock, std::chrono::seconds(10), [this] {
            return stop_checker_.load();
        });
        
        if (stop_checker_) break;
        
        // 检查并执行达到阈值的申请
        check_and_execute();
        
        // 清理过期申请
        cleanup_expired();
    }
}

void CommandRequestManager::check_and_execute() {
    std::vector<RequestInfo> to_execute;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : requests_) {
            auto& req = pair.second;
            if (!req.executed && req.vote_count() >= vote_threshold_) {
                req.executed = true;
                req.executed_at = std::chrono::system_clock::now();
                to_execute.push_back(req);
            }
        }
    }
    
    // 在锁外执行回调
    for (const auto& req : to_execute) {
        if (execute_callback_) {
            execute_callback_(req.command, req.applicant);
        }
        std::cout << "[CommandRequest] 命令申请已执行: " << req.command 
                  << " (申请人: " << req.applicant << ")" << std::endl;
    }
    
    if (!to_execute.empty()) {
        save_data();
    }
}

void CommandRequestManager::cleanup_expired() {
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> to_remove;
    std::vector<std::string> images_to_delete;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : requests_) {
            const auto& req = pair.second;
            if (req.executed) {
                auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
                    now - req.executed_at).count();
                if (elapsed >= 24) {
                    to_remove.push_back(req.id);
                    if (!req.image_path.empty()) {
                        images_to_delete.push_back(req.image_path);
                    }
                }
            }
        }
        
        for (const auto& id : to_remove) {
            requests_.erase(id);
        }
    }
    
    // 删除图片文件
    for (const auto& img : images_to_delete) {
        delete_image(img);
    }
    
    if (!to_remove.empty()) {
        save_data();
        std::cout << "[CommandRequest] 清理了 " << to_remove.size() << " 个过期申请" << std::endl;
    }
}

void CommandRequestManager::delete_image(const std::string& image_path) {
    if (image_path.empty()) return;
    
    std::string full_path = upload_dir_ + "/" + image_path;
    std::error_code ec;
    std::filesystem::remove(full_path, ec);
    if (ec) {
        std::cerr << "[CommandRequest] 删除图片失败: " << full_path << std::endl;
    }
}

} // namespace dl
