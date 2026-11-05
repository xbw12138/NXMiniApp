Page({
  data: {
    title: '资讯',
    summary: '',
    body: '',
    status: '正在加载…'
  },

  onLoad(query) {
    const id = query && query.id ? query.id : '';
    if (!id) {
      this.setData({ status: '缺少内容 ID' });
      return;
    }
    wx.request({
      url: '/api/v1/content/' + id,
      success: (res) => {
        const item = res.data || {};
        this.setData({
          title: item.title || '资讯',
          summary: item.summary || '',
          body: item.body || item.summary || '',
          status: ''
        });
      },
      fail: () => {
        this.setData({ status: '无法加载内容' });
      }
    });
  },

  onBack() {
    wx.navigateBack();
  }
});
