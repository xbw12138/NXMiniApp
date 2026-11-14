Page({
  data: {
    sourceId: '',
    itemId: '',
    title: '',
    videoSrc: '',
    headers: '',
    playing: false,
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

    // 续播位置只在内存里记录，时间显示交给容器原生控制层，避免每秒 setData 重排。
    var source = getApp().getSource(sourceId);
    var prog = source ? getApp().getProgress(sourceId + ':' + itemId) : null;
    this.seekTo = prog ? prog.seconds : 0;
    this.currentTime = this.seekTo || 0;

    this.load();
  },

  load() {
    var app = getApp();
    var source = app.getSource(this.data.sourceId);
    if (!source) {
      this.setData({ loading: false, videoSrc: '', statusText: '数据源不存在，请返回后重新添加。' });
      return;
    }
    this.setData({
      loading: true,
      videoSrc: '',
      playing: false,
      statusText: '正在获取播放地址…'
    });

    var self = this;
    app.getPlayUrl(source, this.data.itemId).then(function (result) {
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
      console.log('play: src ready');
      self.setData({
        videoSrc: url,
        headers: headers,
        loading: false,
        statusText: ''
      });
    }).catch(function (err) {
      self.setData({
        loading: false,
        videoSrc: '',
        statusText: '获取播放地址失败：' + ((err && err.message) || (err && err.errMsg) || '未知错误')
      });
    });
  },

  onRetry() {
    this._lastSaved = 0;
    this.load();
  },

  // 元数据就绪后再 seek，比 onReady 固定延时可靠。
  onMeta() {
    if (this.seekTo > 0) {
      var target = this.seekTo;
      this.seekTo = 0;
      try { wx.createVideoContext('player').seek(target); } catch (e) {}
    }
  },

  onPlay() {
    this.setData({ playing: true });
  },

  onPause() {
    this.setData({ playing: false });
  },

  onEnded() {
    this.setData({ playing: false });
    this.currentTime = 0;
    var gid = this.data.sourceId + ':' + this.data.itemId;
    getApp().saveProgress(gid, 0);
  },

  onTime(e) {
    var cur = Math.floor(e.detail.currentTime || 0);
    this.currentTime = cur;
    // 进度按秒落盘；界面不再 setData，避免播放时反复重建渲染树造成卡顿。
    if (cur > 0 && cur % 5 === 0 && cur !== this._lastSaved) {
      this._lastSaved = cur;
      getApp().saveProgress(this.data.sourceId + ':' + this.data.itemId, cur);
    }
  },

  onError(e) {
    // 收起播放器，露出可读原因和重试入口，避免只剩黑屏。
    var msg = (e.detail && e.detail.errMsg) || '请检查网络或数据源';
    console.log('play: error ' + msg);
    this.setData({
      playing: false,
      videoSrc: '',
      loading: false,
      statusText: msg
    });
  },

  saveProgress() {
    if (this.currentTime > 0) {
      var gid = this.data.sourceId + ':' + this.data.itemId;
      getApp().saveProgress(gid, this.currentTime);
    }
  },

  onBack() {
    this.saveProgress();
    wx.navigateBack({ delta: 1 });
  },

  onUnload() {
    this.saveProgress();
  }
});
