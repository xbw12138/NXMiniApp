Page({
  data: {
    editId: '',
    types: [
      { key: 'emby', label: 'Emby' },
      { key: 'jellyfin', label: 'Jellyfin' },
      { key: 'webdav', label: 'WebDAV' },
      { key: 'smb', label: 'SMB' },
      { key: 'aliyun', label: '阿里云盘' },
      { key: 'baidu', label: '百度网盘' }
    ],
    type: 'emby',
    name: '',
    // emby / jellyfin
    url: '',
    username: '',
    password: '',
    token: '',
    // webdav
    rootPath: '/',
    // smb
    server: '',
    share: '',
    domain: '',
    // aliyun
    refreshToken: '',
    // baidu
    accessToken: '',
    testing: false,
    testResult: ''
  },

  onLoad(query) {
    if (query.id) {
      var s = getApp().getSource(query.id);
      if (s) {
        var cfg = s.config || {};
        this.setData({
          editId: s.id,
          type: s.type,
          name: s.name,
          url: cfg.url || '',
          username: cfg.username || '',
          password: cfg.password || '',
          token: cfg.token || '',
          rootPath: cfg.rootPath || '/',
          server: cfg.server || cfg.url || '',
          share: cfg.share || '',
          domain: cfg.domain || '',
          refreshToken: cfg.refreshToken || '',
          accessToken: cfg.accessToken || ''
        });
        wx.setNavigationBarTitle({ title: '编辑数据源' });
        return;
      }
    }
    wx.setNavigationBarTitle({ title: '添加数据源' });
  },

  onType(e) {
    this.setData({ type: e.currentTarget.dataset.key, testResult: '' });
  },

  onInput(e) {
    var field = e.currentTarget.dataset.field;
    var patch = {};
    patch[field] = e.detail.value;
    this.setData(patch);
  },

  buildConfig() {
    var d = this.data;
    var cfg = {};
    if (d.type === 'emby' || d.type === 'jellyfin') {
      cfg.url = d.url;
      if (d.username) cfg.username = d.username;
      if (d.password) cfg.password = d.password;
      if (d.token) cfg.token = d.token;
    } else if (d.type === 'webdav') {
      cfg.url = d.url;
      cfg.username = d.username;
      cfg.password = d.password;
      cfg.rootPath = d.rootPath || '/';
    } else if (d.type === 'smb') {
      var addr = (d.server || d.url || '').trim().replace(/^smb:\/\//i, '');
      var shareName = (d.share || '').trim();
      // 允许把地址写成 host/share 或 \\host\share 的形式。
      addr = addr.replace(/^\\\\?/, '');
      var cut = addr.search(/[\/\\]/);
      if (cut >= 0) {
        if (!shareName) shareName = addr.slice(cut + 1);
        addr = addr.slice(0, cut);
      }
      cfg.server = addr.replace(/[\/\\]+$/, '');
      cfg.share = shareName.replace(/[\/\\]+$/, '');
      cfg.user = d.username;
      cfg.password = d.password;
      cfg.domain = d.domain;
    } else if (d.type === 'aliyun') {
      cfg.refreshToken = d.refreshToken;
    } else if (d.type === 'baidu') {
      cfg.accessToken = d.accessToken;
    }
    return cfg;
  },

  buildSource() {
    return {
      type: this.data.type,
      name: this.data.name || getApp().sourceTypeLabel(this.data.type),
      config: this.buildConfig(),
      enabled: true
    };
  },

  onTest() {
    var self = this;
    var source = self.buildSource();
    self.setData({ testing: true, testResult: '' });
    getApp().testConnection(source).then(function (r) {
      self.setData({ testing: false, testResult: 'ok' });
      wx.showToast({ title: '连接成功，' + r.count + ' 个视频', icon: 'success' });
    }).catch(function (err) {
      self.setData({ testing: false, testResult: 'fail' });
      wx.showModal({
        title: '连接失败',
        content: (err && err.errMsg) || (err && err.message) || '请检查配置',
        showCancel: false
      });
    });
  },

  onSave() {
    var app = getApp();
    var source = this.buildSource();
    if (this.data.editId) {
      app.updateSource(this.data.editId, source);
    } else {
      app.addSource(source);
    }
    wx.showToast({ title: '已保存' });
    setTimeout(function () { wx.navigateBack({ delta: 1 }); }, 600);
  }
});
