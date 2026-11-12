Page({
  data: {
    mode: 'password',
    server: '',
    username: '',
    password: '',
    apiKey: '',
    loading: false,
    errMsg: '',
    connected: false,
    connectedServer: '',
    connectedUser: ''
  },

  onLoad() {
    var app = getApp();
    var cfg = app.loadConfig();
    if (cfg && cfg.server) {
      this.setData({
        server: app.serverLabel(cfg),
        username: cfg.userName || ''
      });
    }
    if (cfg && cfg.server && cfg.token && cfg.userId) {
      this.setData({
        connected: true,
        connectedServer: app.serverLabel(cfg),
        connectedUser: cfg.userName || '未知账号'
      });
    }
  },

  onInput(e) {
    var field = e.currentTarget.dataset.field;
    var patch = {};
    patch[field] = e.detail.value;
    this.setData(patch);
  },

  onMode(e) {
    this.setData({ mode: e.currentTarget.dataset.mode, errMsg: '' });
  },

  goHome() {
    wx.reLaunch({ url: '/pages/index/index' });
  },

  onDisconnect() {
    getApp().clearConfig();
    this.setData({
      connected: false,
      connectedServer: '',
      connectedUser: '',
      password: '',
      apiKey: '',
      errMsg: ''
    });
    wx.showToast({ title: '已断开连接' });
  },

  onConnect() {
    var self = this;
    if (self.data.loading) return;
    var app = getApp();
    var server = app.normalizeServer(self.data.server);
    if (!server) {
      self.setData({ errMsg: '请填写服务器地址' });
      return;
    }

    var mode = self.data.mode;
    var username = String(self.data.username || '').trim();
    var password = self.data.password;
    var apiKey = String(self.data.apiKey || '').trim();

    if (mode === 'password' && (!username || !password)) {
      self.setData({ errMsg: '请填写账号和密码' });
      return;
    }
    if (mode === 'apikey' && !apiKey) {
      self.setData({ errMsg: '请填写 API 密钥' });
      return;
    }

    self.setData({ loading: true, errMsg: '' });
    wx.showLoading({ title: '正在连接…' });

    var login = mode === 'apikey'
      ? app.loginWithApiKey(server, apiKey)
      : app.loginWithPassword(server, username, password);

    login.then(function (cfg) {
      return app.verifyConnection(cfg).then(function () {
        app.saveConfig(cfg);
        wx.hideLoading();
        self.setData({ loading: false, connected: true, connectedServer: app.serverLabel(cfg), connectedUser: cfg.userName });
        wx.showToast({ title: '连接成功' });
        setTimeout(function () { self.goHome(); }, 400);
      });
    }).catch(function (err) {
      wx.hideLoading();
      self.setData({
        loading: false,
        errMsg: (err && err.message) || '连接失败，请检查地址、账号与网络'
      });
    });
  }
});
