//
// miniapp: Switch 硬件能力桥接（六轴体感 / HD 震动 / 红外相机 / NFC-Amiibo）。
//
// 所有控制方法必须在 UI 线程调用：HID、irs、nfp 服务都不允许跨线程访问。
// 状态快照在 UI 线程每帧更新，脚本线程通过 motionState()/irState()/nfcState()
// 读取带锁的副本。桌面构建提供同签名的空实现，方便预览页面布局。
//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "miniapp/json.hpp"

namespace miniapp {

class HardwareManager {
public:
    HardwareManager();
    ~HardwareManager();

    HardwareManager(const HardwareManager&) = delete;
    HardwareManager& operator=(const HardwareManager&) = delete;

    /// UI 线程每帧调用：采样六轴、推进震动时长、轮询红外/NFC。
    void tick(double nowMs);

    // ---- 六轴体感 ----
    /// 脚本首次读取六轴状态时调用：下一帧才真正开启传感器，避免无谓占用 HID。
    void requestMotion();
    JsonValue motionState() const;

    // ---- HD 震动 ----
    /// 振幅 0..1，频率单位 Hz（Joy-Con 支持约 40~1250Hz）。
    /// durationMs<=0 表示持续震动，直到 rumbleStop()。
    void rumble(double lowFreq, double highFreq, double lowAmp, double highAmp,
                double durationMs);
    void rumbleStop();

    // ---- 红外相机 ----
    /// mode: "image"（红外画面）或 "gesture"（聚类手部检测）。
    bool irStart(const std::string& mode);
    void irStop();
    bool irRunning() const;
    /// 取最近一帧红外画面（RGBA8）。version 每次有新帧自增。
    bool copyIrImage(std::vector<uint8_t>& rgba, int& width, int& height,
                     std::uint64_t& version) const;
    JsonValue irState() const;

    // ---- NFC / Amiibo ----
    bool nfcStart();
    void nfcStop();
    /// 挂载当前卡片并读取 Amiibo 信息与应用存档区；结果进入 nfcState()。
    bool nfcMount();
    bool nfcUnmount();
    /// 把 hex 数据写回已挂载 Amiibo 的应用存档区（用于备份还原）。
    bool nfcWriteAppArea(const std::string& hexData);
    JsonValue nfcState() const;

    /// 停止全部硬件并释放服务句柄；视图析构时调用。
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace miniapp
