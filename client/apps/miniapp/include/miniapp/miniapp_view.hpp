//
// miniapp: 承载 WXML 渲染树的 borealis 视图。
//

#pragma once

#include <borealis.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nxkit/media/video_view.hpp>

#include "miniapp/js_runtime.hpp"
#include "miniapp/json.hpp"
#include "miniapp/render_node.hpp"
#include "miniapp/wxml_parser.hpp"
#include "miniapp/wxss.hpp"

namespace miniapp {

class MiniappView : public brls::View, public JsRuntimeHost {
public:
    explicit MiniappView(std::string packagePath = "");
    ~MiniappView() override;

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
                     const std::string& body, HttpDone done) override;
    bool navigateTo(const std::string& url) override;
    bool navigateBack(int delta) override;
    bool redirectTo(const std::string& url) override;
    bool reLaunch(const std::string& url) override;
    bool launchMiniApp(const std::string& id, const std::string& title = "") override;
    JsonValue recentMiniApps() override;
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
                      double value) override;

private:
    struct LocalValue {
        std::string name;
        JsonValue value;
    };

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
        std::unique_ptr<JsRuntime> runtime;
        std::unique_ptr<RenderNode> root;

        bool dirty = true;
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
    bool evaluateAttribute(const WxmlNode& node, const std::string& name,
                           const std::vector<LocalValue>& locals, JsonValue& out);
    bool conditionTruthy(const WxmlNode& node, const char* name,
                         const std::vector<LocalValue>& locals, bool& ok);
    std::string interpolate(const std::string& text,
                            const std::vector<LocalValue>& locals);
    std::string valueToText(const JsonValue& value) const;
    JsonValue buildScope(const std::vector<LocalValue>& locals) const;
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

    // 输入
    void handleActivate();
    void handlePointerTap(float x, float y);
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
    void pumpSwiper(PageState& page, RenderNode& node, double now);
    void pumpVideo(PageState& page, RenderNode& node, double now);
    void toggleVideo(RenderNode* node);
    void startVideo(RenderNode* node, bool forceLoad);
    void stopVideo();
    void dispatchVideoEvent(RenderNode* node, const std::string& handler,
                            const std::string& type);
    void saveStorage();
    void loadStorage();
    int imageTexture(const std::string& src, const std::string& pageDir,
                     const std::string& headers = std::string());
    void requestRemoteImage(const std::string& url, const std::string& headers);
    std::string resolveLocalPath(const std::string& src, const std::string& pageDir);

    std::string packagePathOverride;
    std::string packageDir;
    std::string appJsonText;
    std::string appSource;
    std::string appWxss;
    std::string appTitle = "NX MiniApp";
    std::string appBackground = "#141518";
    std::string errorMessage;

    std::vector<std::unique_ptr<PageState>> pages;
    PageState* current = nullptr;

    std::vector<RenderNode*> focusNodes;
    std::string focusPath;

    ModalState modal;
    std::string toastText;
    std::string toastIcon;
    double toastUntil = 0.0;
    bool loadingVisible = false;
    std::string loadingText = "加载中";
    double overlayClock = 0.0;

    std::string navigationTitle;

    std::map<std::string, int> imageTextures;
    std::set<std::string> pendingImages;
    std::map<std::string, std::string> storageValues;
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
    brls::VoidEvent::Subscription postDrawToken;

    void onVideoState(const nxkit::VideoState& vs);
};

}  // namespace miniapp
