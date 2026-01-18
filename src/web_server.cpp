#include <memory>
#include <web_server.h>
#include <player_list.h>
#include <command_request.h>

#include <httplib.h>

#include <chrono>
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

static std::string get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::string get_file_extension(const std::string& filename) {
    size_t pos = filename.rfind('.');
    if (pos == std::string::npos) return "";
    return filename.substr(pos);
}

static std::string get_mime_type(const std::string& ext) {
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

// ============================================================================
// WebServer 实现
// ============================================================================

WebServer::WebServer(const WebServerConfig& config,
                     PlayerList& player_list,
                     CommandRequestManager& request_manager)
    : config_(config)
    , player_list_(player_list)
    , request_manager_(request_manager)
    , server_(std::make_unique<httplib::Server>()) {
    
    setup_routes();
}

WebServer::~WebServer() {
    stop();
}

void WebServer::set_get_logs_callback(GetLogsCallback callback) {
    get_logs_callback_ = std::move(callback);
}

void WebServer::set_get_ops_callback(GetOpsCallback callback) {
    get_ops_callback_ = std::move(callback);
}

void WebServer::set_execute_command_callback(ExecuteCommandCallback callback) {
    execute_command_callback_ = std::move(callback);
}

void WebServer::set_player_exists_callback(PlayerExistsCallback callback) {
    player_exists_callback_ = std::move(callback);
}

bool WebServer::start() {
    if (running_) return false;
    
    running_ = true;
    server_thread_ = std::thread([this]() {
        std::cout << "[WebServer] 启动在端口 " << config_.port << std::endl;
        if (!server_->listen("0.0.0.0", config_.port)) {
            std::cerr << "[WebServer] 启动失败" << std::endl;
            running_ = false;
        }
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return running_;
}

void WebServer::stop() {
    if (!running_) return;
    
    running_ = false;
    server_->stop();
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    std::cout << "[WebServer] 已停止" << std::endl;
}

void WebServer::add_system_log(const std::string& message) {
    std::lock_guard<std::mutex> lock(system_logs_mutex_);
    
    LogEntry entry;
    entry.timestamp = get_current_time_string();
    entry.type = "system";
    entry.player = "";
    entry.content = message;
    
    system_logs_.push_back(entry);
    
    // 限制日志数量
    if (system_logs_.size() > MAX_SYSTEM_LOGS) {
        system_logs_.erase(system_logs_.begin());
    }
}

// ============================================================================
// 路由设置
// ============================================================================

void WebServer::setup_routes() {
    // 静态文件服务 - 主页
    server_->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(config_.web_root + "/index.html");
        if (file) {
            std::ostringstream oss;
            oss << file.rdbuf();
            res.set_content(oss.str(), "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
        }
    });
    
    // 静态文件服务 - CSS/JS
    server_->Get(R"(/(.+\.(css|js|html|ico)))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string path = config_.web_root + "/" + req.matches[1].str();
        std::ifstream file(path, std::ios::binary);
        if (file) {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string ext = get_file_extension(path);
            res.set_content(oss.str(), get_mime_type(ext));
        } else {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
        }
    });
    
    // 上传文件访问
    server_->Get(R"(/uploads/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string filename = req.matches[1].str();
        std::string path = config_.upload_dir + "/" + filename;
        std::ifstream file(path, std::ios::binary);
        if (file) {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string ext = get_file_extension(path);
            res.set_content(oss.str(), get_mime_type(ext));
        } else {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
        }
    });
    
    // API 路由
    server_->Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_logs(req, res);
    });
    
    server_->Get("/api/online", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_online(req, res);
    });
    
    server_->Get("/api/ops", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_ops(req, res);
    });
    
    server_->Get("/api/banned", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_banned(req, res);
    });
    
    server_->Get("/api/players", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_players(req, res);
    });
    
    server_->Get("/api/requests", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_requests(req, res);
    });
    
    server_->Post("/api/requests", [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_request(req, res);
    });
    
    server_->Post(R"(/api/requests/([^/]+)/vote)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_vote(req, res);
    });
    
    // 配置
    server_->set_payload_max_length(1024 * 1024 * 10); // 10MB
}

// ============================================================================
// API 处理函数
// ============================================================================

