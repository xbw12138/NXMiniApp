// Yoga 1.19.0 的 event 组件在裁剪依赖时未随 25 个核心文件一起提供。
// 这里补一个最小实现：保留布局统计所需的数据结构，事件发布为空操作。
#pragma once

#include <array>
#include <cstdint>

#include "YGEnums.h"
#include "Yoga.h"

namespace facebook::yoga {

enum class LayoutType : uint8_t {
  kLayout = 0,
  kMeasure = 1,
  kCachedLayout = 2,
  kCachedMeasure = 3,
};

enum class LayoutPassReason : int {
  kInitial = 0,
  kAbsLayout = 1,
  kStretch = 2,
  kMultilineStretch = 3,
  kFlexLayout = 4,
  kMeasureChild = 5,
  kAbsMeasureChild = 6,
  kFlexMeasure = 7,
  COUNT = 8,
};

inline const char* LayoutPassReasonToString(LayoutPassReason reason) {
  switch (reason) {
    case LayoutPassReason::kInitial:
      return "initial";
    case LayoutPassReason::kAbsLayout:
      return "abs_layout";
    case LayoutPassReason::kStretch:
      return "stretch";
    case LayoutPassReason::kMultilineStretch:
      return "multiline_stretch";
    case LayoutPassReason::kFlexLayout:
      return "flex_layout";
    case LayoutPassReason::kMeasureChild:
      return "measure_child";
    case LayoutPassReason::kAbsMeasureChild:
      return "abs_measure_child";
    case LayoutPassReason::kFlexMeasure:
      return "flex_measure";
    case LayoutPassReason::COUNT:
      break;
  }
  return "unknown";
}

struct LayoutData {
  int32_t layouts = 0;
  int32_t measures = 0;
  int32_t maxMeasureCache = 0;
  int32_t cachedLayouts = 0;
  int32_t cachedMeasures = 0;
  int32_t measureCallbacks = 0;
  std::array<int32_t, static_cast<size_t>(LayoutPassReason::COUNT)>
      measureCallbackReasonsCount{};
};

struct Event {
  struct NodeAllocation {
    YGConfigRef config;
  };
  struct NodeDeallocation {
    YGConfigRef config;
  };
  struct NodeBaselineStart {};
  struct NodeBaselineEnd {};
  struct MeasureCallbackStart {};
  struct MeasureCallbackEnd {
    void* layoutContext;
    float width;
    YGMeasureMode widthMeasureMode;
    float height;
    YGMeasureMode heightMeasureMode;
    float measuredWidth;
    float measuredHeight;
    LayoutPassReason reason;
  };
  struct NodeLayout {
    LayoutType layoutType;
    void* layoutContext;
  };
  struct LayoutPassStart {
    void* layoutContext;
  };
  struct LayoutPassEnd {
    void* layoutContext;
    LayoutData* layoutData;
  };

  template <typename E>
  static void publish(YGNodeRef) {}

  template <typename E>
  static void publish(YGNodeRef, E) {}
};

}  // namespace facebook::yoga
