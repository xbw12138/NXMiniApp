Page({
  data: {
    sourceId: '',
    itemId: '',
    title: '',
    videoSrc: '',
    headers: '',
    playing: false,
    currentTime: 0,
    duration: 0,
    timeText: '00:00 / 00:00',
    statusText: '正在获取播放地址…',
    loading: true
  },

  onLoad(query) {
    var sourceId = '';
    var itemId = '';
    var title = '';
    try { sourceId = decodeURIComponent(query.sourceId || ''); } catch (e) { sourceId = query.sourceId || ''; }
    try { itemId = decodeURIComponent(query.itemId || ''); } catch (e) { itemId = query.itemId || ''; }
    try { title = decodeURIComponent(query.title || ''); } catch (e) { title = query.title || ''; }

    this.setData({ sourceId: sourceId, itemId: itemId, title: title });
    wx.setNavigationBarTitle({ title: title || '播放' });

    var app = getApp();
    var source = app.getSource(sourceId);
    if (!source) {
      this.setData({ loading: false, statusText: '数据源不存在' });
      return;
    }

    var self = this;
    // 恢复上次进度
    var gid = sourceId + ':' + itemId;
    var prog = app.getProgress(gid);
    self.seekTo = prog ? prog.seconds : 0;

    app.getPlayUrl(source, itemId).then(function (result) {
      // result 可能是字符串 URL，也可能是 {url, headers}
      var url = '';
      var headers = '';
      if (typeof result === 'string') {
        url = result;
      } else if (result && result.url) {
        url = result.url;
        headers = result.headers || '';
      }
      if (!url) throw new Error('未获取到播放地址');
      self.setData({
        videoSrc: url,
        headers: headers,
        loading: false,
        statusText: ''
      });
    }).catch(function (err) {
      self.setData({
        loading: false,
        statusText: '获取播放地址失败：' + ((err && err.message) || (err && err.errMsg) || '未知错误')
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

  onEnded() {
    this.setData({ playing: false });
    var gid = this.data.sourceId + ':' + this.data.itemId;
    getApp().saveProgress(gid, 0);
  },

  onTime(e) {
    var cur = Math.floor(e.detail.currentTime || 0);
    var dur = Math.floor(e.detail.duration || 0);
    var app = getApp();
    this.setData({
      currentTime: cur,
      duration: dur,
      timeText: app.fmtTime(cur) + ' / ' + app.fmtTime(dur)
    });
    if (cur > 0 && cur % 5 === 0 && cur !== this._lastSaved) {
      this._lastSaved = cur;
      var gid = this.data.sourceId + ':' + this.data.itemId;
      app.saveProgress(gid, cur);
    }
  },

  onError(e) {
    // 播放失败时收起播放器，露出错误提示和返回按钮，避免黑屏无从操作。
    this.setData({
      playing: false,
      videoSrc: '',
      statusText: '播放失败：' + ((e.detail && e.detail.errMsg) || '请检查网络或数据源')
    });
  },

  onToggle() {
    var ctx = wx.createVideoContext('player');
    if (this.data.playing) ctx.pause();
    else ctx.play();
  },

  onSeek(e) {
    var delta = Number(e.currentTarget.dataset.delta);
    var ctx = wx.createVideoContext('player');
    var target = Math.max(0, this.data.currentTime + delta);
    ctx.seek(target);
  },

  onBack() {
    if (this.data.currentTime > 0) {
      var gid = this.data.sourceId + ':' + this.data.itemId;
      getApp().saveProgress(gid, this.data.currentTime);
    }
    wx.navigateBack({ delta: 1 });
  }
});
