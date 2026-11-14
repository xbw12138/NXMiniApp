const HOME_CACHE_KEY = 'home.cache.v1';
const MAX_STORIES = 3;
const GRID_COLS = 3;
const GRID_ROWS = 2;
const HOT_SIZE = 5;

const WEEKDAYS = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];

function blanks(count) {
  const list = [];
  for (let i = 0; i < count; i += 1) list.push(i);
  return list;
}

function dateText() {
  const now = new Date();
  return (now.getMonth() + 1) + '月' + now.getDate() + '日 ' + WEEKDAYS[now.getDay()];
}

Page({
  data: {
    banners: [],
    bannerIndex: 0,
    hero: {},
    stories: [],
    storyBlanks: [],
    newsCount: 0,
    recentRows: [],
    hot: [],
    dateText: dateText(),
    status: '正在加载…'
  },

  onLoad() {
    this.featured = [];
    this.apps = [];
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
    const news = payload.news || [];
    const featured = payload.featured || [];
    const apps = payload.apps || [];
    this.featured = featured;
    this.apps = apps;

    const stories = this.buildStories(news);
    const hero = banners.length > 0 ? banners[0] : {
      id: 0,
      title: '欢迎来到 NX 商店',
      summary: '用方向键浏览精选小程序，按 A 直接打开'
    };

    this.setData({
      banners: banners,
      bannerIndex: 0,
      hero: hero,
      stories: stories,
      storyBlanks: blanks(Math.max(0, MAX_STORIES - stories.length)),
      newsCount: news.length,
      recentRows: this.buildRecent(featured),
      hot: this.buildHot(featured, apps),
      // 没有任何内容时才提示加载中，避免缓存直出时闪一行文字。
      status: (banners.length || featured.length || news.length) ? '' : '正在加载…'
    });

    if (fresh) {
      try {
        wx.setStorageSync(HOME_CACHE_KEY, {
          banners: banners,
          news: news,
          featured: featured,
          apps: apps
        });
      } catch (err) {
        // 缓存失败不影响展示。
      }
    }
    this.startBanner(banners.length);
  },

  buildStories(news) {
    return news.slice(0, MAX_STORIES);
  },

  // 快捷启动：最近用过的排前面，不够一屏时用精选补位。
  buildRecent(featured) {
    const total = GRID_COLS * GRID_ROWS;
    const apps = [];
    const seen = {};

    let recent = [];
    try {
      recent = wx.miniapp.listRecent() || [];
    } catch (err) {
      recent = [];
    }

    const push = (id, title, icon) => {
      if (!id || seen[id] || apps.length >= total) return;
      seen[id] = true;
      apps.push({
        id: id,
        title: title || id,
        icon_url: icon || '',
        initial: (title || id).substring(0, 1)
      });
    };

    recent.forEach((r) => {
      const meta = featured.filter((f) => f.id === r.id)[0] || {};
      const title = (r.title && r.title !== r.id) ? r.title : (meta.title || r.id);
      push(r.id, title, r.icon || meta.icon_url);
    });
    featured.forEach((f) => push(f.id, f.title, f.icon_url));

    const rows = [];
    for (let i = 0; i < apps.length; i += GRID_COLS) {
      const items = apps.slice(i, i + GRID_COLS);
      const rowBlanks = [];
      for (let b = items.length; b < GRID_COLS; b += 1) rowBlanks.push(b);
      rows.push({ items: items, blanks: rowBlanks });
    }
    return rows;
  },

  // 热门排行：用商店全量列表按使用次数排序，没有列表时退回精选。
  buildHot(featured, apps) {
    const pool = (apps && apps.length > 0 ? apps : featured).filter((item) => item.id !== 'home');
    const sorted = pool.slice().sort((a, b) => (Number(b.opens) || 0) - (Number(a.opens) || 0));
    return sorted.slice(0, HOT_SIZE).map((item, i) => ({
      id: item.id,
      title: item.title,
      icon_url: item.icon_url || '',
      no: (i < 9 ? '0' : '') + (i + 1),
      hot: i < 3,
      meta: (item.category || '小程序') + ' · ' + (Number(item.opens) || 0) + ' 次使用'
    }));
  },

  loadHome() {
    let home = null;
    let apps = null;
    let remaining = 2;

    const finish = () => {
      remaining -= 1;
      if (remaining > 0) return;
      if (home) {
        this.applyPayload({
          banners: home.banners || [],
          news: home.news || [],
          featured: home.featured || [],
          apps: apps || []
        }, true);
        if (this.data.status === '正在加载…') this.setData({ status: '暂无内容' });
        return;
      }
      this.setData({ status: this.hasCache ? '离线 · 显示缓存' : '无法连接服务端' });
    };

    wx.request({
      url: '/api/v1/miniapp-home',
      success: (res) => {
        home = res.data || {};
        finish();
      },
      fail: () => {
        finish();
      }
    });

    wx.request({
      url: '/api/v1/miniapps',
      data: { page: 1, page_size: 50 },
      success: (res) => {
        apps = (res.data && res.data.items) || [];
        finish();
      },
      fail: () => {
        finish();
      }
    });
  },

  startBanner(count) {
    if (this.bannerTimer) clearInterval(this.bannerTimer);
    if (count <= 1) return;
    let index = 0;
    this.bannerTimer = setInterval(() => {
      index = (index + 1) % count;
      this.setData({ hero: this.data.banners[index], bannerIndex: index });
    }, 5000);
  },

  onHeroTap() {
    const hero = this.data.hero;
    if (!hero || !hero.id) return;
    this.reportNewsOpen(hero.id);
    wx.navigateTo({ url: '/pages/news/news?id=' + hero.id });
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

  onLaunch(e) {
    const ds = e.currentTarget.dataset;
    if (ds.id) wx.miniapp.launch(ds.id, ds.title || '');
  },

  onSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  // 「今日」就是当前页，点击相当于刷新一次首页数据。
  onToday() {
    this.loadHome();
  },

  onAll() {
    wx.navigateTo({ url: '/pages/all/all' });
  }
});
