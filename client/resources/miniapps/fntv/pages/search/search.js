var COLS = 5;

function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) rows.push(list.slice(i, i + cols));
  return rows;
}

Page({
  data: {
    keyword: '',
    results: [],
    rows: [],
    loading: false,
    searched: false,
    errMsg: ''
  },

  onInput(e) {
    this.setData({ keyword: e.detail.value });
  },

  onSearch() {
    var self = this;
    var keyword = String(self.data.keyword || '').trim();
    if (!keyword) {
      wx.showToast({ title: '请输入片名' });
      return;
    }
    if (self.data.loading) return;

    self.setData({ loading: true, errMsg: '', searched: true });
    getApp().search(keyword).then(function (list) {
      self.setData({
        loading: false,
        results: list,
        rows: chunkRows(list, COLS)
      });
    }).catch(function (err) {
      self.setData({
        loading: false,
        results: [],
        rows: [],
        errMsg: (err && err.message) || '搜索失败'
      });
    });
  },

  onItemTap(e) {
    var id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.navigateTo({ url: '/pages/detail/detail?id=' + encodeURIComponent(id) });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
