Page({
  data: {
    fid: '',
    name: '',
    sources: [],
    videoSrc: '',
    current: '',
    mode: 'transcode',
    statusText: '',
    playing: false,
    triedDirect: false,
    // 转码 m3u8 与原始直链都要带 Cookie，否则 CDN 直接 412。
    headers: ''
  },

  onLoad(query) {
    var fid = query.fid || '';
    var name = '';
    try { name = decodeURIComponent(query.name || ''); } catch (e) { name = query.name || ''; }
    var cookie = getApp().cookie();
    this.setData({ fid: fid, name: name, headers: cookie ? 'Cookie: ' + cookie : '' });
    wx.setNavigationBarTitle({ title: name || '视频播放' });
    this.loadTranscode();
  },

  loadTranscode() {
    var self = this;
    wx.showLoading({ title: '获取播放地址…' });
    getApp().videoSources(self.data.fid).then(function (body) {
      wx.hideLoading();
      var raw = (body.data && body.data.video_list) || [];
      var pref = ['normal', 'high', 'low', 'super', '2k', '4k'];
      var labelMap = { low: '流畅', normal: '标清', high: '高清', super: '超清', '2k': '2K', '4k': '4K' };
      var sources = [];
      pref.forEach(function (r) {
        raw.forEach(function (v) {
          var url = v && v.video_info && v.video_info.url;
          if (v && v.resolution === r && url) {
            var dup = false;
            sources.forEach(function (s) { if (s.url === url) dup = true; });
            if (!dup) sources.push({ res: r, label: labelMap[r] || r, url: url });
          }
        });
      });
      if (!sources.length) {
        self.setData({ statusText: '未获取到转码流，正在尝试原始文件直连…' });
        self.useDirect();
        return;
      }
      self.setData({ sources: sources });
      self.play(sources[0]);
    }).catch(function (err) {
      wx.hideLoading();
      self.setData({ statusText: '转码地址获取失败：' + ((err && err.message) || '') });
      self.useDirect();
    });
  },

  play(source) {
    this.setData({
      videoSrc: source.url,
      current: source.label || source.res || '',
      mode: 'transcode',
      statusText: '',
      triedDirect: false
    });
  },

  onPickSource(e) {
    var url = e.currentTarget.dataset.url;
    var found = null;
    this.data.sources.forEach(function (s) { if (s.url === url) found = s; });
    if (found) this.play(found);
  },

  useDirect() {
    var self = this;
    if (self.data.triedDirect) return;
    self.setData({ triedDirect: true });
    wx.showLoading({ title: '获取原始文件地址…' });
    getApp().downloadUrl(self.data.fid).then(function (url) {
      wx.hideLoading();
      self.setData({ videoSrc: url, current: '原始文件', mode: 'direct', statusText: '' });
    }).catch(function (err) {
      wx.hideLoading();
      self.setData({ statusText: '原始文件地址获取失败：' + ((err && err.message) || '') });
    });
  },

  onToggle() {
    var ctx = wx.createVideoContext('player');
    if (this.data.playing) ctx.pause();
    else ctx.play();
  },

  onPlay() { this.setData({ playing: true }); },
  onPause() { this.setData({ playing: false }); },
  onEnded() { this.setData({ playing: false }); },

  onError() {
    var self = this;
    self.setData({ playing: false });
    if (self.data.mode === 'transcode' && !self.data.triedDirect) {
      self.setData({ statusText: '转码播放失败，正在尝试原始文件直连…' });
      self.useDirect();
    } else {
      self.setData({ statusText: '播放失败：请切换清晰度或检查网络' });
    }
  }
});
