Page({
  data: {
    sources: [],
    testing: {}
  },

  onShow() {
    this.refresh();
  },

  refresh() {
    var app = getApp();
    var list = app.getSources().map(function (s) {
      var cfg = s.config || {};
      var configured = cfg.url || cfg.server || cfg.refreshToken || cfg.accessToken;
      return {
        id: s.id,
        name: s.name,
        type: s.type,
        typeLabel: app.sourceTypeLabel(s.type),
        enabled: s.enabled !== false,
        summary: configured ? '已配置' : '未配置'
      };
    });
    this.setData({ sources: list });
  },

  onAdd() {
    wx.navigateTo({ url: '/pages/sources/add' });
  },

  onEdit(e) {
    var id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/sources/add?id=' + id });
  },

  onToggle(e) {
    var id = e.currentTarget.dataset.id;
    var app = getApp();
    var s = app.getSource(id);
    if (!s) return;
    app.updateSource(id, { enabled: s.enabled === false });
    this.refresh();
  },

  onTest(e) {
    var id = e.currentTarget.dataset.id;
    var app = getApp();
    var s = app.getSource(id);
    if (!s) return;
    var self = this;
    var testing = {};
    testing[id] = true;
    self.setData({ testing: testing });
    app.testConnection(s).then(function (r) {
      var done = {};
      done[id] = false;
      self.setData({ testing: done });
      wx.showToast({ title: '连接成功，共 ' + r.count + ' 个视频', icon: 'success' });
    }).catch(function (err) {
      var done = {};
      done[id] = false;
      self.setData({ testing: done });
      wx.showModal({
        title: '连接失败',
        content: (err && err.errMsg) || (err && err.message) || '请检查地址与凭证',
        showCancel: false
      });
    });
  },

  onDelete(e) {
    var id = e.currentTarget.dataset.id;
    var self = this;
    wx.showModal({
      title: '删除数据源',
      content: '确定要删除这个数据源吗？',
      confirmText: '删除',
      cancelText: '取消',
      success: function (res) {
        if (res.confirm) {
          getApp().removeSource(id);
          self.refresh();
          wx.showToast({ title: '已删除' });
        }
      }
    });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
