# WXML 子集

WXML 由项目内置的极简解析器处理，不是浏览器 DOM，也不是微信 WXML 的完整实现。

## 1. 标签

| 标签 | 行为 |
| --- | --- |
| `view` | 普通盒子，支持背景/边框/圆角/flex/定位；`bindtap` |
| `text` | 文本块，内容会被收集并绘制 |
| `image` | 加载包内本地图片，支持 `mode` |
| `button` | 圆角绿色按钮外观，文案居中，`bindtap` 可用 |
| `scroll-view` | 裁剪与焦点自动滚动 |
| `swiper` / `swiper-item` | 自动轮播与指示点 |
| `input` / `textarea` | 系统输入法、`bindinput` / `bindconfirm` |
| `switch` / `checkbox` / `radio` / `slider` | 原生绘制与 `bindchange` |
| `navigator` | 页面跳转 |
| `progress` / `rich-text` / `icon` | 进度条 / 纯文本降级 / 原生图标 |
| `video` | poster / 播放暂停 / 控制条；`NXKIT_WITH_MPV` 构建可真实播放 |
| `form` / `label` / `checkbox-group` / `radio-group` | 容器语义 |
| `block` | 不生成节点，只展开子节点 |
| 其他任意标签 | 一律按 `view` 处理（属性保留但不生效） |

由此产生的常见问题：

- `picker`、`picker-view`、`canvas`、`map`、`web-view` 仍只是普通 `view`。
- `bindtap` / `bind:tap` / `catchtap` / `catch:tap` 均可使用；`bindtap` 会沿祖先冒泡。
- `block` 包裹 `wx:for` 是合法的：它不会生成额外盒子。

## 2. 根节点

- WXML 只有一个顶层元素时，该元素就是页面根，**强制占满 1280×720**，它自己的 `width` / `height` 会被忽略。
- 有多个顶层元素时，会额外包一层虚拟 `view`，类名为 `page`；这时 `.page` 的 WXSS 规则会作用在这层容器上。
- 根节点没有背景色时，使用 `app.json` 的 `window.backgroundColor`。

## 3. 文本

- 放在 `view` 里的裸文本会生成独立文本节点，按父节点的 flex 方向排列，并继承父节点字体属性。
- `text` 内部所有后代文本会被拼成一整段，嵌套标签的 class/style 不生效。
- 段首尾空白会被去掉；只有空白的文本节点会被丢弃。
- `text` 内部嵌套子元素的 `wx:if` 不会被求值，文本仍会参与拼接；不要在 `text` 里放条件结构。
- 单段文本最多绘制 64 行，超出部分被丢弃。

推荐写法：一行一个 `text`，需要换行时用多个 `text` 或明确的 `line-height`。

## 4. 数据绑定

支持任意 JS 表达式，在页面数据作用域内求值：

```xml
<text>{{user.name}}</text>
<text>{{count + 1}}</text>
<text>{{score >= 60 ? '及格' : '不及格'}}</text>
<view style="width: {{width}}rpx;"></view>
```

- 支持点路径、数组下标、`.length`、四则/比较/逻辑/三元运算。
- 可以调用全局函数（如 `JSON.stringify`），不要依赖页面方法。
- 值是字符串/数字/布尔时正常输出；`null`、数组、对象输出空字符串。
- 布尔输出 `true` / `false`。
- 同一个属性里可以拼接静态文本与多个 `{{}}`，例如 `class="row {{cls}}"`。
- 支持插值的属性：`class`、`id`、`style`、`src` 以及组件属性。

## 5. 指令

### wx:for

```xml
<view wx:for="{{items}}" wx:for-item="item" wx:for-index="i">
  <text>{{i}} - {{item.name}}</text>
</view>
```

- 只有数组会展开；非数组或路径不存在时整个节点不渲染。
- 默认变量名 `item`、`index`，可用 `wx:for-item` / `wx:for-index` 改名。
- 嵌套循环时内层变量遮蔽外层同名变量。
- `wx:key` 可以写，但当前没有 diff/复用；每次 `setData` 全量重建。

### wx:if / wx:elif / wx:else

```xml
<view wx:if="{{score >= 90}}">优秀</view>
<view wx:elif="{{score >= 70}}">良好</view>
<view wx:else>继续加油</view>
```

- 条件按同级顺序组成链；为假时不创建节点。
- 真值规则：非空字符串、非 0 数字、`true`、非空数组/对象为真。
- 没有 `hidden` 属性。

## 6. 解析细节

- 标签名区分大小写，建议全小写。
- 属性值建议用双引号，单引号也支持；不带引号时遇到空白/`>`/`/` 结束。
- 支持自闭合：`<view />`、`<image src="..." />`。
- 支持 `<!-- 注释 -->`。
- 实体：`&amp;` `&lt;` `&gt;` `&quot;` `&apos;` `&#39;` `&nbsp;`。
- 结束标签名不做匹配校验；漏写结束标签会改变树结构，仍应逐层闭合。
- 不支持命名空间、CDATA、`<template>`、`<import>`、`<include>`。

## 7. 推荐的页面骨架

```xml
<view class="page">
  <view class="header">
    <text class="title">{{title}}</text>
    <text class="subtitle">{{subtitle}}</text>
  </view>

  <view class="card">
    <view class="row" wx:for="{{items}}" wx:for-item="item">
      <view class="dot" style="background-color: {{item.color}};"></view>
      <view class="row-main">
        <text class="row-name">{{item.name}}</text>
        <text class="row-desc">{{item.desc}}</text>
      </view>
    </view>
  </view>

  <button class="start" bindtap="onStart">开始</button>
</view>
```
