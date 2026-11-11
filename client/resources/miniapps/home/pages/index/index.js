const HOME_CACHE_KEY = 'home.cache.v1';
const MAX_STORIES = 3;
const DOCK_MAX_RECENT = 9;  // 最近使用最多 9 个，空出最后一格给「添加」
const DOCK_COLS = 5;

Page({
  data: {
    banners: [],
    banner: {},
    bannerIndex: 0,
    stories: [],
    newsCount: 0,
    dockRows: [],
    status: '正在加载…'
  },

  onLoad() {
    this.featured = [];
    // 先用上次的缓存直出，网络请求在后台刷新：打开首页不再等接口往返。
    this.applyCached();
    this.loadHome();
  },

  onUnload() {
    if (this.bannerTimer) clearInterval(this.bannerTimer);
  },

  applyCached() {
    let cached = null;
    try {
      cached = wx.getStorageSync(HOME_CACHE_KEY);
    } catch (err) {
      cached = null;
    }
    if (!cached || !cached.banners) return;
    this.hasCache = true;
    this.applyPayload(cached, false);
  },

  applyPayload(payload, fresh) {
    const banners = payload.banners || [];
    const featured = payload.featured || [];
    const news = payload.news || [];
    this.featured = featured;
    this.setData({
      banners: banners,
      banner: banners.length > 0 ? banners[0] : {},
      bannerIndex: 0,
      stories: this.buildStories(news),
      newsCount: news.length,
      dockRows: this.buildDock(featured),
      // 没有任何内容时才提示加载中，避免缓存直出时闪一行文字。
      status: (banners.length || featured.length || news.length) ? '' : '正在加载…'
    });
    if (fresh) {
      try {
        wx.setStorageSync(HOME_CACHE_KEY, {
          banners: banners,
          news: news,
          featured: featured
        });
      } catch (err) {
        // 缓存失败不影响展示。
      }
    }
    this.startBanner(banners.length);
  },

  // 资讯栏用 01、02 这样的编号，读起来像专栏目录；横向只放得下 3 条。
  buildStories(news) {
    return news.slice(0, MAX_STORIES).map((item, i) => Object.assign({}, item, {
      no: (i < 9 ? '0' : '') + (i + 1)
    }));
  },

  // Dock 只放最近使用过的应用：最近的排第一个，没排满时补一个「添加」入口。
  buildDock(featured) {
    const byId = {};
    featured.forEach((f) => { byId[f.id] = f; });

    let recent = [];
    try {
      recent = wx.miniapp.listRecent() || [];
    } catch (err) {
      recent = [];
    }

    const apps = [];
    const seen = {};
    recent.forEach((r) => {
      if (!r.id || seen[r.id] || apps.length >= DOCK_MAX_RECENT) return;
      seen[r.id] = true;
      const f = byId[r.id] || {};
      const title = (r.title && r.title !== r.id) ? r.title : (f.title || r.id);
      apps.push({
        id: r.id,
        title: title,
        // 容器在启动小程序时已把图标地址存进最近使用记录，这里直接用。
        icon_url: r.icon || f.icon_url || '',
        initial: title.substring(0, 1),
        add: false
      });
    });
    apps.push({ id: '', title: '添加', add: true });

    // 容器不支持 flex-wrap，按 5 列在 JS 里切行；最后一行空缺补不可聚焦的占位块。
    const rows = [];
    for (let i = 0; i < apps.length; i += DOCK_COLS) {
      const items = apps.slice(i, i + DOCK_COLS);
      const blanks = [];
      for (let b = items.length; b < DOCK_COLS; b += 1) blanks.push(b);
      rows.push({ items: items, blanks: blanks });
    }
    return rows;
  },

  loadHome() {
    wx.request({
      url: '/api/v1/miniapp-home',
      success: (res) => {
        const payload = res.data || {};
        this.applyPayload(payload, true);
        if (this.data.status === '正在加载…') this.setData({ status: '暂无内容' });
      },
      fail: () => {
        // 有缓存时保持缓存内容，只在顶栏提示网络状态。
        this.setData({ status: this.hasCache ? '离线 · 显示缓存' : '无法连接服务端' });
      }
    });
  },

  startBanner(count) {
    if (this.bannerTimer) clearInterval(this.bannerTimer);
    if (count <= 1) return;
    let index = 0;
    this.bannerTimer = setInterval(() => {
      index = (index + 1) % count;
      this.setData({ banner: this.data.banners[index], bannerIndex: index });
    }, 4000);
  },

  onDockTap(e) {
    const ds = e.currentTarget.dataset;
    // 「全部」入口没有 id，用它区分，避免 dataset 布尔值被转成字符串。
    if (!ds.id) {
      this.onSearch();
      return;
    }
    wx.miniapp.launch(ds.id, ds.title || '');
    this.setData({ dockRows: this.buildDock(this.featured) });
  },

  onNewsTap(e) {
    const id = e.currentTarget.dataset.target;
    if (!id) return;
    this.reportNewsOpen(id);
    wx.navigateTo({ url: '/pages/news/news?id=' + id });
  },

  // 资讯曝光明细：views(PV) + uv(独立设备)，后台可见。
  reportNewsOpen(id) {
    let device = '';
    try {
      device = (wx.miniapp && wx.miniapp.deviceId) ? wx.miniapp.deviceId() : '';
    } catch (err) {
      device = '';
    }
    wx.request({
      url: '/api/v1/content/' + id + '/open',
      method: 'POST',
      data: { device_id: device },
      fail: () => {}
    });
  },

  onSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  onAll() {
    wx.navigateTo({ url: '/pages/all/all' });
  }
});
