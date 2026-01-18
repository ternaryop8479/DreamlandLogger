#include <command_request.h>
#include <io/program.h>
#include <player_list.h>
#include <server_manager.h>
#include <web_server.h>

#include <csignal>
#include <iostream>
#include <memory>

// 全局指针，用于信号处理
static dl::WebServer* g_web_server = nullptr;
static dl::ServerManager* g_server_manager = nullptr;

void signal_handler(int signal) {
    std::cout << "\n[Main] 收到信号 " << signal << "，正在关闭..." << std::endl;
    
    if (g_web_server) {
        g_web_server->stop();
    }
    
    if (g_server_manager) {
        g_server_manager->stop();
    }
    
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3) {
        std::cout << "用法: " << argv[0] << " <服务器启动命令> [端口]" << std::endl;
        std::cout << "示例: " << argv[0] << " \"cd server && java -jar server.jar nogui\" 8080" << std::endl;
        return 1;
    }
    
    std::string server_command = argv[1];
    int port = 8080;
    if (argc == 3) {
        port = std::atoi(argv[2]);
    }
    
    std::cout << "==================================================" << std::endl;
    std::cout << "      MC 服务器管理系统 - DreamlandLogger       " << std::endl;
    std::cout << "==================================================" << std::endl;
    
    try {
        // 创建 PlayerList（注意：Program 引用在 ServerManager 中）
        dl::Program program(server_command);
        
        dl::PlayerList player_list(
            "data/players.list",
            "data/banned.list",
            "data/forbidden_commands.list",
            program
        );
        
        // 创建 ServerManager
        dl::ServerManager server_manager(
            &program,
            "server/ops.json",
            player_list
        );
        g_server_manager = &server_manager;
        
        // 创建 CommandRequestManager
        dl::CommandRequestManager request_manager(
            "data/requests.dat",
            "data/uploads",
            5,  // 投票阈值
            [&server_manager](const std::string& command, const std::string& applicant) {
                // 命令执行回调
                server_manager.execute_command(command);
            }
        );
        
        // 创建 WebServer
        dl::WebServerConfig web_config;
        web_config.port = port;
        web_config.web_root = "web";
        web_config.upload_dir = "data/uploads";
        
        dl::WebServer web_server(web_config, player_list, request_manager);
        g_web_server = &web_server;
        
        // 设置回调
        web_server.set_get_logs_callback([&server_manager]() {
            auto logs = server_manager.get_logs();
            std::vector<dl::LogEntry> result;
            for (const auto& log : logs) {
                dl::LogEntry entry;
                entry.timestamp = log.timestamp;
                entry.type = log.type;
                entry.player = log.player;
                entry.content = log.content;
                result.push_back(entry);
            }
            return result;
        });
        
        web_server.set_get_ops_callback([&server_manager]() {
            return server_manager.get_ops();
        });
        
        web_server.set_execute_command_callback([&server_manager](const std::string& cmd) {
            server_manager.execute_command(cmd);
        });
        
        web_server.set_player_exists_callback([&player_list](const std::string& player) {
            auto players = player_list.list_player();
            return std::find(players.begin(), players.end(), player) != players.end();
        });
        
        // 注册信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // 启动服务器
        if (!server_manager.start()) {
            std::cerr << "[Main] MC 服务器启动失败" << std::endl;
            return 1;
        }
        
        // 启动 Web 服务器
        if (!web_server.start()) {
            std::cerr << "[Main] Web 服务器启动失败" << std::endl;
            server_manager.stop();
            return 1;
        }
        
        std::cout << "\n==================================================" << std::endl;
        std::cout << "  系统启动成功！" << std::endl;
        std::cout << "  Web 管理界面: http://localhost:" << port << std::endl;
        std::cout << "  按 Ctrl+C 停止服务器" << std::endl;
        std::cout << "==================================================" << std::endl;
        
        // 主循环
        while (server_manager.is_running() && web_server.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        std::cout << "[Main] 服务器已停止" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[Main] 异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
