//
// nxkit: VideoView implementation — wiliwili-style player facade over MPVCore.
//

#include "nxkit/media/video_view.hpp"

#include <borealis/core/logger.hpp>

namespace nxkit {

VideoView::VideoView() {
#ifdef NXKIT_WITH_MPV
    registerMpvEvent();
#endif
}

VideoView::~VideoView() {
#ifdef NXKIT_WITH_MPV
    unregisterMpvEvent();
#endif
}

void VideoView::setUrl(const std::string& url) {
    currentUrl = url;
#ifdef NXKIT_WITH_MPV
    if (url.empty()) return;
    state = VideoState{};
    state.loading = true;
    fireState();
    MPVCore::instance().setUrl(url);
#else
    (void)url;
    state = VideoState{};
    state.error = true;
    state.errorText = "当前构建未启用视频播放";
    fireState();
#endif
}

void VideoView::resume() {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().resume();
#endif
}

void VideoView::pause() {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().pause();
#endif
}

void VideoView::stop() {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().stop();
#endif
    state.ended = false;
    state.playing = false;
    state.paused = false;
    state.currentTime = 0.0;
    fireState();
}

void VideoView::togglePlay() {
#ifdef NXKIT_WITH_MPV
    auto& mpv = MPVCore::instance();
    if (mpv.isPaused()) {
        mpv.resume();
    } else {
        mpv.pause();
    }
#endif
}

void VideoView::seek(double seconds) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().seek(static_cast<int64_t>(seconds));
#endif
}

void VideoView::seekRelative(double seconds) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().seekRelative(static_cast<int64_t>(seconds));
#endif
}

void VideoView::seekPercent(double percent) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().seekPercent(percent);
#endif
}

void VideoView::setSpeed(double speed) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().setSpeed(speed);
#endif
}

void VideoView::setVolume(int64_t volume) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().setVolume(volume);
#endif
}

void VideoView::setMute(bool mute) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().command_async("set", "mute", mute ? "yes" : "no");
#endif
}

void VideoView::setLoop(bool loop) {
#ifdef NXKIT_WITH_MPV
    MPVCore::instance().command_async("set", "loop-file", loop ? "inf" : "no");
#endif
}

bool VideoView::isValid() const {
#ifdef NXKIT_WITH_MPV
    return MPVCore::instance().isValid();
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
    MPVCore::instance().draw(rect, alpha);
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
                    state.error = false;
                    state.errorText.clear();
                    state.duration = static_cast<double>(mpv.duration);
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
                    break;
                case MpvEventEnum::LOADING_START:
                    state.loading = true;
                    break;
                case MpvEventEnum::LOADING_END:
                    state.loading = false;
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
