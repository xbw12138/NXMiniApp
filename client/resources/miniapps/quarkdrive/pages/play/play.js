Page({
  data: {
    fid: '',
    name: '',
    displayName: '',
    sources: [],
    videoSrc: '',
    current: '',
    headers: '',
    loading: true,
    playing: false,
    directMode: false,
    directTried: false,
    statusText: '',
    errMsg: '',
    timeText: '00:00 / 00:00'
  },

  sourceIndex: 0,

  onLoad(query) {
    var fid = query.fid || '';
    var name = '';
    try { name = decodeURIComponent(query.name || ''); } catch (e) { name = query.name || ''; }
    this.setData({
      fid: fid,
      name: name,
      displayName: name.length > 34 ? name.slice(0, 34) + '…' : name,
      // 转码流与原始直链都做了防盗链，必须同时带 Cookie / 客户端 UA / Referer。
      headers: getApp().streamHeaders()
    });
    wx.setNavigationBarTitle({ title: name || '视频播放' });
    this.loadSources();
  },

  loadSources() {
    var self = this;
    this.sourceIndex = 0;
    this.setData({
      loading: true,
      errMsg: '',
      statusText: '正在获取播放地址…',
      videoSrc: '',
      sources: [],
      directMode: false,
      directTried: false
    });
    getApp().videoSources(this.data.fid).then(function (body) {
      var raw = (body.data && body.data.video_list) || [];
      var pref = ['high', 'normal', 'super', '2k', '4k', 'low'];
      var labelMap = { low: '流畅', normal: '标清', high: '高清', super: '超清', '2k': '2K', '4k': '4K' };
      var sources = [];
      pref.forEach(function (r) {
        raw.forEach(function (v) {
          var info = v && v.video_info;
          var url = info && info.url;
          // 跳过未转码完成 / 不可访问 / 无地址的清晰度，避免选中后直接播放失败。
          if (!v || !url) return;
          if (v.trans_status && v.trans_status !== 'success') return;
          if (v.accessable === false || v.accessible === false) return;
          if (v.resolution === r) {
            var dup = false;
            sources.forEach(function (s) { if (s.res === r) dup = true; });
            if (!dup) sources.push({ res: r, label: labelMap[r] || r, url: url });
          }
        });
      });
      if (!sources.length) {
        self.setData({ statusText: '未获取到转码流，正在尝试原始文件…' });
        self.useDirect();
        return;
      }
      self.setData({ sources: sources });
      // 服务端会给出推荐清晰度，优先用它（与夸克客户端一致）。
      var def = (body.data && body.data.default_resolution) || '';
      var start = 0;
      sources.forEach(function (s, i) { if (s.res === def) start = i; });
      self.playSource(sources[start], start);
    }).catch(function (err) {
      self.setData({ statusText: '转码地址获取失败，正在尝试原始文件…' });
      self.useDirect();
    });
  },

  playSource(source, index) {
    var self = this;
    this.sourceIndex = index;
    var same = source.url === this.data.videoSrc;
    this.setData({
      loading: false,
      errMsg: '',
      statusText: '',
      current: source.label || source.res || '',
      videoSrc: same ? '' : source.url
    });
    // src 与上一次相同时不会触发重新加载，先清空再设回去。
    if (same) {
      setTimeout(function () { self.setData({ videoSrc: source.url }); }, 80);
    }
  },

  onPickSource(e) {
    var index = Number(e.currentTarget.dataset.index);
    var source = this.data.sources[index];
    if (!source) return;
    this.setData({ directMode: false });
    this.playSource(source, index);
  },

  useDirect() {
    var self = this;
    if (self.data.directTried) return;
    self.setData({ directTried: true, loading: true, errMsg: '', statusText: '正在获取原始文件地址…' });
    getApp().downloadUrl(self.data.fid).then(function (url) {
      self.setData({ directMode: true });
      self.playSource({ label: '原始文件', url: url }, -1);
    }).catch(function (err) {
      self.setData({
        loading: false,
        statusText: '',
        errMsg: '原始文件地址获取失败：' + ((err && err.message) || '请稍后重试')
      });
    });
  },

  onRetry() {
    this.loadSources();
  },

  onError(e) {
    var detail = '';
    try {
      detail = (e && e.detail && (e.detail.errMsg || e.detail.error || e.detail.message)) || '';
    } catch (err) { detail = ''; }
    this.setData({ playing: false });

    if (!this.data.directMode && this.sourceIndex + 1 < this.data.sources.length) {
      this.setData({ statusText: '该清晰度播放失败，正在切换其他清晰度…' });
      this.playSource(this.data.sources[this.sourceIndex + 1], this.sourceIndex + 1);
      return;
    }
    if (!this.data.directMode && !this.data.directTried) {
      this.setData({ statusText: '转码播放失败，正在尝试原始文件…' });
      this.useDirect();
      return;
    }
    this.setData({
      loading: false,
      statusText: '',
      errMsg: '播放失败：可能是网络异常或视频编码不受支持' + (detail ? '（' + detail + '）' : '')
    });
  },

  onToggle() {
    var ctx = wx.createVideoContext('player');
    if (this.data.playing) ctx.pause();
    else ctx.play();
  },

  onPlay() { this.setData({ playing: true, statusText: '', errMsg: '' }); },
  onPause() { this.setData({ playing: false }); },
  onEnded() { this.setData({ playing: false }); },
  onWaiting() { this.setData({ statusText: '缓冲中…' }); },

  onTime(e) {
    var cur = Math.floor((e.detail && e.detail.currentTime) || 0);
    var dur = Math.floor((e.detail && e.detail.duration) || 0);
    if (cur === this._shownTime && dur === this._shownDur) return;
    this._shownTime = cur;
    this._shownDur = dur;
    this.setData({
      timeText: this.clock(cur) + ' / ' + this.clock(dur),
      statusText: this.data.statusText === '缓冲中…' ? '' : this.data.statusText
    });
  },

  clock(sec) {
    var s = Math.max(0, sec || 0);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var r = s % 60;
    function p(x) { return x < 10 ? '0' + x : '' + x; }
    return (h > 0 ? h + ':' : '') + p(m) + ':' + p(r);
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
