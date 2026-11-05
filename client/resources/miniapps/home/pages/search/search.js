Page({
  data: {
    keyword: '',
    items: [],
    status: '正在加载小程序…'
  },

  onLoad() {
    this.search('');
  },

  onInput(e) {
    this.setData({ keyword: e.detail.value });
  },

  onConfirm(e) {
    this.search(e.detail.value);
  },

  onSubmit() {
    this.search(this.data.keyword);
  },

  search(keyword) {
    const kw = (keyword || '').trim();
    this.setData({ status: '正在搜索…' });
    wx.request({
      url: '/api/v1/miniapps',
      data: kw ? { q: kw } : {},
      success: (res) => {
        const payload = res.data || {};
        const items = payload.items || [];
        this.setData({
          items: items,
          status: items.length === 0
            ? (kw ? '没有找到「' + kw + '」' : '商店里还没有小程序')
            : '共 ' + items.length + ' 个小程序'
        });
      },
      fail: () => {
        this.setData({ items: [], status: '无法连接服务端，请检查网络' });
      }
    });
  },

  onLaunch(e) {
    const id = e.currentTarget.dataset.id;
    if (id) wx.miniapp.launch(id);
  },

  onBack() {
    wx.navigateBack();
  }
});
