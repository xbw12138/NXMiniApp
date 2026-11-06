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
    this.setData({ recent: wx.miniapp.listRecent() || [] });
  },

  onLaunch(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.miniapp.launch(id);
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
