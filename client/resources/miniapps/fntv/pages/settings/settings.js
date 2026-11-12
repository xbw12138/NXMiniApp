Page({
  data: {
    server: '',
    userName: '',
    authModeText: '',
    testing: false,
    resultText: ''
  },

  onShow() {
    var app = getApp();
    var cfg = app.loadConfig();
    if (!cfg || !cfg.server || !cfg.token) {
      wx.redirectTo({ url: '/pages/login/login' });
      return;
    }
    this.setData({
      server: app.serverLabel(cfg),
      userName: cfg.userName || '未知账号',
      authModeText: cfg.authMode === 'apikey' ? 'API 密钥' : '账号密码',
      resultText: ''
    });
  },

  onTest() {
    var self = this;
    if (self.data.testing) return;
    self.setData({ testing: true, resultText: '正在连接服务器…' });
    getApp().verifyConnection(getApp().getConfig()).then(function (views) {
      self.setData({
        testing: false,
        resultText: '连接正常，共 ' + (views ? views.length : 0) + ' 个媒体库'
      });
    }).catch(function (err) {
      self.setData({
        testing: false,
        resultText: '连接失败：' + ((err && err.message) || '未知错误')
      });
    });
  },

  onReconnect() {
    wx.navigateTo({ url: '/pages/login/login' });
  },

  onDisconnect() {
    wx.showModal({
      title: '断开连接',
      content: '断开后需要重新输入服务器与账号信息，确定继续吗？',
      success: function (res) {
        if (!res.confirm) return;
        getApp().clearConfig();
        wx.reLaunch({ url: '/pages/login/login' });
      }
    });
  },

  goHome() {
    wx.reLaunch({ url: '/pages/index/index' });
  }
});
