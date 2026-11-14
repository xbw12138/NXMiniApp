//
// nxkit: libmpv wrapper (OpenGL), trimmed port of wiliwili's MPVCore.
//

#include "nxkit/media/mpv_core.hpp"

#ifdef NXKIT_WITH_MPV

#include <clocale>
#include <cmath>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <fmt/format.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "nxkit/paths.hpp"

#ifdef MPV_USE_FB

#define SHADER_VERSION "#version 150 core\n"

const char* vertexShaderSource = SHADER_VERSION
    "in vec3 aPos;\n"
    "in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos, 1.0);\n"
    "   TexCoord = aTexCoord;\n"
    "}\0";
const char* fragmentShaderSource = SHADER_VERSION
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D ourTexture;\n"
    "uniform float Alpha;\n"
    "void main()\n"
    "{\n"
    "   FragColor = texture(ourTexture, TexCoord);\n"
    "   FragColor.a = Alpha;\n"
    "}\n\0";

#define CONCATENATE3(s1, s2, s3) s1##s2##s3
#define CONCATENATE2(s1, s2) s1##s2
#define checkGLError(type, id)                                                 \
    {                                                                          \
        char* info = nullptr;                                                  \
        int length = 0;                                                        \
        CONCATENATE3(glGet, type, iv)(id, GL_COMPILE_STATUS, &length);         \
        if (length > 0) {                                                      \
            info = (char*)malloc(length);                                      \
            if (info) {                                                        \
                CONCATENATE3(glGet, type, InfoLog)(id, length, &length, info); \
            }                                                                  \
        }                                                                      \
        if (info) {                                                            \
            brls::Logger::error("Failed to load the shader: {}", info);        \
            free(info);                                                        \
        } else {                                                               \
            brls::Logger::error("Failed to load the shader");                  \
        }                                                                      \
        CONCATENATE2(glDelete, type)(id);                                      \
    }

static GLuint createShader(GLint type, const char* source) {
    GLuint shader = glCreateShader(type);
    int success;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        checkGLError(Shader, shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(GLuint s1, GLuint s2) {
    if (s1 == 0 || s2 == 0) return 0;
    GLuint program = glCreateProgram();
    int success;
    glAttachShader(program, s1);
    glAttachShader(program, s2);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        checkGLError(Program, program);
        return 0;
    }
    return program;
}
#endif  // MPV_USE_FB

static inline void check_error(int status) {
    if (status < 0) {
        brls::Logger::error("MPV ERROR ====> {}", mpv_error_string(status));
    }
}

static void* get_proc_address(void* unused, const char* name) {
    return (void*)glfwGetProcAddress(name);
}

namespace nxkit {

void MPVCore::on_update(void* self) {
    brls::sync([]() {
        uint64_t flags = mpv_render_context_update(MPVCore::instance().getContext());
#ifdef MPV_USE_FB
        MPVCore::instance().redraw = flags & MPV_RENDER_UPDATE_FRAME;
        if (MPVCore::instance().redraw) {
            mpv_render_context_render(MPVCore::instance().mpv_context,
                                      MPVCore::instance().mpv_params);
            glBindFramebuffer(GL_FRAMEBUFFER, MPVCore::instance().default_framebuffer);
            glViewport(0, 0, (GLsizei)brls::Application::windowWidth,
                       (GLsizei)brls::Application::windowHeight);
            mpv_render_context_report_swap(MPVCore::instance().mpv_context);
        }
#else
        (void)flags;
#endif
    });
}

void MPVCore::on_wakeup(void* self) {
    brls::sync([]() { MPVCore::instance().eventMainLoop(); });
}

MPVCore::MPVCore() {
    this->init();
    brls::Application::getExitEvent()->subscribe([this]() { this->clean(); });
}

MPVCore::~MPVCore() = default;

void MPVCore::init() {
    brls::Logger::info("mpv init begin");
    setlocale(LC_NUMERIC, "C");
    this->mpv = mpv_create();
    if (!mpv) {
        brls::Logger::error("mpv create failed");
        return;
    }
    std::string confDir = nxkit::dataDirectory();
    // misc
    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "gpu-shader-cache-dir",
                          fmt::format("{}cache", confDir).c_str());
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "audio-channels", "stereo");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "loop-file", "no");
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "video-timing-offset", "0");  // 60fps
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "hr-seek", "yes");
    mpv_set_option_string(mpv, "reset-on-next-file", "speed,pause");
    mpv_set_option_string(mpv, "vo", "libmpv");

    if (MPVCore::LOW_QUALITY) {
        brls::Logger::info("lavc: skip loop filter and set fast decode");
        mpv_set_option_string(mpv, "vd-lavc-skiploopfilter", "all");
        mpv_set_option_string(mpv, "vd-lavc-fast", "yes");
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2, 2)
        mpv_set_option_string(mpv, "profile", "fast");
