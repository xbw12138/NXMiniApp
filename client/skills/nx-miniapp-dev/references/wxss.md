# WXSS 子集

WXSS 使用项目内置的解析器，只支持下列选择器和声明。**未列出的属性一律被静默忽略**（不会报错，也不会生效）。

## 1. 选择器

| 写法 | 支持 |
| --- | --- |
| `.class` | 是 |
| `#id` | 是 |
| `view` / `text` / `button` / `image` | 是（按标签名） |
| `.a .b` | 是（后代选择器，空格分隔） |
| `.a > .b` | 否 |
| `view.a`、`.a.b`（组合） | 否 |
| `:hover`、`::before` 等伪类/伪元素 | 否 |
| `.a, .b`（逗号分组） | 否 |
| `[attr]`、`*` | 否 |
| `@media`、`@import`、`@keyframes` | 否；`@media` 块会被跳过，其他 at-rule 可能破坏后续解析，禁止使用 |
| CSS 变量 `var(--x)` | 否 |

规则匹配**没有优先级概念**：同一个属性，源码中后出现的规则覆盖先出现的规则；内联 `style` 最后应用。

## 2. 长度单位

| 单位 | 行为 |
| --- | --- |
| `px` | 逻辑像素 |
| `rpx` | 固定换算：`1rpx = 640/750 ≈ 0.853` 逻辑像素 |
| `%` | `width` / `height` / `min-*` / `max-*` / `flex-basis`，相对父节点 |
| 无单位数字 | 仅长度属性可解析（等价 px）；`line-height` 可为倍数 |
| `em` / `rem` / `vw` / `vh` | 不支持 |

注意：逻辑画布是 **1280×720**，`750rpx ≈ 640px`，**不等于整屏宽**。整屏布局请用 `width: 100%` 或 `1280px`。

## 3. 支持的属性

### 盒子与外观

| 属性 | 值 | 说明 |
| --- | --- | --- |
| `background` / `background-color` | 颜色 | 只支持纯色，不支持图片/渐变 |
| `color` | 颜色 | 文本颜色，可继承 |
| `width` / `height` | px / rpx / % | 支持百分比 |
| `padding` / `padding-top` / `padding-right` / `padding-bottom` / `padding-left` | 1–4 个长度 | 与 CSS 简写一致 |
| `margin` / `margin-top` / `margin-right` / `margin-bottom` / `margin-left` | 1–4 个长度 | 与 CSS 简写一致 |
| `border` | 宽度 + 颜色，顺序任意 | 四边统一；样式关键字（如 `solid`）可有可无 |
| `border-width` | 长度 | 四边统一 |
| `border-color` | 颜色 | 四边统一 |
| `border-radius` | 长度 | 统一圆角，自动限制为短边一半 |
| `opacity` | 0–1 | 整体透明度 |
| `box-shadow` | `x y blur color` | 单层阴影 |
| `display` | `flex` / `none` | `none` 不参与布局 |
| `position` | `absolute` | 配合 `top` / `right` / `bottom` / `left` |
| `min-width` / `min-height` / `max-width` / `max-height` | 长度或百分比 | 尺寸约束 |

### Flex 布局

| 属性 | 值 |
| --- | --- |
| `flex-direction` | `row` / `column`（默认 `column`） |
| `justify-content` | `flex-start`（默认）/ `center` / `flex-end` / `space-between` / `space-around` / `space-evenly` |
| `align-items` | `stretch`（默认）/ `flex-start` / `center` / `flex-end` |
| `align-self` | `stretch` / `flex-start` / `center` / `flex-end` |
| `flex` | CSS 语义简写：`flex: 1` → grow 1 / shrink 1 / basis 0 |
| `flex-grow` / `flex-shrink` / `flex-basis` | 数值 / 长度或百分比 |
| `overflow` / `overflow-x` / `overflow-y` | `hidden` |
| `gap` / `column-gap` | 长度；只在主轴方向生效，通过给第 2 个起子节点加外边距实现 |

### 文本

| 属性 | 值 |
| --- | --- |
| `font-size` | 长度，默认 28（逻辑 px） |
| `font-weight` | `bold` / `bolder` / 数字 ≥ 600 |
| `line-height` | 无单位倍数（默认 1.4）或长度 |
| `text-align` | `left`（默认）/ `center` / `right` |

粗体通过重复绘制 0.6px 偏移实现，不是真的粗体字重。

## 4. 颜色

- `#RGB`、`#RGBA`、`#RRGGBB`、`#RRGGBBAA`
- `rgb(r, g, b)`、`rgba(r, g, b, a)`；分量可写数字或百分号
- 命名色：`white`、`black`、`red`、`green`、`blue`、`gray` / `grey`、`lightgray`、`darkgray`、`orange`、`yellow`、`purple`、`pink`、`cyan`、`teal`、`brown`、`transparent`
- 不支持 `hsl()`、CSS 颜色变量与渐变

## 5. 继承

只有这些文本属性会从父节点继承到子节点：

`color`、`font-size`、`font-weight`、`line-height`、`text-align`。

背景、边框、圆角、内外边距、宽高、flex 属性**都不会继承**。`view` 里的裸文本会继承父节点字体样式。

## 6. 明确不支持

- 层级与布局扩展：`z-index`、`flex-wrap`、`align-content`、`aspect-ratio`
- 视觉特效：`transform`、`transition`、`animation`、`filter`
- 文本扩展：`letter-spacing`、`text-decoration`、`white-space`、`text-overflow`
- 背景扩展：`background-image`、渐变、`background-size` / `background-position`
- 选择器扩展：子代 `>`、属性选择器、伪类/伪元素、`@media`、`@import`、`@keyframes`
- `!important` 会被去掉，不参与优先级

## 7. 组件默认样式

| 组件 | 默认 |
| --- | --- |
| `view` | 无背景、无边框、flex-direction: column、align-items: stretch |
| `text` | 继承颜色/字号，默认字号 28，行高 1.4×字号 |
| `button` | 绿色背景 `#0FDC78`、深色文字、圆角 44rpx、默认 320×88rpx（可通过 WXSS 覆盖） |
| `image` | 默认 200×120rpx；加载包内图片，默认 `scaleToFill` |
| `input` / `textarea` | 深色背景、圆角 12rpx、左右内边距 20rpx；默认 100%×80rpx / 160rpx |
| `switch` / `checkbox` / `radio` | 96×56rpx / 48×48rpx / 48×48rpx，原生绘制 |
| `slider` / `progress` | 100%×80rpx / 100%×24rpx |
| `swiper` / `swiper-item` | 100%×300rpx / 100%×100% |
| `navigator` | 100%×80rpx |

`button` 与 `image` 的默认宽高只在没有显式设置 `width` / `height` 时生效。

## 8. 可用样式模板

```css
.page {
  background-color: #141518;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.card {
  width: 660rpx;
  background-color: #1F2125;
  border: 1rpx solid #2A2D33;
  border-radius: 24rpx;
  padding: 20rpx 28rpx;
  gap: 16rpx;
}

.title {
  color: #F5F6F7;
  font-size: 48rpx;
  font-weight: bold;
  line-height: 1.12;
}

.desc {
  color: #8A9099;
  font-size: 22rpx;
}
```
