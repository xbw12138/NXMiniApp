//
// miniapp: 承载 WXML 渲染树的 borealis 视图。
//

#pragma once

#include <borealis.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nxkit/media/video_view.hpp>

#include "miniapp/js_runtime.hpp"
#include "miniapp/json.hpp"
#include "miniapp/logic_thread.hpp"
#include "miniapp/render_node.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"

namespace nxkit {
class AudioPlayer;
}

namespace miniapp {

class MiniappView : public brls::View, public JsRuntimeHost {
public:
    // appTitleHint：启动加载页显示的标题（包还没解析出来时先给个名字）。
    explicit MiniappView(std::string packagePath = "", std::string appTitleHint = "");
    ~MiniappView() override;

    // 根页面再按 B 时的兜底回调。为空（默认）时直接 popActivity；
    // 被内嵌到启动器时由启动器注入「退出到桌面」确认弹窗。
    std::function<void()> onRequestExit;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override;

    // ---- JsRuntimeHost ----
    void onDataChanged() override;
    void showToast(const std::string& title, const std::string& icon,
                   int durationMs) override;
    void showLoading(const std::string& title) override;
    void hideLoading() override;
    void showModal(const std::string& title, const std::string& content,
                   bool showCancel,
                   std::function<void(bool confirm)> done) override;
    void setNavigationBarTitle(const std::string& title) override;
    void httpRequest(const std::string& method, const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const std::string& body, int timeoutMs, HttpDone done) override;
    void smbConnect(const std::string& server, const std::string& share,
                    const std::string& user, const std::string& password,
                    const std::string& domain, SmbDone done) override;
    void smbListFiles(const std::string& id, const std::string& path,
                      SmbDone done) override;
    void smbScan(const std::string& id, const std::string& path, int maxDepth, int maxEntries,
                 SmbDone done, std::function<void(int, int)> progress = {}) override;
    void smbCancelScan() override;
    void mediaScan(const std::string& sourceId, const std::string& smbId, const std::string& root,
                   int maxDepth, int maxFiles, SmbDone done) override;
    void mediaLibrary(const std::string& sourceId, const std::string& root, int offset, int limit,
                      SmbDone done) override;
    void smbDisconnect(const std::string& id) override;
    std::string smbStreamUrl(const std::string& id, const std::string& path) override;
    bool navigateTo(const std::string& url) override;
    bool navigateBack(int delta) override;
    bool redirectTo(const std::string& url) override;
    bool reLaunch(const std::string& url) override;
    bool launchMiniApp(const std::string& id, const std::string& title = "") override;
    JsonValue recentMiniApps() override;
    std::string deviceId() override;
    JsonValue systemInfo() override;
    void vibrateShort() override;
    bool storageGet(const std::string& key, std::string& value) override;
    void storageSet(const std::string& key, const std::string& value) override;
    void storageRemove(const std::string& key) override;
    void storageClear() override;
    std::vector<std::string> storageKeys() override;
    std::string clipboardGet() override;
    void clipboardSet(const std::string& text) override;
    bool videoControl(const std::string& id, const std::string& action,
                      double value, const std::string& text = std::string()) override;
    JsonValue gamepadState() override;
    bool audioControl(const std::string& id, const std::string& action,
                      double value = 0.0,
                      const std::string& text = std::string()) override;
    JsonValue audioState(const std::string& id) override;

private:
    using LocalValue = EvalLocal;

    struct PageState {
        std::string path;      // pages/index/index
        std::string dir;       // 页面文件目录
        std::string wxmlText;
        std::string wxssText;
        std::string jsText;
        std::string error;
        std::string background = "#141518";
        std::string title;

        WxmlNode wxmlRoot;
        WxssStylesheet stylesheet;
        JsonValue data;
        // UI 侧 data 副本对应的脚本版本号（见 JsRuntime::dataVersion）。
        std::uint64_t dataVersion = 0;
        std::shared_ptr<JsRuntime> runtime;
        std::unique_ptr<RenderNode> root;

        bool dirty = true;
        // setData 后有节点结构性/尺寸变化，需要重新同步 Yoga 并重算布局。
        bool layoutDirty = false;
        bool loaded = false;
        bool readyCalled = false;

