Page({
  data: {
    checking: true,
    logged: false,
    nickname: '',
    qrState: 'idle', // idle | loading | ready | expired | error
    qrStatus: '',
    qrRows: [],
    qrMod: 8,
    qrSize: 0,
    showCookie: false,
    cookieInput: '',
    errMsg: ''
  },

  token: '',
  timer: null,
  polling: false,
  finished: false,

  onShow() {
    var cookie = getApp().cookie();
    if (!cookie) {
      this.setData({ checking: false, logged: false });
      if (this.data.qrState === 'ready') {
        this.startPoll();
      } else if (!this.finished) {
        this.startQr();
      }
      return;
    }
    var self = this;
    self.setData({ checking: true });
    getApp().accountInfo().then(function (info) {
      self.setData({ checking: false, logged: true, nickname: info.nickname || '夸克用户' });
    }).catch(function () {
      try { wx.removeStorageSync('quark_cookie'); } catch (e) {}
      self.setData({ checking: false, logged: false });
      self.startQr();
    });
  },

  onHide() {
    this.stopPoll();
  },

  onUnload() {
    this.stopPoll();
  },

  startQr() {
    var self = this;
    self.stopPoll();
    self.finished = false;
    self.token = '';
    self.setData({ qrState: 'loading', qrStatus: '正在获取二维码…', qrRows: [], errMsg: '' });
    getApp().qrToken().then(function (r) {
      self.token = r.token;
      var m;
      try {
        m = getApp().qrMatrix(r.content);
      } catch (e) {
        self.setData({ qrState: 'error', qrStatus: '二维码生成失败，请改用 Cookie 登录' });
        return;
      }
      var mod = Math.max(2, Math.floor(420 / m.size));
      var rows = [];
      for (var rr = 0; rr < m.size; rr++) {
        var runs = [];
        var pos = 0;
        var c = 0;
        while (c < m.size) {
          if (m.modules[rr][c]) {
            var start = c;
            while (c < m.size && m.modules[rr][c]) c++;
            runs.push({ g: (start - pos) * mod, w: (c - start) * mod });
            pos = c;
          } else {
            c++;
          }
        }
        rows.push(runs);
      }
      self.setData({
        qrState: 'ready',
        qrStatus: '请使用夸克 App 扫码',
        qrRows: rows,
        qrMod: mod,
        qrSize: mod * m.size
      });
      self.startPoll();
    }).catch(function (err) {
      self.setData({ qrState: 'error', qrStatus: (err && err.message) || '获取二维码失败' });
    });
  },

  startPoll() {
    var self = this;
    self.stopPoll();
    self.timer = setInterval(function () { self.checkQr(); }, 2000);
  },

  stopPoll() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  },

  checkQr() {
    var self = this;
    if (self.finished || self.polling || !self.token || self.data.qrState !== 'ready') return;
    self.polling = true;
    getApp().qrPoll(self.token).then(function (r) {
      self.polling = false;
      if (self.finished) return;
      if (r.state === 'expired') {
        self.stopPoll();
        self.setData({ qrState: 'expired', qrStatus: '二维码已过期，请刷新' });
        return;
      }
      if (r.state === 'ok') {
        self.finished = true;
        self.stopPoll();
        self.setData({ qrState: 'loading', qrStatus: '已确认，正在登录…' });
        getApp().exchangeTicket(r.ticket).then(function () {
          return getApp().accountInfo();
        }).then(function (info) {
          self.setData({ logged: true, nickname: info.nickname || '夸克用户', errMsg: '' });
          wx.showToast({ title: '登录成功' });
        }).catch(function (err) {
          var msg = (err && err.message) || '登录失败';
          if (msg === 'EMPTY_COOKIES') msg = '登录凭证获取失败：请更新容器到最新版本，或改用 Cookie 登录';
          self.finished = false;
          self.setData({ qrState: 'error', qrStatus: msg });
        });
        return;
      }
      if (self.data.qrStatus !== '等待扫码…') self.setData({ qrStatus: '等待扫码…' });
    }).catch(function () {
      self.polling = false; // 网络抖动，下一拍自动重试
    });
  },

  onRefreshQr() {
    this.startQr();
  },

  onToggleCookie() {
    this.setData({ showCookie: !this.data.showCookie });
  },

  onCookie(e) {
    this.setData({ cookieInput: e.detail.value });
  },

  onLogin() {
    var cookie = (this.data.cookieInput || '').replace(/^\s+|\s+$/g, '');
    if (!cookie) {
      wx.showToast({ title: '请先粘贴 Cookie' });
      return;
    }
    var self = this;
    wx.setStorageSync('quark_cookie', cookie);
    wx.showLoading({ title: '正在验证…' });
    getApp().accountInfo().then(function (info) {
      wx.hideLoading();
      self.stopPoll();
      self.finished = true;
      self.setData({ logged: true, nickname: info.nickname || '夸克用户', errMsg: '', cookieInput: '' });
      wx.showToast({ title: '登录成功' });
    }).catch(function (err) {
      wx.hideLoading();
      try { wx.removeStorageSync('quark_cookie'); } catch (e) {}
      self.setData({ errMsg: (err && err.message) || 'Cookie 无效，请重新获取' });
    });
  },

  goList() {
    wx.navigateTo({ url: '/pages/list/list?fid=0&name=' + encodeURIComponent('我的夸克网盘') });
  },

  goSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  onLogout() {
    var self = this;
    wx.showModal({
      title: '退出登录',
      content: '将清除本机保存的 Cookie',
      confirmText: '退出',
      cancelText: '取消',
      success(res) {
        if (res.confirm) {
          try { wx.removeStorageSync('quark_cookie'); } catch (e) {}
          self.setData({ logged: false, cookieInput: '', nickname: '' });
          self.startQr();
        }
      }
    });
  }
});
