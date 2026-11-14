Page({
  data: {
    q: '',
    items: [],
    searched: false,
    loading: false,
    errMsg: '',
    total: 0,
    subtitle: '输入关键词开始搜索'
  },

  onQuery(e) {
    this.setData({ q: e.detail.value });
  },

  onBack() { wx.navigateBack({ delta: 1 }); },
  goHome() { wx.reLaunch({ url: '/pages/index/index' }); },

  onSearch() {
    var q = (this.data.q || '').replace(/^\s+|\s+$/g, '');
    if (!q) {
      wx.showToast({ title: '请输入关键词' });
      return;
    }
    var self = this;
    self.setData({ loading: true, errMsg: '', subtitle: '正在搜索…' });
    wx.showLoading({ title: '搜索中…' });
    getApp().searchFiles(q, 1).then(function (body) {
      wx.hideLoading();
      var list = (body.data && body.data.list) || [];
      var total = (body.metadata && body.metadata._total) || list.length;
      var items = [];
      list.forEach(function (f) { items.push(getApp().fileModel(f)); });
      self.setData({
        loading: false,
        searched: true,
        items: items,
        total: total,
        subtitle: '找到 ' + (total || items.length) + ' 个结果'
      });
    }).catch(function (err) {
      wx.hideLoading();
      var msg = (err && err.message) || '搜索失败';
      self.setData({ loading: false, searched: true, errMsg: msg, items: [], subtitle: '搜索失败' });
      if (msg.indexOf('Cookie') >= 0 || msg.indexOf('登录') >= 0) {
        try { wx.removeStorageSync('quark_cookie'); } catch (e) {}
        wx.reLaunch({ url: '/pages/index/index' });
      }
    });
  },

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