        // 组件本地状态（按稳定路径保存，跨 setData 重建保留）。
        std::map<std::string, float> scrollOffsets;
        std::map<std::string, bool> toggles;
        std::map<std::string, std::string> radioSelections;
        std::map<std::string, float> sliderValues;
        std::map<std::string, std::string> inputValues;
        std::map<std::string, int> swiperIndex;
        std::map<std::string, double> swiperLast;

        // video 播放状态（按稳定路径保存）。
        std::map<std::string, bool> videoPlaying;
        std::map<std::string, bool> videoStarted;
        std::map<std::string, std::string> videoLoadedSrc;
        std::map<std::string, double> videoTime;
        std::map<std::string, double> videoLastUpdate;
    };

    struct ModalState {
        bool active = false;
        std::string title;
        std::string content;
        bool showCancel = true;
        int focus = 0;  // 0 取消，1 确定
        std::function<void(bool)> done;
    };

    void loadPackage();
    bool openPage(const std::string& url, bool push);
    void closeCurrentPage();
    void loadPageFiles(PageState& page, const std::string& path, bool withRuntime);
    void cleanupPage(PageState& page);
    void rebuildTree(PageState& page);
    // 把新构建的快照合并进现有渲染树：按 path 复用节点，只把变化的节点标脏。
    // 返回 true 表示有节点需要重新布局。
    bool reconcileTree(RenderNode& dst, RenderNode& src);
    void rebuildLayout(PageState& page, float width, float height);
    void updateFocusList(PageState& page);

    void buildChildren(const WxmlNode& node, const Style& parentStyle,
                       std::vector<SelectorNode>& ancestors,
                       const std::vector<LocalValue>& locals,
                       const std::string& path,
                       std::vector<std::unique_ptr<RenderNode>>& out);
    void buildElement(const WxmlNode& node, const Style& parentStyle,
                      std::vector<SelectorNode>& ancestors,
                      const std::vector<LocalValue>& locals,
                      const std::string& path,
                      std::vector<std::unique_ptr<RenderNode>>& out,
                      bool skipFor);
    std::string collectText(const WxmlNode& node,
                            const std::vector<LocalValue>& locals);

    bool evaluateExpression(const std::string& expression,
                            const std::vector<LocalValue>& locals, JsonValue& out,
                            bool* failed = nullptr);
    // 纯属性路径（a、a.b.c）直接在 locals / data 上取值，不进 JS 引擎。
    // 返回 false 表示根标识符不存在，需要交回脚本引擎。
    bool resolvePropertyPath(const std::vector<LocalValue>& locals,
                             const std::vector<std::string>& segments, JsonValue& out) const;
    bool evaluateAttribute(const WxmlNode& node, const std::string& name,
                           const std::vector<LocalValue>& locals, JsonValue& out);
    bool conditionTruthy(const WxmlNode& node, const char* name,
                         const std::vector<LocalValue>& locals, bool& ok);
    std::string interpolate(const std::string& text,
                            const std::vector<LocalValue>& locals);
    std::string valueToText(const JsonValue& value) const;
    std::string attributeText(const WxmlNode& node, const std::string& name,
                              const std::vector<LocalValue>& locals);
    JsonValue dataForPage() const;

