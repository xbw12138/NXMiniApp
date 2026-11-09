//
// nxkit: VideoView implementation — wiliwili-style player facade over MPVCore.
//

#include "nxkit/media/video_view.hpp"

#include <borealis/core/logger.hpp>

namespace nxkit {

VideoView::VideoView() {
    // 注意：这里不能调 registerMpvEvent()。MPVCore 单例一旦触达就会做
    // mpv_initialize（内置脚本加载等大量分配），而桌面转发器（NSP）启动的
    // 进程内存池比 hbmenu 小，启动期初始化会直接崩在 malloc。
    // 因此 MPV 改为首次真正播放（mpv()）时才初始化。
}

#ifdef NXKIT_WITH_MPV
MPVCore& VideoView::mpv() {
    auto& core = MPVCore::instance();
    registerMpvEvent();
    return core;
}
#endif

VideoView::~VideoView() {
#ifdef NXKIT_WITH_MPV
    unregisterMpvEvent();
#endif
}

void VideoView::setUrl(const std::string& url, const std::string& headers,
                       const std::string& extra) {
    currentUrl = url;
#ifdef NXKIT_WITH_MPV
    if (url.empty()) return;
    state = VideoState{};
    state.loading = true;
    state.waiting = true;
    pendingSubtitles.clear();
    fireState();
    // http-header-fields 必须通过全局 set 设置，不能走 loadfile 的 per-file 选项
    // （mpv 会把选项串按 option=value 解析，破坏 Cookie 等含 =/; 的值）。
    mpv().setHttpHeaders(headers);
    mpv().setUrl(url, extra, "replace");
#else
    (void)url;
    (void)headers;
    (void)extra;
    state = VideoState{};
    state.error = true;
    state.errorText = "当前构建未启用视频播放";
    fireState();
#endif
}

void VideoView::addSubtitle(const std::string& url) {
    if (url.empty()) return;
#ifdef NXKIT_WITH_MPV
    if (state.metadataLoaded) {
        mpv().command_async("sub-add", url, "select");
    } else {
        pendingSubtitles.push_back(url);
    }
#else
    (void)url;
#endif
}

void VideoView::clearSubtitles() {
    pendingSubtitles.clear();
#ifdef NXKIT_WITH_MPV
    if (registered) mpv().command_async("sub-remove", "all");
#endif
}

void VideoView::resume() {
#ifdef NXKIT_WITH_MPV
    mpv().resume();
#endif
}

void VideoView::pause() {
#ifdef NXKIT_WITH_MPV
    if (registered) mpv().pause();
#endif
}

void VideoView::stop() {
#ifdef NXKIT_WITH_MPV
    // 未初始化过 MPV 时不要在这里触发初始化：退出流程也会调 stop()，
    // 为了停止一个从没播放过的视频而去初始化 mpv 会白吃一大块内存。
    if (registered) mpv().stop();
#endif
    state.ended = false;
    state.playing = false;
    state.paused = false;
    state.currentTime = 0.0;
    fireState();
}

void VideoView::togglePlay() {
#ifdef NXKIT_WITH_MPV
    auto& core = mpv();
    if (core.isPaused()) {
        core.resume();
    } else {
        core.pause();
    }
#endif
}

void VideoView::seek(double seconds) {
#ifdef NXKIT_WITH_MPV
    mpv().seek(static_cast<int64_t>(seconds));
#endif
}

void VideoView::seekRelative(double seconds) {
#ifdef NXKIT_WITH_MPV
    mpv().seekRelative(static_cast<int64_t>(seconds));
#endif
}

void VideoView::seekPercent(double percent) {
#ifdef NXKIT_WITH_MPV
    mpv().seekPercent(percent);
#endif
}

void VideoView::setSpeed(double speed) {
#ifdef NXKIT_WITH_MPV
    mpv().setSpeed(speed);
#endif
}

void VideoView::setVolume(int64_t volume) {
#ifdef NXKIT_WITH_MPV
    mpv().setVolume(volume);
#endif
}

void VideoView::setMute(bool mute) {
#ifdef NXKIT_WITH_MPV
    mpv().command_async("set", "mute", mute ? "yes" : "no");
#endif
}

void VideoView::setLoop(bool loop) {
#ifdef NXKIT_WITH_MPV
    mpv().command_async("set", "loop-file", loop ? "inf" : "no");
#endif
}

bool VideoView::isValid() const {
#ifdef NXKIT_WITH_MPV
    // MPV 是懒初始化的，播放前 registered 仍为 false，但构建本身支持播放。
    // 只有初始化过且确实没拿到 mpv 上下文，才算不可用。
    return !registered || MPVCore::instance().isValid();
#else
    return false;
#endif
}

bool VideoView::isPlaying() const { return state.playing; }

bool VideoView::isPaused() const { return state.paused; }

bool VideoView::isEnded() const { return state.ended; }

double VideoView::getPlaybackTime() const { return state.currentTime; }

double VideoView::getDuration() const { return state.duration; }

void VideoView::draw(brls::Rect rect, float alpha) {
#ifdef NXKIT_WITH_MPV
    // 从未播放过视频时不要为了画空白区域而初始化 MPV。
    if (registered) mpv().draw(rect, alpha);
#else
    (void)rect;
    (void)alpha;
#endif
}

void VideoView::setStateCallback(StateCallback callback) {
    stateCallback = std::move(callback);
}

void VideoView::fireState() {
    if (stateCallback) stateCallback(state);
}

#ifdef NXKIT_WITH_MPV

void VideoView::registerMpvEvent() {
    if (registered) return;
    registered = true;

    eventToken = MPVCore::instance().getEvent()->subscribe(
        [this](MpvEventEnum event) {
            auto& mpv = MPVCore::instance();
            switch (event) {
                case MpvEventEnum::MPV_LOADED:
                    state.loading = false;
                    state.waiting = false;
                    state.metadataLoaded = true;
                    state.error = false;
                    state.errorText.clear();
                    state.duration = static_cast<double>(mpv.duration);
                    // 文件加载前登记的 external 字幕此时才能挂上。
                    for (const auto& subtitle : pendingSubtitles) {
                        mpv.command_async("sub-add", subtitle, "select");
                    }
                    pendingSubtitles.clear();
                    break;
                case MpvEventEnum::MPV_RESUME:
                    state.playing = true;
                    state.paused = false;
                    state.ended = false;
                    state.loading = false;
                    break;
                case MpvEventEnum::MPV_PAUSE:
                    state.playing = false;
                    state.paused = true;
                    break;
                case MpvEventEnum::MPV_STOP:
                    state.playing = false;
                    state.paused = false;
                    state.ended = true;
                    state.loading = false;
                    state.waiting = false;
                    break;
                case MpvEventEnum::LOADING_START:
                    state.loading = true;
                    state.waiting = true;
                    break;
                case MpvEventEnum::LOADING_END:
                    state.loading = false;
                    state.waiting = false;
                    break;
                case MpvEventEnum::MPV_SEEK_END:
                    state.waiting = false;
                    state.seekSerial += 1;
                    break;
                case MpvEventEnum::UPDATE_DURATION:
                    state.duration = static_cast<double>(mpv.duration);
                    break;
                case MpvEventEnum::UPDATE_PROGRESS:
                    state.currentTime = mpv.getPlaybackTime();
                    state.duration = static_cast<double>(mpv.duration);
                    break;
                case MpvEventEnum::END_OF_FILE:
                    state.playing = false;
                    state.paused = false;
                    state.ended = true;
                    state.currentTime = state.duration;
                    break;
                case MpvEventEnum::MPV_FILE_ERROR:
                    state.error = true;
                    state.errorText = "视频加载失败";
                    state.playing = false;
                    state.loading = false;
                    break;
                default:
                    return;  // no state change worth firing
            }
            fireState();
        });
}

void VideoView::unregisterMpvEvent() {
    if (!registered) return;
    registered = false;
    MPVCore::instance().getEvent()->unsubscribe(eventToken);
}

#endif  // NXKIT_WITH_MPV

}  // namespace nxkit
