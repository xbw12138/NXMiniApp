Page({
  data: {
    banners: [],
    banner: {},
    news: [],
    featured: [],
    recent: [],
    status: '正在加载…'
  },

  onLoad() {
    this.loadHome();
    this.loadRecent();
  },

  onUnload() {
    if (this.bannerTimer) clearInterval(this.bannerTimer);
  },

  loadHome() {
    wx.request({
      url: '/api/v1/miniapp-home',
      success: (res) => {
        const payload = res.data || {};
        const banners = payload.banners || [];
        this.setData({
          banners: banners,
          banner: banners.length > 0 ? banners[0] : {},
          news: payload.news || [],
          featured: payload.featured || [],
          status: ''
        });
        this.loadRecent();
        this.startBanner(banners.length);
      },
      fail: () => {
        this.setData({ status: '无法连接服务端，请检查网络' });
      }
    });
  },

  startBanner(count) {
    if (this.bannerTimer) clearInterval(this.bannerTimer);
    if (count <= 1) return;
    let index = 0;
    this.bannerTimer = setInterval(() => {
      index = (index + 1) % count;
      this.setData({ banner: this.data.banners[index] });
    }, 4000);
  },

  loadRecent() {
    const list = wx.miniapp.listRecent() || [];
    // 老记录里可能只存了 id，用推荐列表里的应用名补齐。
    const names = {};
    (this.data.featured || []).forEach((f) => { names[f.id] = f.title; });
    this.setData({
      recent: list.map((r) => Object.assign({}, r, {
        title: (r.title && r.title !== r.id) ? r.title : (names[r.id] || r.id)
      }))
    });
  },

  onLaunch(e) {
    const ds = e.currentTarget.dataset;
    if (!ds.id) return;
    wx.miniapp.launch(ds.id, ds.title || '');
    this.loadRecent();
  },

  onNewsTap(e) {
    const id = e.currentTarget.dataset.target;
    if (!id) return;
    wx.navigateTo({ url: '/pages/news/news?id=' + id });
  },

  onSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  }
});