    // 绘制
    void drawNode(NVGcontext* vg, RenderNode& node, float parentAlpha,
                  const RenderNode* clipParent);
    void drawBackground(NVGcontext* vg, RenderNode& node, float alpha);
    void drawTextBlock(NVGcontext* vg, const RenderNode& node, float x, float y,
                       float width, float alpha);
    void drawImage(NVGcontext* vg, RenderNode& node, float alpha);
    void drawInput(NVGcontext* vg, RenderNode& node, float alpha);
    void drawProgress(NVGcontext* vg, RenderNode& node, float alpha);
    void drawIcon(NVGcontext* vg, RenderNode& node, float alpha);
    void drawVideo(NVGcontext* vg, RenderNode& node, float alpha);
    void drawVideoSurface();
    // 全屏视频时在 video 直出之后单独开一帧 NanoVG，把 data-overlay 子树
    // 与弹窗/提示画到画面上层。
    void drawVideoOverlay();
    void drawOverlayNodes(NVGcontext* vg, RenderNode& node, float parentAlpha);
    bool hasOverlayNodes(const RenderNode& node) const;
    bool isOverlayNode(const RenderNode* node) const;
    void drawSwitch(NVGcontext* vg, RenderNode& node, float alpha);
    void drawCheckbox(NVGcontext* vg, RenderNode& node, float alpha);
    void drawRadio(NVGcontext* vg, RenderNode& node, float alpha);
    void drawSlider(NVGcontext* vg, RenderNode& node, float alpha);
    void drawNavigator(NVGcontext* vg, RenderNode& node, float alpha);
    void drawSwiper(NVGcontext* vg, RenderNode& node, float alpha,
                    const RenderNode* clipParent);
    void drawFocusRing(NVGcontext* vg, const RenderNode& node);
    void drawOverlays(NVGcontext* vg, float width, float height);
    void drawError(NVGcontext* vg, float x, float y, float width, float height,
                   const std::string& message);
    // 包解析完成前先画加载页，避免启动时黑屏/卡住。
    void drawLoading(NVGcontext* vg, float x, float y, float width, float height);

    // 输入
    // 页面级按键事件 onKeyDown；返回 true 表示脚本消费了该按键，
    // 调用方应跳过默认的焦点移动 / 确认 / 返回逻辑。
    bool dispatchKeyToPage(const std::string& key);
    // 当前帧物理十字键是否按下（用于区分摇杆与十字键）。
    static bool isDpadHeld(brls::ControllerButton pad);
    void handleActivate();
    void handlePointerTap(float x, float y);
    // 触摸/鼠标按下、移动、抬起：派发 bindtouchstart / bindtouchmove / bindtouchend。
    void handlePointerState(float x, float y, const std::string& phase);
    void dispatchTouchEvent(const std::string& type, RenderNode* target, float x, float y);
    // 长按判定（在 tick 中驱动）。
    void pumpLongPress(double now);
    void drawCanvas(NVGcontext* vg, RenderNode& node, float alpha);
    void activateNode(RenderNode* node);
    RenderNode* hitTestNode(RenderNode& node, float x, float y) const;
    RenderNode* findNodeByPath(RenderNode& node, const std::string& path) const;
    RenderNode* findNodeById(RenderNode& node, const std::string& id) const;
    // 返回 true 表示事件被小程序内部消费；false 表示已到边界，交给 borealis 焦点导航（可逃逸到外层 UI）
    bool handleDirection(brls::FocusDirection direction);
    bool moveFocus(RenderNode* from, brls::FocusDirection direction);
    RenderNode* findFocusByPath(const std::string& path) const;
    RenderNode* firstFocusable() const;
    void ensureVisible(RenderNode* node);
    void triggerTap(RenderNode* node);
    void triggerChange(RenderNode* node, const JsonValue& detail, const std::string& value);
    void activateInput(RenderNode* node);
    float valueOf(RenderNode* node) const;
    void setLocalValue(RenderNode* node, const std::string& value);