#else
        // 老版本 mpv 没有 fast profile，手工关掉最耗 CPU 的缩放/抖动步骤。
        mpv_set_option_string(mpv, "scale", "bilinear");
        mpv_set_option_string(mpv, "dscale", "bilinear");
        mpv_set_option_string(mpv, "dither", "no");
        mpv_set_option_string(mpv, "correct-downscaling", "no");
        mpv_set_option_string(mpv, "linear-downscaling", "no");
        mpv_set_option_string(mpv, "sigmoid-upscaling", "no");
        mpv_set_option_string(mpv, "hdr-compute-peak", "no");
        mpv_set_option_string(mpv, "allow-delayed-peak-detect", "yes");
#endif
    }

    if (MPVCore::INMEMORY_CACHE) {
        brls::Logger::info("set memory cache: {}MB", MPVCore::INMEMORY_CACHE);
        mpv_set_option_string(
            mpv, "demuxer-max-bytes",
            fmt::format("{}MiB", MPVCore::INMEMORY_CACHE).c_str());
        mpv_set_option_string(
            mpv, "demuxer-max-back-bytes",
            fmt::format("{}MiB", MPVCore::INMEMORY_CACHE / 2).c_str());
    } else {
        mpv_set_option_string(mpv, "cache", "no");
    }

    if (HARDWARE_DEC) {
        mpv_set_option_string(mpv, "hwdec", PLAYER_HWDEC_METHOD.c_str());
        brls::Logger::info("MPV hardware decode: {}", PLAYER_HWDEC_METHOD);
    } else {
        mpv_set_option_string(mpv, "hwdec", "no");
    }

    // Making the loading process faster
#if defined(__SWITCH__)
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    // This should fix random crash, but I don't know why.
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");
#endif
    mpv_set_option_string(mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_option_string(mpv, "demuxer-lavf-probescore", "24");

    if (MPVCore::TERMINAL) {
        mpv_set_option_string(mpv, "terminal", "yes");
        mpv_set_option_string(mpv, "msg-level", "all=v");
    }

    if (mpv_initialize(mpv) < 0) {
        // 内存不足时初始化会失败：这里不能再 fatal，否则整个容器直接退出，
        // 上层连「播放失败」都提示不出来。
        brls::Logger::error("Could not initialize mpv context");
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        return;
    }

    // 把 mpv/ffmpeg 的告警与错误转发到容器日志（miniapp.log），
    // 真机播放失败时才能看到具体原因（打不开流、编码不支持等）。
    mpv_request_log_messages(mpv, MPVCore::TERMINAL ? "v" : "warn");

    // set observe properties
    check_error(mpv_observe_property(mpv, 1, "core-idle", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 2, "eof-reached", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 3, "duration", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 4, "playback-time", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 5, "cache-speed", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 6, "percent-pos", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 7, "paused-for-cache", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 10, "speed", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 11, "volume", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 12, "pause", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 13, "playback-abort", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 14, "seeking", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 16, "path", MPV_FORMAT_STRING));

    // init renderer params
    int advanced_control{1};
    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
        {MPV_RENDER_PARAM_INVALID, nullptr}};

    if (mpv_render_context_create(&mpv_context, mpv, params) < 0) {
        brls::Logger::error("failed to initialize mpv GL context");
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        return;
    }

    brls::Logger::info("MPV Version: {}", mpv_get_property_string(mpv, "mpv-version"));
    brls::Logger::info("FFMPEG Version: {}",
                       mpv_get_property_string(mpv, "ffmpeg-version"));
    setVolume(MPVCore::VIDEO_VOLUME);

    mpv_set_wakeup_callback(mpv, on_wakeup, this);
    mpv_render_context_set_update_callback(mpv_context, on_update, this);

    focusSubscription = brls::Application::getWindowFocusChangedEvent()->subscribe(
        [this](bool focus) {
            static bool playing = false;
            if (focus) {
                if (playing) resume();
            } else {
                playing = isPlaying();
                pause();
            }
        });

    this->initializeVideo();
    brls::Logger::info("mpv init done");
}

