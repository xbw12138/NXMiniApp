Page({
  data: {
    fid: '0',
    name: '',
    items: [],
    page: 1,
    total: 0,
    pages: 1,
    loading: false,
    errMsg: ''
  },

  onLoad(query) {
    var fid = query.fid || '0';
    var name = '我的夸克网盘';
    try {
      name = decodeURIComponent(query.name || '') || name;
    } catch (e) {
      name = query.name || name;
    }
    this.setData({ fid: fid, name: name });
    wx.setNavigationBarTitle({ title: name });
    this.load(1);
  },

  load(page) {
    var self = this;
    self.setData({ loading: true, errMsg: '' });
    wx.showLoading({ title: '加载中…' });
    getApp().listFiles(self.data.fid, page).then(function (body) {
      wx.hideLoading();
      var list = (body.data && body.data.list) || [];
      var total = (body.metadata && body.metadata._total) || (body.data && body.data._total) || list.length;
      var size = getApp().globalData.pageSize;
      var items = [];
      list.forEach(function (f) { items.push(getApp().fileModel(f)); });
      self.setData({
        loading: false,
        page: page,
        total: total,
        pages: Math.max(1, Math.ceil(total / size)),
        items: items
      });
    }).catch(function (err) {
      wx.hideLoading();
      var msg = (err && err.message) || '加载失败';
      self.setData({ loading: false, errMsg: msg });
      if (msg.indexOf('Cookie') >= 0 || msg.indexOf('登录') >= 0) {
        try { wx.removeStorageSync('quark_cookie'); } catch (e) {}
        wx.showToast({ title: msg });
        wx.reLaunch({ url: '/pages/index/index' });
      }
    });
  },

  onRefresh() { this.load(this.data.page); },
  prevPage() { if (this.data.page > 1) this.load(this.data.page - 1); },
  nextPage() { if (this.data.page < this.data.pages) this.load(this.data.page + 1); },
  goSearch() { wx.navigateTo({ url: '/pages/search/search' }); },
  goHome() { wx.reLaunch({ url: '/pages/index/index' }); },

  onItem(e) {
    var d = e.currentTarget.dataset;
    var self = this;
    if (d.kind === 'dir') {
      wx.navigateTo({ url: '/pages/list/list?fid=' + d.fid + '&name=' + encodeURIComponent(d.name) });
      return;
    }
    if (d.kind === 'video') {
      wx.navigateTo({ url: '/pages/play/play?fid=' + d.fid + '&name=' + encodeURIComponent(d.name) });
      return;
    }
    if (d.kind === 'image') {
      wx.navigateTo({ url: '/pages/image/image?fid=' + d.fid + '&name=' + encodeURIComponent(d.name) });
      return;
    }
    wx.showModal({
      title: d.name,
      content: '大小 ' + (d.size || '未知') + '  ·  修改于 ' + (d.time || '未知'),
      confirmText: '复制链接',
      cancelText: '关闭',
      success(res) {
        if (!res.confirm) return;
        wx.showLoading({ title: '获取下载链接…' });
        getApp().downloadUrl(d.fid).then(function (url) {
          wx.hideLoading();
          wx.setClipboardData({
            data: url,
            success() { wx.showToast({ title: '已复制下载链接' }); }
          });
        }).catch(function (err) {
          wx.hideLoading();
          wx.showModal({ title: '获取失败', content: (err && err.message) || '请稍后重试', showCancel: false });
        });
      }
    });
  }
});