    void tick(double now);
    // ---- 线程协作 ----
    bool isUiThread() const { return std::this_thread::get_id() == uiThreadId; }
    // 回 UI 线程执行；本来就在 UI 线程时直接执行。
    void postUi(std::function<void()> task);
    // 投递到逻辑线程；逻辑线程不可用时同步执行。
    void postLogic(std::function<void()> task);
    // UI 线程每帧刷新一份宿主状态快照，供逻辑线程同步读取。
    void refreshHostStateCache();
    JsonValue collectSystemInfo();
    JsonValue collectGamepadState();
    JsonValue collectAudioState(const std::string& id);
    // 会销毁页面（进而销毁该页 JsRuntime）的导航不能同步执行：wx.navigateBack 等
    // 通常由定时器或点击回调触发，此时 QuickJS 还在这套 context 上执行，直接释放
    // 会让引擎继续读写已释放的内存，真机表现为点保存后崩溃。统一延迟到下一帧。
    void flushPendingNavigation();
    bool performNavigateBack(int delta);
    bool performRedirectTo(const std::string& url);
    bool performReLaunch(const std::string& url);
    void pumpSwiper(PageState& page, RenderNode& node, double now);
    void pumpVideo(PageState& page, RenderNode& node, double now);
    void toggleVideo(RenderNode* node);
    void startVideo(RenderNode* node, bool forceLoad);
    void stopVideo();
    // 全屏播放：由 createVideoContext.requestFullScreen/exitFullScreen 驱动，
    // 全屏时按内容区全矩形直出视频，B/返回键退出。
    void enterVideoFullscreen(const std::string& path);
    void exitVideoFullscreen();
    bool isVideoFullscreen() const { return !fullscreenVideoPath.empty(); }
    // 当前是否存在“玩家正在看的视频”：全屏、焦点在 video 上，或铺满内容区的视频正在播放。
    // 只有这种情况下才拦截方向键/A/Y，避免抢走列表页与直播页自己的按键。
    bool videoControlsActive() const;
    // 手柄按键 -> 播放器动作；返回 true 表示已消费。
    bool handleVideoKey(const std::string& key);
    bool handleVideoSeek(double delta);
    // 执行一次 seek 并刷新 OSD/控制层。
    void applyVideoSeek(double delta);
    // 底部分钟式控制条：进度、时间、播放状态、缓冲提示与按键说明。
    void drawVideoBar(NVGcontext* vg, const RenderNode& node, float left, float top,
                      float width, float height, float alpha, bool fullscreen);
    void drawVideoControlOverlay(NVGcontext* vg, float width, float height);
    void dispatchVideoEvent(RenderNode* node, const std::string& handler,
                            const std::string& type);
    void loadStorage();
    void saveStorageKey(const std::string& key, const std::string& value);
    int imageTexture(const std::string& src, const std::string& pageDir,
                     const std::string& headers = std::string());
    void requestRemoteImage(const std::string& url, const std::string& headers);
    // 纹理缓存只增不删会把显存吃光：海报墙动辄上百张封面，必须按帧淘汰。
    void trimImageTextures();
    std::string resolveLocalPath(const std::string& src, const std::string& pageDir);

    std::string packagePathOverride;
    std::string packageDir;
    std::string appJsonText;
    std::string appSource;
    std::string appWxss;
    std::string appTitle = "NX MiniApp";
    std::string appBackground = "#141518";
    std::string errorMessage;
    // 加载页：首帧先出画面，包读取/解析推迟到下一帧之后执行。
    bool pendingLoad = false;
    int loadDelayFrames = 1;
    double loadingStart = 0.0;
    std::string loadingTitle;

    std::vector<std::unique_ptr<PageState>> pages;
    PageState* current = nullptr;

    // 逻辑线程：定时器、网络/SMB 回调在那里跑，UI 线程只做布局、绘制和输入。
    std::unique_ptr<LogicThread> logicThread;
    std::thread::id uiThreadId;
    // 每帧最多排一个 pumpTimers，脚本慢时避免任务堆积。
    std::atomic<bool> logicPumpPending{false};
    // 保护 storageValues / clipboardText / 宿主状态缓存。
    mutable std::mutex hostStateMutex;
    JsonValue cachedSystemInfo = JsonValue::object();
    JsonValue cachedGamepadState = JsonValue::object();
    std::map<std::string, JsonValue> cachedAudioState;
    // 构建渲染树期间共用的求值会话：page.data 作为 JS 作用域原型，
    // 每个 {{}} 只绑定自己的 locals，避免逐表达式重建整份 data。
    JsRuntime::EvalSession evalSession;
    bool evalSessionActive = false;
    std::function<void()> pendingNavigation;

    std::vector<RenderNode*> focusNodes;
    std::string focusPath;

