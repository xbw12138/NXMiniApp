//
// crash_reporter: 崩溃日志留存与上报。
//
// Switch 上进程崩溃后无法再执行任何代码，所以采用「下次启动补传」：
//   1. 启动时写 session.running 标记，并安装致命信号处理器；
//   2. 正常退出删除标记；
//   3. 下次启动发现标记还在 => 上次是异常结束，把上一轮日志转存到
//      pending/ 目录，随后由后台上传线程 POST 到 /api/v1/device/logs。
//

#pragma once

#include <functional>
#include <string>

namespace miniapp::crash {

/// 必须在 setupFileLog() 之前调用（它会读取/轮转上一轮的日志文件）。
void beginSession(const std::string& version);

/// 正常退出时调用：清掉运行标记。
void endSession();

/// 启动后台上传线程，把 pending/ 里的崩溃日志补传到服务端。
void startUploader(const std::string& baseUrl, const std::string& version,
                   const std::string& deviceId);

/// 主动上报当前会话日志（不依赖崩溃）。后台线程执行，完成后在主线程回调，
/// 便于界面直接弹出结果；message 是给用户看的简短结果。
void reportCurrentLog(const std::string& baseUrl, const std::string& version,
                      const std::string& deviceId, const std::string& reason,
                      std::function<void(bool ok, const std::string& message)> callback);

/// 把一行日志同时记到内存环形缓冲里（SD 卡写失败/未刷盘时仍能上报）。
void appendLogLine(const std::string& line);

}  // namespace miniapp::crash
