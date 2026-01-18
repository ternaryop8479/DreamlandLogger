#ifndef DREAMLAND_LOGGER_INCLUDE_WEB_SERVER_H
#define DREAMLAND_LOGGER_INCLUDE_WEB_SERVER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <httplib.h>

// Forward declaration
namespace httplib {
class Server;
}

namespace dl {

class PlayerList;
class CommandRequestManager;

// 日志条目
struct LogEntry {
    std::string timestamp;
    std::string type;      // "join", "leave", "command", "chat", "system"
    std::string player;
    std::string content;
};

// 获取日志回调类型
using GetLogsCallback = std::function<std::vector<LogEntry>()>;
// 获取OP列表回调类型
using GetOpsCallback = std::function<std::vector<std::string>()>;
// 执行命令回调类型
using ExecuteCommandCallback = std::function<void(const std::string& command)>;
// 检查玩家是否存在回调类型
using PlayerExistsCallback = std::function<bool(const std::string& player)>;

// Web服务器配置
struct WebServerConfig {
    int port = 8080;                              // 监听端口
    std::string web_root = "web";                 // 静态文件目录
    std::string upload_dir = "data/uploads";      // 上传文件目录
};

// Web服务器
class WebServer {
public:
    // 构造函数
    WebServer(const WebServerConfig& config,
              PlayerList& player_list,
              CommandRequestManager& request_manager);
    
    ~WebServer();
    
    // 禁用拷贝
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    
    // 设置回调
    void set_get_logs_callback(GetLogsCallback callback);
    void set_get_ops_callback(GetOpsCallback callback);
    void set_execute_command_callback(ExecuteCommandCallback callback);
    void set_player_exists_callback(PlayerExistsCallback callback);
    
    // 启动服务器（非阻塞，在新线程中运行）
    bool start();
    
    // 停止服务器
    void stop();
    
    // 是否正在运行
    bool is_running() const { return running_.load(); }
    
    // 添加系统日志（如命令执行日志）
    void add_system_log(const std::string& message);

private:
    // 设置路由
    void setup_routes();
    
    // API 处理函数
    void handle_get_logs(const httplib::Request& req, httplib::Response& res);
    void handle_get_online(const httplib::Request& req, httplib::Response& res);
    void handle_get_ops(const httplib::Request& req, httplib::Response& res);
    void handle_get_banned(const httplib::Request& req, httplib::Response& res);
    void handle_get_players(const httplib::Request& req, httplib::Response& res);
    void handle_get_requests(const httplib::Request& req, httplib::Response& res);
    void handle_post_request(const httplib::Request& req, httplib::Response& res);
    void handle_post_vote(const httplib::Request& req, httplib::Response& res);
    
    // 获取客户端IP
    static std::string get_client_ip(const httplib::Request& req);
    
    // JSON 辅助函数
    static std::string escape_json(const std::string& s);

private:
    WebServerConfig config_;
    PlayerList& player_list_;
    CommandRequestManager& request_manager_;
    
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    
    // 回调函数
    GetLogsCallback get_logs_callback_;
    GetOpsCallback get_ops_callback_;
    ExecuteCommandCallback execute_command_callback_;
    PlayerExistsCallback player_exists_callback_;
    
    // 系统日志
    std::vector<LogEntry> system_logs_;
    mutable std::mutex system_logs_mutex_;
    static constexpr size_t MAX_SYSTEM_LOGS = 100;
};

} // namespace dl

#endif // DREAMLAND_LOGGER_INCLUDE_WEB_SERVER_H