    // 触摸状态：按下目标、长按计时、tap 抑制。
    std::string touchTargetPath;
    bool touchActive = false;
    bool suppressTap = false;
    bool longPressFired = false;
    bool longPressCancelled = false;
    double touchStartTime = 0.0;
    float touchStartX = 0.0f;
    float touchStartY = 0.0f;
    float touchLastX = 0.0f;
    float touchLastY = 0.0f;
    // 触摸拖动滚动：记录本次按下的 scroll-view 路径与上一次坐标。
    std::string touchScrollPath;
    float touchScrollLastX = 0.0f;
    float touchScrollLastY = 0.0f;
    // 上一次生效的 autofocus 节点路径，用于弹窗出现时只抢一次焦点。
    std::string lastAutoFocusPath;
    // 弹窗打开前的焦点路径，关闭弹窗后还原。
    std::string restoreFocusPath;

    ModalState modal;
    std::string toastText;
    std::string toastIcon;
    double toastUntil = 0.0;
    bool loadingVisible = false;
    std::string loadingText = "加载中";
    double overlayClock = 0.0;

    std::string navigationTitle;

    std::map<std::string, int> imageTextures;
    // 每张纹理最近一次被绘制的帧号，用于 LRU 淘汰。
    std::map<std::string, uint64_t> imageTextureUse;
    uint64_t imageFrame = 0;
    std::set<std::string> pendingImages;
    std::map<std::string, std::string> storageValues;
    std::string storageDir;
    std::string storageFile;
    std::string clipboardText;

    float rpxScale = 1.0f;
    float layoutWidth = -1.0f;
    float layoutHeight = -1.0f;
    int fontId = -1;
    bool runLoopActive = false;
    brls::VoidEvent::Subscription runLoopToken;
    bool focusInitialized = false;
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);

    // video：全局只允许一个播放器实例。通过 nxkit::VideoView 对接
    // wiliwili 风格的播放器，不再直接调用 mpv。
    std::string activeVideoPath;
    std::string activeVideoSrc;
    // 非空表示当前处于视频全屏，值为对应 video 节点的 path。
    std::string fullscreenVideoPath;
    // 等待起播的 video 节点 path。onReady 里请求全屏会走到起播，而起播要同步
    // 初始化 MPV（几百毫秒到数秒），不能发生在 draw 里，所以推迟到下一帧 tick。
    std::string pendingVideoStartPath;
    // 当前页面是否存在 data-overlay 子树（每帧布局后在 draw() 里更新）。
    bool fullscreenHasOverlay = false;
    brls::VoidEvent::Subscription postDrawToken;

    // ---- 播放器交互状态 ----
    // 控制层保持可见到该时刻（steadyMilliseconds），按键后顺延。
    double videoOverlayUntil = 0.0;
    // 方向键长按连续 seek：方向（-1/+1/0）、下一次触发时刻、已加速档位。
    double videoSeekDir = 0.0;
    double videoSeekNextAt = 0.0;
    int videoSeekStep = 0;
    // seek 时在画面下方提示“快进 10 秒 · 01:23”。
    std::string videoOsdText;
    double videoOsdUntil = 0.0;

    // wx.createInnerAudioContext 的音轨状态。
    // Switch 上 libnx 的 audren 只有一个进程级音频渲染器，多个 mpv 音频实例同时出声
    // 会互相踩踏渲染器状态直接崩溃，所以所有 context 共用下面这一个纯音频 mpv，
    // 同一时刻只播一路：新音轨起播会顶掉旧音轨，一次性音效结束后自动恢复循环音轨。
    struct AudioTrack {
        std::string src;
        bool loop = false;
        int volume = 100;
        double position = 0.0;
    };
    std::map<std::string, AudioTrack> audioTracks;
    std::unique_ptr<nxkit::AudioPlayer> audioPlayer;
    std::string activeAudioId;
    // 被一次性音效打断、结束后需要恢复的循环音轨。
    std::string suspendedAudioId;
    bool audioUnavailable = false;

    nxkit::AudioPlayer* ensureAudioPlayer();
    void switchAudioTrack(const std::string& id, bool resumePosition);
    // 当前激活音轨播完后，接回被它打断的循环音轨。
    void resumeSuspendedAudio();

    // wx.vibrateShort 的停止时刻（steadyMilliseconds），0 表示当前没有震动。
    double vibrationStopAt = 0.0;

    void onVideoState(const nxkit::VideoState& vs);
};

}  // namespace miniapp
