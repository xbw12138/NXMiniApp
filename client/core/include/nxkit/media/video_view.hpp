//
// nxkit: VideoView — wiliwili-style player facade over MPVCore.
//
// Wraps the libmpv singleton (nxkit::MPVCore) behind the same high-level
// interface wiliwili exposes through its VideoView: setUrl / resume / pause /
// stop / seek / setSpeed / setVolume / draw, plus an MpvEvent subscription
// that forwards playback state to user-supplied callbacks.
//
// Miniapp (and other callers) should talk to VideoView, never to MPVCore
// directly — this keeps the mpv dependency contained and matches wiliwili's
// layering (VideoView owns the mpv event subscription, MPVCore is the low
// level handle).
//

#pragma once

#include <borealis/core/event.hpp>
#include <borealis/core/geometry.hpp>
#include <borealis/core/singleton.hpp>

#include <functional>
#include <string>

#ifdef NXKIT_WITH_MPV
#include "nxkit/media/mpv_core.hpp"
#endif

namespace nxkit {

/// Playback state snapshot pushed to the onState callback.
struct VideoState {
    bool playing = false;
    bool paused = false;
    bool ended = false;
    bool error = false;
    bool loading = false;
    double currentTime = 0.0;  // seconds
    double duration = 0.0;     // seconds
    std::string errorText;
};

/// wiliwili-style player facade. Owns the MpvEvent subscription; callers
/// register callbacks instead of polling mpv state every frame.
class VideoView : public brls::Singleton<VideoView> {
public:
    using StateCallback = std::function<void(const VideoState&)>;

    VideoView();
    ~VideoView();

    /// Load a new media url. Passing an empty url is a no-op.
    void setUrl(const std::string& url);
    /// Resume playback (no-op if nothing loaded).
    void resume();
    /// Pause playback.
    void pause();
    /// Stop and unload the current media.
    void stop();
    /// Toggle between play and pause.
    void togglePlay();

    /// Seek to an absolute position in seconds.
    void seek(double seconds);
    /// Seek to a relative offset in seconds.
    void seekRelative(double seconds);
    /// Seek to a percentage [0..1].
    void seekPercent(double percent);

    void setSpeed(double speed);
    void setVolume(int64_t volume);
    void setMute(bool mute);
    void setLoop(bool loop);

    /// State queries (cached from the last MpvEvent).
    bool isValid() const;
    bool isPlaying() const;
    bool isPaused() const;
    bool isEnded() const;
    double getPlaybackTime() const;
    double getDuration() const;

    /// Blit the decoded video into the given content-area rect. Must be
    /// called after NanoVG has finished its frame (same constraint as
    /// MPVCore::draw).
    void draw(brls::Rect rect, float alpha = 1.0f);

    /// Register a callback fired on every meaningful state change (play,
    /// pause, ended, error, time update, duration update). Replaces any
    /// previously registered callback.
    void setStateCallback(StateCallback callback);

    /// Last resolved url passed to setUrl.
    const std::string& getUrl() const { return currentUrl; }

private:
    void fireState();

#ifdef NXKIT_WITH_MPV
    void registerMpvEvent();
    void unregisterMpvEvent();

    MpvEvent::Subscription eventToken;
    bool registered = false;
#endif
    StateCallback stateCallback;
    std::string currentUrl;
    VideoState state;
};

}  // namespace nxkit
