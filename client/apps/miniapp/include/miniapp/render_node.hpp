//
// miniapp: WXML 数据绑定后的渲染节点。
//

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "miniapp/style.hpp"

struct YGNode;
using YGNodeRef = YGNode*;

namespace miniapp {

struct RenderNode {
    std::string tag;
    std::string text;        // text / button 的展示文字
    std::vector<std::string> classes;

    std::string id;
    std::string path;  // 稳定路径，用于焦点/滚动/开关状态
    std::string bindTap;
    std::string bindChange;
    std::string bindInput;
    std::string bindConfirm;
    std::map<std::string, std::string> dataset;

    // 组件状态
    std::string src;       // image
    std::string imageMode; // image
    // 远程图需要的附加请求头，如 "Cookie: a=b"（鉴权直链预览用）。
    std::string imageHeaders;
    // lazy-load：只有滚动到可见区域后才发起下载。
    bool imageLazyLoad = false;
    bool imageLoadDispatched = false;
    bool imageErrorDispatched = false;
    std::string bindImageLoad;
    std::string bindImageError;
    std::string value;     // input / slider
    std::string placeholder;
    std::string navUrl;    // navigator
    std::string openType;  // navigator
    std::string radioName;
    bool checked = false;
    bool disabled = false;
    float sliderMin = 0.0f;
    float sliderMax = 100.0f;
    float sliderStep = 1.0f;

    // progress / icon / rich-text
    float progressPercent = 0.0f;
    Rgba progressActive{0.06f, 0.86f, 0.47f, 1.0f};
    Rgba progressTrack{0.24f, 0.25f, 0.28f, 1.0f};
    std::string iconType = "info";
    float iconSize = 48.0f;

    // video
    std::string videoSrc;
    std::string videoPoster;
    // 形如 "Cookie: a=b; c=d" 的附加请求头，用于需要鉴权的直链（交给 mpv）。
    std::string videoHeaders;
    std::string videoObjectFit = "contain";
    bool videoAutoplay = false;
    bool videoLoop = false;
    bool videoMuted = false;
    bool videoControls = true;
    bool videoPlaying = false;
    bool videoError = false;
    std::string videoErrorText;
    double videoTime = 0.0;
    double videoDuration = 0.0;
    // initial-time：起播位置（秒）；playback-rate：倍速；volume：0..100。
    double videoInitialTime = 0.0;
    double videoPlaybackRate = 0.0;  // 0 = 使用 mpv 默认（1.0）
    int videoVolume = -1;            // -1 = 不覆盖
    std::string videoSubtitle;       // 外挂字幕地址（可为 SMB 路径）
    // 事件去重游标：避免每帧重复派发 loadedmetadata / seekcomplete。
    bool videoMetadataDispatched = false;
    bool videoWaiting = false;
    unsigned videoSeekSerial = 0;
    std::string bindPlay;
    std::string bindPause;
    std::string bindEnded;
    std::string bindTimeUpdate;
    std::string bindError;
    std::string bindLoadedMetadata;
    std::string bindWaiting;
    std::string bindProgress;
    std::string bindSeekComplete;
    std::string bindFullscreenChange;
    bool catchTap = false;

    // data-overlay：全屏视频时该子树绘制在视频上层（普通模式下无特殊行为）。
    bool overlay = false;

    // autofocus：节点出现时把摇杆焦点交给它（弹窗里的默认按钮）。
    bool autoFocus = false;

    // scroll-view / swiper
    float scrollOffset = 0.0f;
    float contentHeight = 0.0f;
    bool horizontal = false;
    int swiperIndex = 0;
    int swiperCount = 0;
    bool autoplay = false;
    float swiperInterval = 3000.0f;

    Style style;

    std::vector<std::unique_ptr<RenderNode>> children;
    RenderNode* parent = nullptr;
    YGNodeRef yoga = nullptr;

    float fontSize = 28.0f;
    float lineHeight = 39.2f;

    // 布局后计算出的绝对矩形（含滚动偏移）。
    float absLeft = 0.0f;
    float absTop = 0.0f;
    float absWidth = 0.0f;
    float absHeight = 0.0f;

    bool isText() const { return tag == "#text" || tag == "text"; }
    bool isButton() const { return tag == "button"; }
    bool isImage() const { return tag == "image"; }
    bool isScrollView() const { return tag == "scroll-view"; }
    bool isInteractive() const {
        return !bindTap.empty() || tag == "button" || tag == "switch" ||
               tag == "checkbox" || tag == "radio" || tag == "slider" ||
               tag == "input" || tag == "textarea" || tag == "navigator" ||
               tag == "video";
    }
    bool isContainer() const {
        return !isText() && !isImage() && tag != "input" && tag != "textarea" &&
               tag != "slider" && tag != "switch" && tag != "checkbox" &&
               tag != "radio" && tag != "progress" && tag != "rich-text" &&
               tag != "icon" && tag != "video";
    }
};

}  // namespace miniapp
