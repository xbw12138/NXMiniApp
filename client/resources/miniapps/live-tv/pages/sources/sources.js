// 直播电视 · 直播源管理
function trim(text) {
  return String(text || '').replace(/^\s+|\s+$/g, '');
}

Page({
  data: {
    sources: [],
    name: '',
    url: '',
    adding: false,
    addMsg: ''
  },

  onShow() {
    this.refresh();
  },

  refresh() {
    var app = getApp();
    var list = app.getSources().map(function (s) {
      var cache = app.readSourceCache(s.id);
      return {
        id: s.id,
        name: s.name,
        url: s.url,
        builtin: !!s.builtin,
        enabled: s.enabled !== false,
        count: cache ? cache.list.length : 0,
        error: cache && cache.stale ? (cache.error || '上次刷新失败') : ''
      };
    });
    this.setData({ sources: list });
  },

  onToggle(e) {
    var id = e.currentTarget.dataset.id;
    var app = getApp();
    for (var i = 0; i < this.data.sources.length; i++) {
      if (this.data.sources[i].id !== id) continue;
      app.updateSource(id, { enabled: !this.data.sources[i].enabled });
      break;
    }
    this.refresh();
  },

  onDelete(e) {
    var id = e.currentTarget.dataset.id;
    var self = this;
    var app = getApp();
    wx.showModal({
      title: '删除直播源',
      content: '删除后该源里的频道会从频道墙移除，确定吗？',
      success: function (res) {
        if (!res.confirm) return;
        app.removeSource(id);
        self.refresh();
        wx.showToast({ title: '已删除', icon: 'none' });
      }
    });
  },

  onName(e) {
    this.setData({ name: e.detail.value, addMsg: '' });
  },

  onUrl(e) {
    this.setData({ url: e.detail.value, addMsg: '' });
  },

  onAdd() {
    var self = this;
    var app = getApp();
    var url = trim(this.data.url);
    if (!url) {
      this.setData({ addMsg: '请先填写 M3U 播放列表地址' });
      return;
    }
    if (url.indexOf('http') !== 0) {
      this.setData({ addMsg: '地址需以 http:// 或 https:// 开头' });
      return;
    }
    if (this.data.adding) return;

    this.setData({ adding: true, addMsg: '' });
    wx.showLoading({ title: '正在检测直播源…' });
    app.fetchChannels({ id: 'probe', url: url }).then(function (list) {
      wx.hideLoading();
      var source = app.addSource(trim(self.data.name) || '自定义直播源', url);
      app.writeSourceCache(source.id, { time: Date.now(), list: list });
      self.setData({
        adding: false,
        name: '',
        url: '',
        addMsg: '已经加进来了，共 ' + list.length + ' 个频道'
      });
      wx.showToast({ title: '直播源添加成功', icon: 'none' });
      self.refresh();
    }).catch(function (err) {
      wx.hideLoading();
      self.setData({
        adding: false,
        addMsg: '检测失败：' + ((err && err.message) || '无法读取这个地址')
      });
    });
  },

  onReset() {
    var self = this;
    var app = getApp();
    wx.showModal({
      title: '恢复默认源',
      content: '会清掉你添加的直播源，只保留内置源，确定吗？',
      success: function (res) {
        if (!res.confirm) return;
        app.resetSources();
        self.refresh();
        wx.showToast({ title: '已恢复默认源', icon: 'none' });
      }
    });
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