void MPVCore::clean() {
    // 初始化失败时 mpv/mpv_context 可能为空，退出流程仍会走到这里。
    if (!this->mpv) return;
    check_error(mpv_command_string(this->mpv, "quit"));

    brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);

    brls::Logger::info("uninitialize Video");
    this->uninitializeVideo();

    brls::Logger::info("trying free mpv context");
    if (this->mpv_context) {
        mpv_render_context_free(this->mpv_context);
        this->mpv_context = nullptr;
    }

    brls::Logger::info("trying terminate mpv");
    if (this->mpv) {
        mpv_terminate_destroy(this->mpv);
        this->mpv = nullptr;
    }
}

void MPVCore::restart() {
    this->clean();
    this->init();
    mpvCoreEvent.fire(MpvEventEnum::RESTART);
    setFrameSize(rect);
}

void MPVCore::uninitializeVideo() {
#ifdef MPV_USE_FB
    if (media_framebuffer != 0) glDeleteFramebuffers(1, &media_framebuffer);
    if (media_texture != 0) glDeleteTextures(1, &media_texture);
    if (shader.vbo != 0) glDeleteBuffers(1, &shader.vbo);
    if (shader.ebo != 0) glDeleteBuffers(1, &shader.ebo);
    if (shader.prog != 0) glDeleteProgram(shader.prog);
#ifdef MPV_USE_VAO
    if (shader.vao != 0) glDeleteVertexArrays(1, &shader.vao);
    shader.vao = 0;
#endif
    media_framebuffer = 0;
    media_texture = 0;
    shader.vbo = 0;
    shader.ebo = 0;
    shader.prog = 0;
#endif
}

void MPVCore::initializeVideo() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &default_framebuffer);

#ifdef MPV_USE_FB
    if (this->media_texture != 0) return;
    brls::Logger::debug("initializeGL");

    // create texture
    glGenTextures(1, &this->media_texture);
    glBindTexture(GL_TEXTURE_2D, this->media_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)brls::Application::windowWidth,
                 (int)brls::Application::windowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // create frame buffer
    glGenFramebuffers(1, &this->media_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, this->media_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           this->media_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        brls::Logger::error("glCheckFramebufferStatus failed");
        return;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
    this->mpv_fbo.fbo = (int)this->media_framebuffer;

    // build and compile our shader program
    GLuint vertexShader = createShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint shaderProgram = linkProgram(vertexShader, fragmentShader);

    if (vertexShader) glDeleteShader(vertexShader);
    if (fragmentShader) glDeleteShader(fragmentShader);
    this->shader.prog = shaderProgram;

    // set up vertex data (and buffer(s)) and configure vertex attributes
    unsigned int indices[] = {0, 1, 3, 1, 2, 3};
    glGenBuffers(1, &this->shader.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, this->shader.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &this->shader.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->shader.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

#ifdef MPV_USE_VAO
    glGenVertexArrays(1, &this->shader.vao);
    glBindVertexArray(this->shader.vao);
    glBindBuffer(GL_ARRAY_BUFFER, this->shader.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->shader.ebo);

    GLuint aPos = glGetAttribLocation(shaderProgram, "aPos");
    glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)nullptr);
    glEnableVertexAttribArray(aPos);

    GLuint aTexCoord = glGetAttribLocation(shaderProgram, "aTexCoord");
    glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(aTexCoord);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