void WebServer::handle_get_logs(const httplib::Request&, httplib::Response& res) {
    std::ostringstream json;
    json << "{\"logs\":[";
    
    bool first = true;
    
    // 获取游戏日志
    if (get_logs_callback_) {
        auto logs = get_logs_callback_();
        for (const auto& log : logs) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"timestamp\":\"" << escape_json(log.timestamp) << "\",";
            json << "\"type\":\"" << escape_json(log.type) << "\",";
            json << "\"player\":\"" << escape_json(log.player) << "\",";
            json << "\"content\":\"" << escape_json(log.content) << "\"";
            json << "}";
        }
    }
    
    // 获取系统日志
    {
        std::lock_guard<std::mutex> lock(system_logs_mutex_);
        for (const auto& log : system_logs_) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"timestamp\":\"" << escape_json(log.timestamp) << "\",";
            json << "\"type\":\"" << escape_json(log.type) << "\",";
            json << "\"player\":\"" << escape_json(log.player) << "\",";
            json << "\"content\":\"" << escape_json(log.content) << "\"";
            json << "}";
        }
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_get_online(const httplib::Request&, httplib::Response& res) {
    auto online = player_list_.list_online_player();
    
    std::ostringstream json;
    json << "{\"players\":[";
    
    bool first = true;
    for (const auto& p : online) {
        if (!first) json << ",";
        first = false;
        json << "{";
        json << "\"name\":\"" << escape_json(p.name) << "\",";
        json << "\"client\":\"" << escape_json(p.client_info) << "\"";
        json << "}";
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_get_ops(const httplib::Request&, httplib::Response& res) {
    std::ostringstream json;
    json << "{\"ops\":[";
    
    if (get_ops_callback_) {
        auto ops = get_ops_callback_();
        bool first = true;
        for (const auto& op : ops) {
            if (!first) json << ",";
            first = false;
            json << "\"" << escape_json(op) << "\"";
        }
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_get_banned(const httplib::Request&, httplib::Response& res) {
    auto banned = player_list_.list_banned_player_info();
    
    std::ostringstream json;
    json << "{\"players\":[";
    
    bool first = true;
    for (const auto& p : banned) {
        if (!first) json << ",";
        first = false;
        json << "{";
        json << "\"name\":\"" << escape_json(p.name) << "\",";
        json << "\"reason\":\"" << escape_json(p.reason) << "\",";
        json << "\"ban_time\":\"" << escape_json(p.get_ban_time_string()) << "\",";
        json << "\"unban_time\":\"" << escape_json(p.get_unban_time_string()) << "\",";
        json << "\"permanent\":" << (p.is_permanent ? "true" : "false");
        json << "}";
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_get_players(const httplib::Request&, httplib::Response& res) {
    auto players = player_list_.list_player();
    
    std::ostringstream json;
    json << "{\"players\":[";
    
    bool first = true;
    for (const auto& p : players) {
        if (!first) json << ",";
        first = false;
        json << "\"" << escape_json(p) << "\"";
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_get_requests(const httplib::Request&, httplib::Response& res) {
    auto requests = request_manager_.list_requests();
    size_t threshold = request_manager_.get_threshold();
    
    std::ostringstream json;
    json << "{\"threshold\":" << threshold << ",\"requests\":[";
    
    bool first = true;
    for (const auto& r : requests) {
        if (!first) json << ",";
        first = false;
        json << "{";
        json << "\"id\":\"" << escape_json(r.id) << "\",";
        json << "\"applicant\":\"" << escape_json(r.applicant) << "\",";
        json << "\"command\":\"" << escape_json(r.command) << "\",";
        json << "\"reason\":\"" << escape_json(r.reason) << "\",";
        json << "\"image\":\"" << escape_json(r.image_path) << "\",";
        json << "\"votes\":" << r.vote_count() << ",";
        json << "\"executed\":" << (r.executed ? "true" : "false") << ",";
        json << "\"created_at\":\"" << escape_json(r.get_created_time_string()) << "\"";
        json << "}";
    }
    
    json << "]}";
    
    res.set_content(json.str(), "application/json; charset=utf-8");
}

void WebServer::handle_post_request(const httplib::Request& req, httplib::Response& res) {
    std::string applicant, command, reason;
    std::string image_data;
    std::string image_ext;
    
    // 检查是否为 multipart/form-data
    if (req.is_multipart_form_data()) {
        // 获取表单字段
        if (!req.form.has_field("applicant") || !req.form.has_field("command") || !req.form.has_field("reason")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing required fields\"}", "application/json");
            return;
        }
        
        applicant = req.form.get_field("applicant");
        command = req.form.get_field("command");
        reason = req.form.get_field("reason");
        
        // 检查是否有图片
        if (req.form.has_file("image")) {
            auto image_file = req.form.get_file("image");
            if (!image_file.content.empty()) {
                image_data = image_file.content;
                image_ext = get_file_extension(image_file.filename);
                if (image_ext.empty()) {
                    if (image_file.content_type.find("png") != std::string::npos) {
                        image_ext = ".png";
                    } else if (image_file.content_type.find("jpeg") != std::string::npos || 
                               image_file.content_type.find("jpg") != std::string::npos) {
                        image_ext = ".jpg";
                    } else if (image_file.content_type.find("gif") != std::string::npos) {
                        image_ext = ".gif";
                    } else {
                        image_ext = ".png";
                    }
                }
            }
        }
    } else {
		std::cout << "[WebServer] 处理普通 POST 请求" << std::endl;
        // 处理普通 POST 数据
        if (!req.has_param("applicant")) {
			std::cout << "[WebServer] 缺少必要字段" << std::endl;
            res.status = 400;
            res.set_content("{\"error\":\"Missing required fields\"}", "application/json");
            return;
        }
        
        applicant = req.get_param_value("applicant");
        command = req.get_param_value("command");
        reason = req.get_param_value("reason");
    }
    
    // 去除首尾空白
    applicant = trim(applicant);
    command = trim(command);
    reason = trim(reason);
    
    // 检查申请人是否存在
    if (player_exists_callback_ && !player_exists_callback_(applicant)) {
        res.status = 400;
        res.set_content("{\"error\":\"Player not found\"}", "application/json");
        return;
    }
    
    // 检查是否需要检讨书
    bool is_self_pardon = CommandRequestManager::is_self_pardon(applicant, command);
    if (is_self_pardon && image_data.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Self-pardon requires confession image\"}", "application/json");
        return;
    }
    
    std::string id = request_manager_.create_request(applicant, command, reason, image_data, image_ext);
    
    std::ostringstream json;
    json << "{\"id\":\"" << escape_json(id) << "\"}";
    res.set_content(json.str(), "application/json");
    
    std::cout << "[WebServer] 新命令申请: " << command << " (申请人: " << applicant << ")" << std::endl;
    
    // 添加到系统日志
    add_system_log("新命令申请: " + command + " (申请人: " + applicant + ")");
}

void WebServer::handle_post_vote(const httplib::Request& req, httplib::Response& res) {
    std::string request_id = req.matches[1].str();
    std::string ip = get_client_ip(req);
    
    int result = request_manager_.vote(request_id, ip);
    
    std::ostringstream json;
    switch (result) {
        case 0:
            json << "{\"success\":true,\"message\":\"Vote recorded\"}";
            std::cout << "[WebServer] 投票成功: " << request_id << " (IP: " << ip << ")" << std::endl;
            break;
        case 1:
            res.status = 400;
            json << "{\"success\":false,\"error\":\"Already voted\"}";
            break;
        case 2:
            res.status = 404;
            json << "{\"success\":false,\"error\":\"Request not found\"}";
            break;
        case 3:
            res.status = 400;
            json << "{\"success\":false,\"error\":\"Request already executed\"}";
            break;
        default:
            res.status = 500;
            json << "{\"success\":false,\"error\":\"Unknown error\"}";
            break;
    }
    
    res.set_content(json.str(), "application/json");
}

// ============================================================================
// 辅助函数
// ============================================================================

std::string WebServer::get_client_ip(const httplib::Request& req) {
    // 优先使用 X-Forwarded-For 头（反向代理情况）
    if (req.has_header("X-Forwarded-For")) {
        std::string xff = req.get_header_value("X-Forwarded-For");
        size_t comma = xff.find(',');
        if (comma != std::string::npos) {
            return xff.substr(0, comma);
        }
        return xff;
    }
    
    // 使用 X-Real-IP 头
    if (req.has_header("X-Real-IP")) {
        return req.get_header_value("X-Real-IP");
    }
    
    // 使用远程地址
    return req.remote_addr;
}

std::string WebServer::escape_json(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

} // namespace dl
