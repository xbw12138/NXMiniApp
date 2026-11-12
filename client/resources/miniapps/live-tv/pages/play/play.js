// 直播电视 · 播放页
Page({
  data: {
    key: '',
    name: '',
    group: '',
    subtitle: '',
    src: '',
    lines: [],
    lineIndex: 0,
    lineTotal: 0,
    fav: false,
    status: '正在连接直播流…',
    error: '',
    prevName: '上一台',
    nextName: '下一台'
  },

  onLoad(query) {
    this.scope = query.scope || 'all';
    this.keyword = query.kw || '';
    this.list = [];
    this.index = 0;

    var app = getApp();
    var key = query.key ? decodeURIComponent(query.key) : '';
    var lib = app.loadLibraryCached();
    var channels = (lib && lib.channels) || [];

    if (!key || !channels.length) {
      this.setData({ error: '频道缓存已失效，请返回首页重新加载' });
      return;
    }

    var list = app.listForScope(channels, this.scope, this.keyword);
    if (!list.length) list = channels.slice();
    this.list = list;

    var target = app.findChannel(channels, key);
    for (var i = 0; i < list.length; i++) {
      if (list[i].key === key) {
        this.index = i;
        target = list[i];
        break;
      }
    }
    if (!target) {
      this.setData({ error: '找不到这个频道，可能直播源刚刚更新过' });
      return;
    }

    this.channel = target;
    this.playLine(0);
  },

  playLine(lineIndex) {
    var app = getApp();
    var channel = this.channel;
    var urls = (channel && channel.urls) || [];
    if (!urls.length) {
      this.setData({ error: '这个频道没有可用的播放地址', status: '' });
      return;
    }
    var index = lineIndex || 0;
    if (index < 0 || index >= urls.length) index = 0;

    app.addRecent(channel.key);

    var lines = [];
    for (var i = 0; i < urls.length; i++) {
      lines.push({ index: i, label: '线路 ' + (i + 1), on: i === index });
    }

    this.setData({
      key: channel.key,
      name: channel.name,
      group: channel.group,
      subtitle: channel.group + ' · ' + urls.length + ' 条线路',
      src: urls[index],
      lines: lines,
      lineIndex: index,
      lineTotal: urls.length,
      fav: app.isFav(channel.key),
      status: '线路 ' + (index + 1) + ' / ' + urls.length + ' · 正在连接…',
      error: '',
      prevName: this.neighborName(-1),
      nextName: this.neighborName(1)
    });
  },

  neighborName(step) {
    if (!this.list || this.list.length < 2) return step < 0 ? '上一台' : '下一台';
    var app = getApp();
    var i = (this.index + step + this.list.length) % this.list.length;
    return app.truncate(this.list[i].name, 8);
  },

  onPlay() {
    this.setData({ status: '', error: '' });
  },

  onWaiting() {
    this.setData({ status: '缓冲中…' });
  },

  onEnded() {
    this.setData({ status: '直播已结束，试试换线路或换台' });
  },

  // 播放失败：自动切到下一条线路，全部失败才报错。
  onVideoError() {
    var urls = (this.channel && this.channel.urls) || [];
    var next = this.data.lineIndex + 1;
    if (next < urls.length) {
      wx.showToast({ title: '线路 ' + (this.data.lineIndex + 1) + ' 不可用，自动切换', icon: 'none' });
      this.playLine(next);
      return;
    }
    this.setData({
      status: '',
      error: '这个频道的 ' + urls.length + ' 条线路都连不上，换台或稍后重试'
    });
  },

  onSwitchLine() {
    var urls = (this.channel && this.channel.urls) || [];
    if (urls.length < 2) {
      wx.showToast({ title: '这个频道只有一条线路', icon: 'none' });
      return;
    }
    this.playLine((this.data.lineIndex + 1) % urls.length);
  },

  onPickLine(e) {
    var index = Number(e.currentTarget.dataset.index);
    if (index === this.data.lineIndex) return;
    this.playLine(index);
  },

  onFav() {
    var app = getApp();
    var on = app.toggleFav(this.channel.key);
    this.setData({ fav: on });
    wx.showToast({ title: on ? '已收藏' : '已取消收藏', icon: 'none' });
  },

  onPrev() {
    this.go(-1);
  },

  onNext() {
    this.go(1);
  },

  go(step) {
    if (!this.list || this.list.length < 2) {
      wx.showToast({ title: '当前分组只有一个频道', icon: 'none' });
      return;
    }
    var next = (this.index + step + this.list.length) % this.list.length;
    var channel = this.list[next];
    // 上下台用 redirectTo 换页：新页 onLoad 会先于本页 onUnload 触发，
    // 这里标记一下，避免 onUnload 把新页刚起来的播放停掉。
    this.switching = true;
    wx.redirectTo({
      url: '/pages/play/play?key=' + encodeURIComponent(channel.key) +
           '&scope=' + encodeURIComponent(this.scope) +
           '&kw=' + encodeURIComponent(this.keyword)
    });
  },

  onFullscreen() {
    try {
      wx.createVideoContext('player').requestFullScreen();
    } catch (e) {
      wx.showToast({ title: '当前环境不支持全屏', icon: 'none' });
    }
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  },

  // 容器在页面返回销毁时不会主动停掉 MPV，必须自己停，否则退回频道列表后声音还在响。
  onUnload() {
    if (this.switching) return;
    this.stopPlayback();
  },

  stopPlayback() {
    if (this.stopped) return;
    this.stopped = true;
    try {
      wx.createVideoContext('player').stop();
    } catch (e) {}
  }
});
