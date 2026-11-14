// 文档站侧边导航：各页面只需引入本脚本，导航由这里统一生成。
(function () {
  const script = document.currentScript;
  const root = new URL("../", script.src);

  const NAV = [
    {
      group: "指南",
      items: [
        { path: "index.html", title: "概览" },
        { path: "guide/structure.html", title: "目录结构" },
        { path: "guide/template.html", title: "WXML 与数据绑定" },
        { path: "guide/js.html", title: "脚本运行时" },
      ],
    },
    {
      group: "组件",
      items: [
        { path: "components/index.html", title: "组件总览" },
        { path: "components/index.html#view", title: "view / scroll-view" },
        { path: "components/index.html#text", title: "text / rich-text" },
        { path: "components/index.html#image", title: "image / icon" },
        { path: "components/index.html#button", title: "button / navigator" },
        { path: "components/index.html#form", title: "input / textarea" },
        { path: "components/index.html#choice", title: "switch / checkbox / radio" },
        { path: "components/index.html#slider", title: "slider / progress" },
        { path: "components/index.html#swiper", title: "swiper" },
        { path: "components/index.html#video", title: "video" },
        { path: "components/index.html#canvas", title: "canvas" },
        { path: "components/index.html#touch", title: "触摸与长按事件" },
        { path: "components/index.html#form-container", title: "form / label / block" },
      ],
    },
    {
      group: "样式",
      items: [{ path: "style/index.html", title: "WXSS 子集" }],
    },
    {
      group: "API",
      items: [
        { path: "api/index.html", title: "wx.* 支持状态" },
        { path: "api/index.html#network", title: "网络 request" },
        { path: "api/index.html#ui", title: "界面交互" },
        { path: "api/index.html#game", title: "游戏能力" },
        { path: "api/index.html#canvas", title: "画布" },
        { path: "api/index.html#media", title: "媒体" },
        { path: "api/index.html#miniapp", title: "小程序互跳" },
        { path: "api/index.html#storage", title: "数据缓存" },
        { path: "api/index.html#route", title: "路由" },
        { path: "api/index.html#system", title: "系统信息" },
        { path: "api/index.html#compat", title: "兼容占位接口" },
      ],
    },
    {
      group: "参考",
      items: [
        { path: "reference/compatibility.html", title: "与微信的差异" },
        { path: "reference/errors.html", title: "错误与排查" },
        { path: "reference/store.html", title: "商店与上架" },
      ],
    },
  ];

  const here = location.href.split("#")[0].split("?")[0];
  const hash = location.hash || "";
  const sidebar = document.getElementById("sidebar");
  if (!sidebar) return;

  const parts = [];
  for (const section of NAV) {
    parts.push("<h3>" + section.group + "</h3>");
    for (const item of section.items) {
      const url = new URL(item.path, root).href;
      const itemHash = item.path.includes("#") ? "#" + item.path.split("#")[1] : "";
      const active = here === url.split("#")[0] && hash === itemHash ? " active" : "";
      const tag = item.tag ? '<span class="st">' + item.tag + "</span>" : "";
      parts.push(
        '<a class="' + active.trim() + '" href="' + url + '">' + item.title + tag + "</a>"
      );
    }
  }
  sidebar.innerHTML = parts.join("");
})();
