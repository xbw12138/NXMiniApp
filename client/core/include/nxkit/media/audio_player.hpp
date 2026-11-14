//
// nxkit: AudioPlayer — 纯音频的 libmpv 实例（BGM / 音效）。
//
// MPVCore 是带 OpenGL 渲染上下文的单例，只适合 <video>。小程序还需要能在
// 播放视频之外独立播放音频（wx.createInnerAudioContext），因此这里开一个
// 不创建 vo / render context 的独立 mpv handle：解码走 ffmpeg，输出走平台
// 默认 ao（Switch 为 audren，桌面为 SDL / coreaudio）。
//

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#ifdef NXKIT_WITH_MPV
#include <mpv/client.h>
#endif

namespace nxkit {

class AudioPlayer {
public:
    struct State {
        bool playing = false;
        bool ended = false;
        bool loaded = false;
        double currentTime = 0.0;
        double duration = 0.0;
        std::string error;
    };

    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool isValid() const;
    /// 加载音频文件（本地路径或 http(s) 直链）。start 为起播位置（秒）。
    bool load(const std::string& url, bool loop, double start = 0.0);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setVolume(int volume);
    void setLoop(bool loop);

    State state() const;

private:
    void run();
    // 发起 loadfile；start > 0 时带 start= 选项，避免 seek 早于文件加载完成被丢弃。
    void issueLoad(double start);
    void setError(const std::string& text);

#ifdef NXKIT_WITH_MPV
    mpv_handle* handle = nullptr;
#endif
    std::thread worker;
    std::atomic<bool> running{true};
    std::string url;
    double startAt = 0.0;
    // loadfile 已发出、还没收到开始播放事件：此时 play() 不需要重复 load。
    bool loading = false;
    mutable std::mutex mutex;
    State snapshot;
};

}  // namespace nxkit
