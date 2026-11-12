Page({
  data: {
    itemId: '',
    title: '',
    videoSrc: '',
    playing: false,
    currentTime: 0,
    duration: 0,
    timeText: '00:00 / 00:00',
    statusText: '正在获取播放地址…',
    loading: true
  },

  onLoad(query) {
    var itemId = '';
    var title = '';
    var position = 0;
    try { itemId = decodeURIComponent(query.itemId || ''); } catch (e) { itemId = query.itemId || ''; }
    try { title = decodeURIComponent(query.title || ''); } catch (e) { title = query.title || ''; }
    position = Number(query.position || 0) || 0;

    this.setData({ itemId: itemId, title: title });
    this.seekTo = position;
    wx.setNavigationBarTitle({ title: title || '播放' });

    if (!itemId) {
      this.setData({ loading: false, statusText: '播放参数缺失' });
      return;
    }
    this.fetchUrl();
  },

  fetchUrl() {
    var self = this;
    var app = getApp();
    self.setData({ loading: true, statusText: '正在获取播放地址…', videoSrc: '' });

    app.getPlayUrl(self.data.itemId).then(function (url) {
      if (!url) throw new Error('未获取到播放地址');
      self.setData({
        videoSrc: url,
        loading: false,
        statusText: ''
      });
      app.reportPlayback('start', self.data.itemId, self.seekTo);
    }).catch(function (err) {
      self.setData({
        loading: false,
        statusText: '获取播放地址失败：' + ((err && err.message) || '未知错误')
      });
    });
  },

  onReady() {
    var self = this;
    if (this.seekTo > 0) {
      setTimeout(function () {
        try { wx.createVideoContext('player').seek(self.seekTo); } catch (e) {}
      }, 800);
    }
  },

  onPlay() {
    this.setData({ playing: true, statusText: '' });
  },

  onPause() {
    this.setData({ playing: false });
  },

  onTime(e) {
    var cur = Math.floor(e.detail.currentTime || 0);
    var dur = Math.floor(e.detail.duration || 0);
    this._now = cur;
    this._duration = dur;

    // 播放进度每 30 秒同步一次到飞牛影视，保证多端续播一致。
    if (cur > 0 && (!this._lastReport || cur - this._lastReport >= 30)) {
      this._lastReport = cur;
      getApp().reportPlayback('progress', this.data.itemId, cur);
    }

    // timeupdate 每秒会触发多次，HUD 每 2 秒刷新一次，避免频繁重建渲染树。
    if (this._shownSecond && Math.floor(cur / 2) === Math.floor(this._shownSecond / 2)) return;
    this._shownSecond = cur;
    this.setData({
      currentTime: cur,
      duration: dur,
      timeText: getApp().fmtTime(cur) + ' / ' + getApp().fmtTime(dur)
    });
  },

  onEnded() {
    this.setData({ playing: false });
    this._stopped = true;
    var app = getApp();
    app.reportPlayback('stop', this.data.itemId, this._duration || this._now || 0);
    app.markPlayed(this.data.itemId);
  },

  onError(e) {
    this.setData({
      playing: false,
      videoSrc: '',
      statusText: '播放失败：' + ((e.detail && e.detail.errMsg) || '请检查 NAS 网络或视频编码')
    });
  },

  onToggle() {
    var ctx = wx.createVideoContext('player');
    if (this.data.playing) ctx.pause();
    else ctx.play();
  },

  onSeek(e) {
    var delta = Number(e.currentTarget.dataset.delta) || 0;
    var target = Math.max(0, (this._now || 0) + delta);
    try { wx.createVideoContext('player').seek(target); } catch (err) {}
  },

  onRetry() {
    this.seekTo = this._now || this.seekTo || 0;
    this.fetchUrl();
  },

  onBack() {
    var position = this._now || 0;
    if (position > 0) getApp().reportPlayback('stop', this.data.itemId, position);
    this._stopped = true;
    this.stopPlayer();
    wx.navigateBack({ delta: 1 });
  },

  // 页面返回时容器不会主动停止播放器，必须显式 stop，否则退出后仍有声音。
  stopPlayer() {
    try { wx.createVideoContext('player').stop(); } catch (e) {}
  },

  onUnload() {
    this.stopPlayer();
    if (this._now > 0 && !this._stopped) {
      getApp().reportPlayback('stop', this.data.itemId, this._now);
    }
  }
});
