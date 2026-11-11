var BADGES = { emby: 'E', jellyfin: 'J', webdav: 'W', smb: 'S', aliyun: '阿', baidu: '百' };

Page({
  data: {
    sources: []
  },

  onLoad() {
    this.tests = {};
  },

  onShow() {
    this.refresh();
  },

  buildItem(s) {
    var app = getApp();
    var cfg = s.config || {};
    var addr = '';
    if (s.type === 'smb') {
      addr = (cfg.server || '') + (cfg.share ? ' / ' + cfg.share : '');
    } else if (s.type === 'aliyun') {
      addr = cfg.refreshToken ? 'refresh_token 已填写' : '未填写 refresh_token';
    } else if (s.type === 'baidu') {
      addr = cfg.accessToken ? 'access_token 已填写' : '未填写 access_token';
    } else {
      addr = cfg.url || '';
    }
    var cache = app.getMoviesCache(s.id);
    var test = this.tests && this.tests[s.id];
    return {
      id: s.id,
      type: s.type,
      badge: BADGES[s.type] || '?',
      name: s.name || app.sourceTypeLabel(s.type),
      addr: addr || '未填写地址',
      enabled: s.enabled !== false,
      countText: cache ? (cache.list.length + ' 部影片') : '尚未扫描',
      testText: test ? test.text : '',
      testOk: test ? test.ok : false
    };
  },

  refresh() {
    var list = getApp().getSources();
    var items = [];
    for (var i = 0; i < list.length; i++) items.push(this.buildItem(list[i]));
    this.setData({ sources: items });
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
    var next = s.enabled === false;
    app.updateSource(id, { enabled: next });
    wx.showToast({ title: next ? '已启用' : '已禁用' });
    this.refresh();
  },

  onTest(e) {
    var id = e.currentTarget.dataset.id;
    var app = getApp();
    var s = app.getSource(id);
    if (!s) return;
    var self = this;
    this.tests[id] = { text: '测试中…', ok: null };
    this.refresh();
    app.testConnection(s).then(function (r) {
      self.tests[id] = { text: '连接成功 · ' + r.count + ' 个视频', ok: true };
      self.refresh();
    }).catch(function (err) {
      self.tests[id] = { text: (err && (err.errMsg || err.message)) || '连接失败', ok: false };
      self.refresh();
    });
  },

  onDelete(e) {
    var id = e.currentTarget.dataset.id;
    var self = this;
    wx.showModal({
      title: '删除数据源',
      content: '删除后需要重新添加，已缓存的影片信息会一并清除。',
      success: function (res) {
        if (!res.confirm) return;
        getApp().removeSource(id);
        delete self.tests[id];
        try { wx.removeStorageSync(getApp().moviesCacheKey(id)); } catch (err) {}
        wx.showToast({ title: '已删除' });
        self.refresh();
      }
    });
  }
});
