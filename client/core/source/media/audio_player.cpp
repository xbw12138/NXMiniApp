//
// nxkit: AudioPlayer — 纯音频 libmpv 实例实现。
//

#include "nxkit/media/audio_player.hpp"

#ifdef NXKIT_WITH_MPV

#include <borealis/core/logger.hpp>

#include <cstdio>
#include <cstring>

namespace nxkit {

AudioPlayer::AudioPlayer() {
    mpv_handle* created = mpv_create();
    if (!created) {
        setError("mpv_create failed");
        return;
    }

    mpv_set_option_string(created, "config", "no");
    mpv_set_option_string(created, "idle", "yes");
    // 纯音频：不创建视频输出，避免和小程序的 <video> 抢 GL 上下文。
    mpv_set_option_string(created, "vo", "null");
    mpv_set_option_string(created, "vid", "no");
    mpv_set_option_string(created, "audio-channels", "stereo");
    mpv_set_option_string(created, "keep-open", "no");
    mpv_set_option_string(created, "terminal", "no");
    mpv_set_option_string(created, "hr-seek", "yes");
    mpv_set_option_string(created, "cache", "no");
    mpv_set_option_string(created, "ao", "auto");
    mpv_set_option_string(created, "volume", "100");
    mpv_set_option_string(created, "loop-file", "no");

    if (mpv_initialize(created) < 0) {
        mpv_terminate_destroy(created);
        setError("mpv_initialize failed");
        return;
    }

    handle = created;
    mpv_observe_property(handle, 1, "playback-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 3, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 4, "pause", MPV_FORMAT_FLAG);

    worker = std::thread([this]() { run(); });
}

AudioPlayer::~AudioPlayer() {
    running = false;
    if (worker.joinable()) worker.join();
    if (handle) {
        mpv_terminate_destroy(handle);
        handle = nullptr;
    }
}

bool AudioPlayer::isValid() const { return handle != nullptr; }

void AudioPlayer::setError(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.error = text;
}

bool AudioPlayer::load(const std::string& source, bool loop, double start) {
    if (!handle || source.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        url = source;
        startAt = start > 0.0 ? start : 0.0;
        snapshot = State();
    }
    issueLoad(startAt);
    mpv_set_property_string(handle, "loop-file", loop ? "inf" : "no");
    // 统一由 play() 起播，autoplay 交给 JS 层决定，这里先停在首帧。
    mpv_set_property_string(handle, "pause", "yes");
    return true;
}

void AudioPlayer::issueLoad(double start) {
    std::string source;
    {
        std::lock_guard<std::mutex> lock(mutex);
        source = url;
        loading = !source.empty();
    }
    if (source.empty()) return;
    if (start > 0.0) {
        char option[64];
        std::snprintf(option, sizeof(option), "start=%.3f", start);
        const char* args[] = {"loadfile", source.c_str(), "replace", option, nullptr};
        mpv_command(handle, args);
    } else {
        const char* args[] = {"loadfile", source.c_str(), "replace", nullptr};
        mpv_command(handle, args);
    }
}

void AudioPlayer::play() {
    if (!handle) return;
    bool reload = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        // 播完之后再 play 需要重新 load，否则 mpv 停在 idle。
        reload = snapshot.ended || (!snapshot.loaded && !loading);
        if (snapshot.ended) snapshot.ended = false;
    }
    if (reload) issueLoad(startAt);
    mpv_set_property_string(handle, "pause", "no");
}

void AudioPlayer::pause() {
    if (!handle) return;
    mpv_set_property_string(handle, "pause", "yes");
}

void AudioPlayer::stop() {
    if (!handle) return;
    mpv_command_string(handle, "stop");
    std::lock_guard<std::mutex> lock(mutex);
    startAt = 0.0;
    loading = false;
    snapshot.playing = false;
    snapshot.ended = false;
    snapshot.loaded = false;
    snapshot.currentTime = 0.0;
}

void AudioPlayer::seek(double seconds) {
    if (!handle || seconds < 0.0) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        startAt = seconds;
    }
    mpv_set_property(handle, "time-pos", MPV_FORMAT_DOUBLE, &seconds);
}

void AudioPlayer::setVolume(int volume) {
    if (!handle) return;
    int64_t value = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    mpv_set_property(handle, "volume", MPV_FORMAT_INT64, &value);
}

void AudioPlayer::setLoop(bool loop) {
    if (!handle) return;
    mpv_set_property_string(handle, "loop-file", loop ? "inf" : "no");
}

AudioPlayer::State AudioPlayer::state() const {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshot;
}

void AudioPlayer::run() {
    while (running) {
        mpv_event* event = mpv_wait_event(handle, 0.1);
        if (!event || event->event_id == MPV_EVENT_NONE) continue;

        switch (event->event_id) {
            case MPV_EVENT_START_FILE:
            case MPV_EVENT_FILE_LOADED: {
                std::lock_guard<std::mutex> lock(mutex);
                loading = false;
                snapshot.loaded = true;
                snapshot.ended = false;
                snapshot.error.clear();
                break;
            }
            case MPV_EVENT_END_FILE: {
                auto* end = static_cast<mpv_event_end_file*>(event->data);
                const bool eof = end && end->reason == MPV_END_FILE_REASON_EOF;
                std::lock_guard<std::mutex> lock(mutex);
                loading = false;
                snapshot.playing = false;
                snapshot.ended = eof;
                if (!eof) snapshot.loaded = false;
                snapshot.currentTime = 0.0;
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* property = static_cast<mpv_event_property*>(event->data);
                if (!property || !property->data) break;
                std::lock_guard<std::mutex> lock(mutex);
                if (std::strcmp(property->name, "playback-time") == 0 &&
                    property->format == MPV_FORMAT_DOUBLE) {
                    snapshot.currentTime = *static_cast<double*>(property->data);
                } else if (std::strcmp(property->name, "duration") == 0 &&
                           property->format == MPV_FORMAT_DOUBLE) {
                    snapshot.duration = *static_cast<double*>(property->data);
                } else if (std::strcmp(property->name, "eof-reached") == 0 &&
                           property->format == MPV_FORMAT_FLAG) {
                    const bool eof = *static_cast<int*>(property->data) != 0;
                    if (eof) {
                        snapshot.ended = true;
                        snapshot.playing = false;
                    }
                } else if (std::strcmp(property->name, "pause") == 0 &&
                           property->format == MPV_FORMAT_FLAG) {
                    const bool paused = *static_cast<int*>(property->data) != 0;
                    snapshot.playing = snapshot.loaded && !snapshot.ended && !paused;
                }
                break;
            }
            default: break;
        }
    }
}

}  // namespace nxkit

#else  // !NXKIT_WITH_MPV

namespace nxkit {

AudioPlayer::AudioPlayer() = default;
AudioPlayer::~AudioPlayer() = default;
bool AudioPlayer::isValid() const { return false; }
void AudioPlayer::setError(const std::string&) {}
bool AudioPlayer::load(const std::string&, bool, double) { return false; }
void AudioPlayer::play() {}
void AudioPlayer::pause() {}
void AudioPlayer::stop() {}
void AudioPlayer::seek(double) {}
void AudioPlayer::setVolume(int) {}
void AudioPlayer::setLoop(bool) {}
AudioPlayer::State AudioPlayer::state() const { return State(); }
void AudioPlayer::run() {}
void AudioPlayer::issueLoad(double) {}

}  // namespace nxkit

#endif  // NXKIT_WITH_MPV
