Page({
  data: {
    banners: [],
    news: [],
    featured: [],
    recent: [],
    status: '正在加载…'
  },

  onLoad() {
    this.loadHome();
    this.loadRecent();
  },

  loadHome() {
    wx.request({
      url: '/api/v1/miniapp-home',
      success: (res) => {
        const payload = res.data || {};
        this.setData({
          banners: payload.banners || [],
          news: payload.news || [],
          featured: payload.featured || [],
          status: ''
        });
      },
      fail: () => {
        this.setData({ status: '无法连接服务端，请检查网络' });
      }
    });
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
