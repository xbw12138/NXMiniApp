//
// nxkit: libmpv wrapper (OpenGL), trimmed port of wiliwili's MPVCore.
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <fmt/format.h>

#include <string>
#include <vector>

#ifdef NXKIT_WITH_MPV

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <glad/glad.h>

// nxkit's borealis always uses the OpenGL driver; render the video into an
// offscreen framebuffer and blit it on demand.
#define MPV_USE_FB
// VAOs are core in GL3, which all nxkit targets provide.
#define MPV_USE_VAO

struct GLShader {
    GLuint prog;
    GLuint vbo;
    GLuint ebo;
    GLuint vao;
};

namespace nxkit {

enum class MpvEventEnum {
    MPV_LOADED,
    MPV_RESUME,
    MPV_PAUSE,
    MPV_STOP,
    MPV_FILE_ERROR,
    MPV_IDLE,
    START_FILE,
    LOADING_START,
    LOADING_END,
    END_OF_FILE,
    UPDATE_DURATION,
    UPDATE_PROGRESS,
    MPV_SEEK_END,
    CACHE_SPEED_CHANGE,
    VIDEO_SPEED_CHANGE,
    VIDEO_VOLUME_CHANGE,
    VIDEO_MUTE,
    VIDEO_UNMUTE,
    RESTART,
    RESET,
};

using MpvEvent = brls::Event<MpvEventEnum>;

/// Singleton wrapper around libmpv. Rendering targets an offscreen
/// framebuffer (OpenGL) which is blitted inside draw().
class MPVCore : public brls::Singleton<MPVCore> {
public:
    MPVCore();
    ~MPVCore();

    bool isStopped() const;
    bool isPlaying() const;
    bool isPaused() const;
    double getSpeed() const;
    double getPlaybackTime() const;
    std::string getCacheSpeed() const;
    int64_t getVolume() const;
    bool isValid();

    std::string getString(const std::string& key);
    double getDouble(const std::string& key);
    int64_t getInt(const std::string& key);

    /// Loads a url, `extra` holds additional per-file mpv options, see the mpv manual.
    /// Note: `http-header-fields` does NOT work as a per-file option (mpv parses the
    /// options string as option=value pairs and mangles the header value); use
    /// setHttpHeaders() before setUrl() instead.
    void setUrl(const std::string& url, const std::string& extra = "",
                const std::string& method = "replace");

    /// Sets the global `http-header-fields` property for subsequent loads.
    /// Pass an empty string to clear.
    void setHttpHeaders(const std::string& headers);

    void setVolume(int64_t value);
    void resume();
    void pause();
    void stop();
    void seek(int64_t seconds);
    void seekRelative(int64_t seconds);
    void seekPercent(double value);
    void setSpeed(double value);
    void showOsdText(const std::string& value, int duration = 2000);

    /// Blits the video into the given rect (content coordinates).
    void draw(brls::Rect rect, float alpha = 1.0);

    mpv_render_context* getContext();
    mpv_handle* getHandle();
    MpvEvent* getEvent();

    void restart();
    void reset();

    template <typename... Args>
    void command_async(Args&&... args) {
        if (!mpv) return;
        std::vector<std::string> commands = {
            fmt::format("{}", std::forward<Args>(args))...};
        _command_async(commands);
    }

    void _command_async(const std::vector<std::string>& commands);

    // core states
    int64_t duration = 0;      // second
    int64_t cache_speed = 0;   // Bps
    int64_t volume = 100;
    double video_speed = 0;
    bool video_paused = false;
    bool video_stopped = true;
    bool video_seeking = false;
    bool video_playing = false;
    bool video_eof = false;
    double playback_time = 0;
    double percent_pos = 0;
    int64_t video_progress = 0;
    int mpv_error_code = 0;
    std::string filepath;

    // 低画质解码，剔除解码过程中的部分步骤，可以用来节省cpu。
    // 与 wiliwili 在 Switch 上的默认值保持一致：软解高码率视频时明显更流畅。
    inline static bool LOW_QUALITY = true;
    // 视频内存缓存大小（MB），0 表示关闭缓存。
    // 网络流（HLS/直链）留一点缓冲，避免弱网时频繁中断重连。
    inline static int INMEMORY_CACHE = 20;
    inline static bool TERMINAL = false;
    // 硬件解码：有可用解码器时走硬解，没有则自动回退软解（Switch 端无硬解）。
    inline static bool HARDWARE_DEC = true;
    inline static std::string PLAYER_HWDEC_METHOD = "auto";
    inline static bool AUTO_PLAY = true;
    inline static int VIDEO_VOLUME = 100;

private:
    mpv_handle* mpv = nullptr;
    mpv_render_context* mpv_context = nullptr;
    brls::Rect rect = {0, 0, 1920, 1080};

#ifdef MPV_USE_FB
    GLint default_framebuffer = 0;
    GLuint media_framebuffer = 0;
    GLuint media_texture = 0;
    GLShader shader{0};
    mpv_opengl_fbo mpv_fbo{0, 1920, 1080};
    int flip_y{1};
    bool redraw = false;
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    float vertices[20] = {1.0f,  1.0f,  0.0f, 1.0f, 1.0f, 1.0f,  -1.0f,
                          0.0f,  1.0f,  0.0f, -1.0f, -1.0f, 0.0f, 0.0f,
                          0.0f,  -1.0f, 1.0f, 0.0f, 0.0f,  1.0f};
#else
    GLint default_framebuffer = 0;
    mpv_opengl_fbo mpv_fbo{0, 1920, 1080};
    int flip_y{1};
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#endif

    MpvEvent mpvCoreEvent;
    brls::Event<bool>::Subscription focusSubscription;

    void eventMainLoop();
    void initializeVideo();
    void uninitializeVideo();
    void init();
    void clean();
    void setFrameSize(brls::Rect rect);

    static void on_update(void* self);
    static void on_wakeup(void* self);
};

}  // namespace nxkit

#endif  // NXKIT_WITH_MPV