#endif
    brls::Logger::debug("initializeGL done");
#else
    mpv_fbo.fbo = default_framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
#endif
}

void MPVCore::setFrameSize(brls::Rect r) {
    rect = r;
    if (std::isnan(rect.getWidth()) || std::isnan(rect.getHeight())) return;

#ifdef MPV_USE_FB
    if (this->media_texture == 0) return;
    int drawWidth = rect.getWidth() * brls::Application::windowScale;
    int drawHeight = rect.getHeight() * brls::Application::windowScale;

    if (drawWidth == 0 || drawHeight == 0) return;
    brls::Logger::debug("MPVCore::setFrameSize: {}/{}", drawWidth, drawHeight);
    glBindTexture(GL_TEXTURE_2D, this->media_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, drawWidth, drawHeight, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    this->mpv_fbo.w = drawWidth;
    this->mpv_fbo.h = drawHeight;

    float new_min_x = rect.getMinX() / brls::Application::contentWidth * 2 - 1;
    float new_min_y = 1 - rect.getMinY() / brls::Application::contentHeight * 2;
    float new_max_x = rect.getMaxX() / brls::Application::contentWidth * 2 - 1;
    float new_max_y = 1 - rect.getMaxY() / brls::Application::contentHeight * 2;

    vertices[0] = new_max_x;
    vertices[1] = new_min_y;
    vertices[5] = new_max_x;
    vertices[6] = new_max_y;
    vertices[10] = new_min_x;
    vertices[11] = new_max_y;
    vertices[15] = new_min_x;
    vertices[16] = new_min_y;

    glBindBuffer(GL_ARRAY_BUFFER, this->shader.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 在视频暂停时调整纹理尺寸，视频画面会被清空为黑色，强制重新绘制一次
    mpv_render_context_render(mpv_context, mpv_params);
    glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
    glViewport(0, 0, (GLsizei)brls::Application::windowWidth,
               (GLsizei)brls::Application::windowHeight);
    mpv_render_context_report_swap(mpv_context);
#else
    this->mpv_fbo.w = brls::Application::windowWidth;
    this->mpv_fbo.h = brls::Application::windowHeight;
    if (brls::Application::contentWidth < rect.getMaxX() ||
        brls::Application::contentHeight < rect.getMaxY())
        return;
    command_async("set", "video-margin-ratio-right",
                  (brls::Application::contentWidth - rect.getMaxX()) /
                      brls::Application::contentWidth);
    command_async("set", "video-margin-ratio-bottom",
                  (brls::Application::contentHeight - rect.getMaxY()) /
                      brls::Application::contentHeight);
    command_async("set", "video-margin-ratio-top",
                  rect.getMinY() / brls::Application::contentHeight);
    command_async("set", "video-margin-ratio-left",
                  rect.getMinX() / brls::Application::contentWidth);
#endif
}

bool MPVCore::isValid() { return mpv_context != nullptr; }

void MPVCore::draw(brls::Rect area, float alpha) {
    if (mpv_context == nullptr) return;
    if (!(this->rect == area)) setFrameSize(area);

#ifdef MPV_USE_FB
    // OpenGL 将 FBO 绘制到屏幕上
    glUseProgram(shader.prog);
    glBindTexture(GL_TEXTURE_2D, this->media_texture);
#ifdef MPV_USE_VAO
    glBindVertexArray(shader.vao);
#else
    glBindBuffer(GL_ARRAY_BUFFER, shader.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shader.ebo);
    static GLuint aPos = glGetAttribLocation(shader.prog, "aPos");
    glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)nullptr);
    glEnableVertexAttribArray(aPos);

    static GLuint aTexCoord = glGetAttribLocation(shader.prog, "aTexCoord");
    glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(aTexCoord);
#endif

    // Set alpha
    if (alpha < 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    static GLint alphaID = glGetUniformLocation(shader.prog, "Alpha");
    glUniform1f(alphaID, alpha);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
#else
    // 只在非透明时绘制视频，可以避免退出页面时视频画面残留
    if (alpha >= 1) {
        mpv_render_context_render(this->mpv_context, mpv_params);
        glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
        glViewport(0, 0, brls::Application::windowWidth,
                   brls::Application::windowHeight);
        mpv_render_context_report_swap(this->mpv_context);

        // 画背景来覆盖mpv的黑色边框
        if (rect.getWidth() < brls::Application::contentWidth) {
            auto* vg = brls::Application::getNVGContext();
            nvgBeginPath(vg);
            nvgFillColor(vg, brls::Application::getTheme().getColor("brls/background"));
            nvgRect(vg, 0, 0, rect.getMinX(), brls::Application::contentHeight);
            nvgRect(vg, rect.getMaxX(), 0,
                    brls::Application::contentWidth - rect.getMaxX(),
                    brls::Application::contentHeight);
            nvgRect(vg, rect.getMinX() - 1, 0, rect.getWidth() + 2, rect.getMinY());
            nvgRect(vg, rect.getMinX() - 1, rect.getMaxY(), rect.getWidth() + 2,
                    brls::Application::contentHeight - rect.getMaxY());
            nvgFill(vg);
        }
    }
#endif
}

mpv_render_context* MPVCore::getContext() { return this->mpv_context; }

mpv_handle* MPVCore::getHandle() { return this->mpv; }

MpvEvent* MPVCore::getEvent() { return &this->mpvCoreEvent; }

std::string MPVCore::getCacheSpeed() const {
    if (cache_speed >> 20 > 0) {
        return fmt::format("{:.2f} MB/s", (cache_speed >> 10) / 1024.0f);
    } else if (cache_speed >> 10 > 0) {
        return fmt::format("{:.2f} KB/s", cache_speed / 1024.0f);
    } else {
        return fmt::format("{} B/s", cache_speed);
    }
}

void MPVCore::eventMainLoop() {
    while (true) {
        auto event = mpv_wait_event(this->mpv, 0);
        switch (event->event_id) {
            case MPV_EVENT_NONE:
                return;
            case MPV_EVENT_LOG_MESSAGE: {
                auto log = (mpv_event_log_message*)event->data;
                if (log->log_level <= MPV_LOG_LEVEL_ERROR) {
                    brls::Logger::error("{}: {}", log->prefix, log->text);
                } else if (log->log_level <= MPV_LOG_LEVEL_WARN) {
                    brls::Logger::warning("{}: {}", log->prefix, log->text);
                } else if (log->log_level <= MPV_LOG_LEVEL_INFO) {
                    brls::Logger::info("{}: {}", log->prefix, log->text);
                } else if (log->log_level <= MPV_LOG_LEVEL_V) {
                    brls::Logger::debug("{}: {}", log->prefix, log->text);
                } else {
                    brls::Logger::verbose("{}: {}", log->prefix, log->text);
                }
            } break;
            case MPV_EVENT_FILE_LOADED:
                brls::Logger::info("========> MPV_EVENT_FILE_LOADED");
                mpvCoreEvent.fire(MpvEventEnum::MPV_LOADED);
                video_progress = 0;
                mpvCoreEvent.fire(MpvEventEnum::UPDATE_PROGRESS);
                command_async("playlist-clear");

                if (AUTO_PLAY) {
                    mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
                    this->resume();
                } else {
                    mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
                    this->pause();
                }
                break;
            case MPV_EVENT_START_FILE:
                brls::Logger::info("========> MPV_EVENT_START_FILE");
                mpvCoreEvent.fire(MpvEventEnum::START_FILE);
                mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                brls::Logger::info("========> MPV_EVENT_PLAYBACK_RESTART");
                video_stopped = false;
                mpvCoreEvent.fire(MpvEventEnum::LOADING_END);
                break;
            case MPV_EVENT_END_FILE: {
                brls::Logger::info("========> MPV_STOP");
                mpvCoreEvent.fire(MpvEventEnum::MPV_STOP);
                video_stopped = true;
                auto node = (mpv_event_end_file*)event->data;
                if (node->reason == MPV_END_FILE_REASON_ERROR) {
                    mpv_error_code = node->error;
                    brls::Logger::error("========> MPV ERROR: {}",
                                        mpv_error_string(node->error));
                    mpvCoreEvent.fire(MpvEventEnum::MPV_FILE_ERROR);
                }
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* data = ((mpv_event_property*)event->data)->data;
                switch (event->reply_userdata) {
                    case 1:
                        if (data) {
                            bool playing = *(int*)data == 0;
                            if (playing != video_playing) {
                                video_playing = playing;
                                mpvCoreEvent.fire(MpvEventEnum::MPV_IDLE);
                            }
                            video_playing = playing;
                        }
                        break;
                    case 2:
                        if (data) video_eof = *(int*)data;
                        // 视频自然结束时先触发一次 EOF，stop / 换新文件会先触发
                        // STOP 再触发 EOF，排除后者避免多次触发
                        if (video_eof && !video_stopped) {
                            brls::Logger::info("========> END OF FILE");
                            mpvCoreEvent.fire(MpvEventEnum::END_OF_FILE);
                        }
                        break;
                    case 3:
                        if (((mpv_event_property*)event->data)->data)
                            duration = *(int64_t*)((mpv_event_property*)event->data)
                                           ->data;
                        if (duration != 0) {
                            brls::Logger::debug("========> duration: {}", duration);
                            mpvCoreEvent.fire(MpvEventEnum::UPDATE_DURATION);
                        }
                        break;
                    case 4:
                        if (((mpv_event_property*)event->data)->data) {
                            playback_time =
                                *(double*)((mpv_event_property*)event->data)->data;
                            if (video_progress != (int64_t)playback_time) {
                                video_progress = (int64_t)playback_time;
                                mpvCoreEvent.fire(MpvEventEnum::UPDATE_PROGRESS);
                            }
                        }
                        break;
                    case 5:
                        if (((mpv_event_property*)event->data)->data) {
                            cache_speed =
                                *(int64_t*)((mpv_event_property*)event->data)->data;
                            mpvCoreEvent.fire(MpvEventEnum::CACHE_SPEED_CHANGE);
                        }
                        break;
                    case 6:
                        if (((mpv_event_property*)event->data)->data) {
                            percent_pos =
                                *(double*)((mpv_event_property*)event->data)->data;
                        }
                        break;
                    case 7:
                        if (!data) break;
                        if (*(int*)data) {
                            brls::Logger::info("========> VIDEO PAUSED FOR CACHE");
                            mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
                        } else {
                            brls::Logger::info("========> VIDEO RESUME FROM CACHE");
                            mpvCoreEvent.fire(MpvEventEnum::LOADING_END);
                        }
                        break;
                    case 10:
                        if (data) {
                            video_speed = *(double*)data;
                            mpvCoreEvent.fire(MpvEventEnum::VIDEO_SPEED_CHANGE);
                        }
                        break;
                    case 11:
                        if (data) {
                            if (*(int64_t*)data > 0 && volume == 0) {
                                mpvCoreEvent.fire(MpvEventEnum::VIDEO_UNMUTE);
                            } else if (*(int64_t*)data == 0 && volume > 0) {
                                mpvCoreEvent.fire(MpvEventEnum::VIDEO_MUTE);
                            }
                            volume = *(int64_t*)data;
                            mpvCoreEvent.fire(MpvEventEnum::VIDEO_VOLUME_CHANGE);
                        }
                        break;
                    case 12:
                        if (data) video_paused = *(int*)data;
                        if (video_paused) {
                            brls::Logger::info("========> PAUSE");
                            mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
                        } else if (!video_stopped) {
                            brls::Logger::info("========> RESUME");
                            mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
                        }
                        break;
                    case 13:
                        if (data) video_stopped = *(int*)data;
                        break;
                    case 14:
                        if (data) {
                            bool seeking = *(int*)data != 0;
                            if (seeking && !video_seeking) {
                                brls::Logger::info("========> VIDEO SEEKING");
                                mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
                            } else if (!seeking && video_seeking) {
                                brls::Logger::info("========> VIDEO SEEK END");
                                mpvCoreEvent.fire(MpvEventEnum::MPV_SEEK_END);
                            }
                            video_seeking = seeking;
                        }
                        break;
                    case 16:
                        if (data) filepath = *(char**)data;
                        break;
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }
    }
}

void MPVCore::reset() {
    brls::Logger::debug("MPVCore::reset");
    mpvCoreEvent.fire(MpvEventEnum::RESET);
    this->percent_pos = 0;
    this->duration = 0;
    this->cache_speed = 0;
    this->playback_time = 0;
    this->video_progress = 0;
    this->mpv_error_code = 0;

    // 切换视频前设置渲染尺寸，顺便将上一条视频的最后一帧画面清空
    setFrameSize(rect);
}

void MPVCore::setUrl(const std::string& url, const std::string& extra,
                     const std::string& method) {
    brls::Logger::debug("{} Url: {}, extra: {}", method, url, extra);
    if (extra.empty()) {
        command_async("loadfile", url, method);
    } else {
        command_async("loadfile", url, method, "0", extra);
    }
}

void MPVCore::setHttpHeaders(const std::string& headers) {
    command_async("set", "http-header-fields", headers);
}

void MPVCore::setVolume(int64_t value) {
    if (value < 0 || value > 100) return;
    command_async("set", "volume", value);
    MPVCore::VIDEO_VOLUME = (int)value;
}

int64_t MPVCore::getVolume() const { return this->volume; }

void MPVCore::resume() { command_async("set", "pause", "no"); }

void MPVCore::pause() { command_async("set", "pause", "yes"); }

void MPVCore::stop() { command_async("stop"); }

void MPVCore::seek(int64_t p) { command_async("seek", p, "absolute"); }

void MPVCore::seekRelative(int64_t p) { command_async("seek", p, "relative"); }

void MPVCore::seekPercent(double p) { command_async("seek", p * 100, "absolute-percent"); }

bool MPVCore::isStopped() const { return video_stopped; }

bool MPVCore::isPlaying() const { return video_playing; }

bool MPVCore::isPaused() const { return video_paused; }

double MPVCore::getSpeed() const { return video_speed; }

void MPVCore::setSpeed(double value) {
    if (video_speed != value) command_async("set", "speed", value);
}

std::string MPVCore::getString(const std::string& key) {
    char* value = nullptr;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_STRING, &value);
    if (!value) return "";
    std::string result = std::string{value};
    mpv_free(value);
    return result;
}

double MPVCore::getDouble(const std::string& key) {
    double value = 0;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_DOUBLE, &value);
    return value;
}

int64_t MPVCore::getInt(const std::string& key) {
    int64_t value = 0;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_INT64, &value);
    return value;
}

double MPVCore::getPlaybackTime() const { return playback_time; }

void MPVCore::showOsdText(const std::string& value, int d) {
    command_async("show-text", value, d);
}

void MPVCore::_command_async(const std::vector<std::string>& commands) {
    std::vector<const char*> res;
    res.reserve(commands.size() + 1);
    for (auto& i : commands) {
        res.emplace_back(i.c_str());
    }
    res.emplace_back(nullptr);
    int ret = mpv_command_async(mpv, 0, res.data());
    if (ret < 0) {
        brls::Logger::error("mpv_command_async failed: {} (cmd={})",
                            mpv_error_string(ret), commands.empty() ? "" : commands[0]);
    }
}

}  // namespace nxkit

#endif  // NXKIT_WITH_MPV
