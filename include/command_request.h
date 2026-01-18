#ifndef DREAMLAND_LOGGER_INCLUDE_COMMAND_REQUEST_H
#define DREAMLAND_LOGGER_INCLUDE_COMMAND_REQUEST_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dl {

// 命令申请信息
struct RequestInfo {
    std::string id;                                          // 唯一ID
    std::string applicant;                                   // 申请人
    std::string command;                                     // 命令内容
    std::string reason;                                      // 申请原因
    std::string image_path;                                  // 检讨书图片路径（可选）
    std::unordered_set<std::string> voted_ips;               // 已投票的IP列表
    std::chrono::system_clock::time_point created_at;        // 创建时间
    std::chrono::system_clock::time_point executed_at;       // 执行时间
    bool executed = false;                                   // 是否已执行
    
    // 获取投票数
    size_t vote_count() const { return voted_ips.size(); }
    
    // 格式化创建时间
    std::string get_created_time_string() const;
    // 格式化执行时间
    std::string get_executed_time_string() const;
};

// 命令执行回调函数类型
using CommandExecuteCallback = std::function<void(const std::string& command, const std::string& applicant)>;

// 命令申请管理器
class CommandRequestManager {
public:
    // 构造函数
    // @param data_file: 申请数据存储文件路径
    // @param upload_dir: 图片上传目录
    // @param vote_threshold: 投票通过阈值
    // @param execute_callback: 命令执行回调
    CommandRequestManager(const std::string& data_file,
                          const std::string& upload_dir,
                          size_t vote_threshold,
                          CommandExecuteCallback execute_callback);
    
    ~CommandRequestManager();
    
    // 禁用拷贝
    CommandRequestManager(const CommandRequestManager&) = delete;
    CommandRequestManager& operator=(const CommandRequestManager&) = delete;
    
    // 创建新申请
    // @param applicant: 申请人
    // @param command: 命令内容
    // @param reason: 申请原因
    // @param image_data: 图片数据（可选，空则无图片）
    // @param image_ext: 图片扩展名（如 ".png", ".jpg"）
    // @return: 申请ID，失败返回空字符串
    std::string create_request(const std::string& applicant,
                               const std::string& command,
                               const std::string& reason,
                               const std::string& image_data = "",
                               const std::string& image_ext = "");
    
    // 为申请投票
    // @param request_id: 申请ID
    // @param ip: 投票者IP
    // @return: 0=成功, 1=已投过票, 2=申请不存在, 3=已执行
    int vote(const std::string& request_id, const std::string& ip);
    
    // 获取所有申请列表
    std::vector<RequestInfo> list_requests() const;
    
    // 获取单个申请
    // @param request_id: 申请ID
    // @param out: 输出参数
    // @return: 是否找到
    bool get_request(const std::string& request_id, RequestInfo& out) const;
    
    // 获取投票阈值
    size_t get_threshold() const { return vote_threshold_; }
    
    // 设置投票阈值
    void set_threshold(size_t threshold) { vote_threshold_ = threshold; }
    
    // 获取上传目录
    const std::string& get_upload_dir() const { return upload_dir_; }
    
    // 手动保存数据
    bool save() const;
    
    // 检查是否为pardon自己的命令
    static bool is_self_pardon(const std::string& applicant, const std::string& command);

private:
    // 生成唯一ID
    static std::string generate_id();
    
    // 加载数据
    void load_data();
    // 保存数据
    void save_data() const;
    
    // 检查线程函数（检查阈值、清理过期申请）
    void checker_thread_func();
    
    // 检查并执行达到阈值的申请
    void check_and_execute();
    
    // 清理已执行超过24小时的申请
    void cleanup_expired();
    
    // 删除图片文件
    void delete_image(const std::string& image_path);

private:
    std::string data_file_;                                  // 数据文件路径
    std::string upload_dir_;                                 // 上传目录
    size_t vote_threshold_;                                  // 投票阈值
    CommandExecuteCallback execute_callback_;                // 命令执行回调
    
    std::unordered_map<std::string, RequestInfo> requests_;  // 申请映射表
    
    mutable std::mutex mutex_;                               // 数据互斥锁
    
    std::thread checker_thread_;                             // 检查线程
    std::atomic<bool> stop_checker_{false};                  // 停止标志
    std::condition_variable checker_cv_;                     // 条件变量
    std::mutex checker_mutex_;                               // 检查线程互斥锁
};

} // namespace dl

#endif // DREAMLAND_LOGGER_INCLUDE_COMMAND_REQUEST_H
