// 播放页 · 全屏沉浸
// 视频默认铺满并进入全屏，进度条等控件由容器原生控制层绘制（3 秒无操作自动隐藏）；
// 本页只额外叠一层 HUD（标题 + 按键提示）和播放列表面板。
// 按键：A 播放/暂停、←→ 快退/快进 10 秒、LT/RT 上一个/下一个、
//       + 展开/收起播放列表（↑↓ 也能唤出）、Y 全屏、B 返回列表。
var HUD_MS = 3000;

Page({
  data: {
    sourceId: '',
    itemId: '',
    title: '',
    videoSrc: '',
    headers: '',
    playing: false,
    loading: true,
    statusText: '',
    fullScreen: false,
    hudVisible: true,
    hudSub: '',
    panelOpen: false,
    currentIndex: 0,
    playlist: [],
    keyHints: [
      { key: 'A', label: '播放/暂停' },
      { key: '← →', label: '快退/快进' },
      { key: 'LT RT', label: '上一个/下一个' },
      { key: '+', label: '播放列表' },
      { key: 'Y', label: '全屏' },
      { key: 'B', label: '返回' }
    ]
  },

  onLoad(query) {
    var sourceId = '';
    var itemId = '';
    var title = '';
    try { sourceId = decodeURIComponent(query.sourceId || ''); } catch (e) { sourceId = query.sourceId || ''; }
    try { itemId = decodeURIComponent(query.itemId || ''); } catch (e) { itemId = query.itemId || ''; }
    try { title = decodeURIComponent(query.title || ''); } catch (e) { title = query.title || ''; }

    this._hudTimer = null;
    this._requestedFullscreen = false;

    var app = getApp();
    // 有同目录播放列表时整批带进页面，没有（搜索/最近播放进入）就播单个文件。
    var saved = app.getPlaylist(sourceId);
    var items = (saved && saved.items) ? saved.items.slice() : [];
    var idx = -1;
    for (var i = 0; i < items.length; i++) {
      if (items[i].path === itemId) { idx = i; break; }
    }
    if (idx < 0) {
      items = [{
        path: itemId,
        title: title || itemId.split('/').pop(),
        sizeText: ''
      }];
      idx = 0;
    }

    this.setData({ sourceId: sourceId, itemId: itemId, title: title, playlist: items });
    this.loadItem(idx);
  },

  onUnload() {
    this.clearHudTimer();
    this.saveProgress();
    // 页面销毁前显式停掉播放器，避免退出后音频还在响。
    try { if (this._ctx) this._ctx.stop(); } catch (e) {}
  },

  ctx() {
    if (!this._ctx) this._ctx = wx.createVideoContext('player');
    return this._ctx;
  },

  nudge(i) {
    var list = this.data.playlist || [];
    if (i < 0 || i >= list.length) return false;
    this.loadItem(i);
    return true;
  },

  loadItem(index) {
    var app = getApp();
    var list = this.data.playlist || [];
    var item = list[index];
    if (!item) return;

    var gid = this.data.sourceId + ':' + item.path;
    var prog = app.getProgress(gid);
    this.seekTo = prog ? prog.seconds : 0;
    this.currentTime = this.seekTo || 0;
    this._lastSaved = 0;
    this._requestedFullscreen = false;
    // 切集时先停掉旧播放器，再销毁 video 节点重建。
    if (this.data.videoSrc) {
      try { this.ctx().stop(); } catch (e) {}
    }

    var pos = (index + 1) + ' / ' + list.length;
    this.setData({
      itemId: item.path,
      title: item.title,
      currentIndex: index,
      hudSub: pos + (item.sizeText ? ' · ' + item.sizeText : ''),
      hudVisible: true,
      panelOpen: false,
      loading: true,
      videoSrc: '',
      playing: false,
      statusText: '正在获取播放地址…'
    });
    wx.setNavigationBarTitle({ title: item.title || '播放' });
    this.showHud();
    this.load();
  },

  load() {
    var app = getApp();
    var source = app.getSource(this.data.sourceId);
    if (!source) {
      this.setData({ loading: false, videoSrc: '', statusText: '数据源不存在，请返回后重新添加。' });
      return;
    }

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
      self.showHud();
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

  // 元数据就绪后再 seek，比 onReady 固定延时可靠；同时在这里请求全屏。
  onMeta() {
    var ctx = this.ctx();
    if (this.seekTo > 0) {
      var target = this.seekTo;
      this.seekTo = 0;
      try { ctx.seek(target); } catch (e) {}
    }
    if (!this._requestedFullscreen) {
      this._requestedFullscreen = true;
      try { ctx.requestFullScreen(); } catch (e) {}
    }
    this.showHud();
  },

  onFullscreen(e) {
    this.setData({ fullScreen: !!(e && e.detail && e.detail.fullScreen) });
    this.showHud();
  },

  onPlay() {
    this.setData({ playing: true });
    this.showHud();
  },

  onPause() {
    this.setData({ playing: false });
    this.showHud();
  },

  onEnded() {
    var app = getApp();
    this.setData({ playing: false });
    this.currentTime = 0;
    app.clearProgress(this.data.sourceId + ':' + this.data.itemId);
    // 目录播放列表：播完自动接着放下一部。
    if (this.data.currentIndex + 1 < (this.data.playlist || []).length) {
      this.nudge(this.data.currentIndex + 1);
    } else {
      this.showHud();
    }
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
    this.clearHudTimer();
    this.setData({
      playing: false,
      videoSrc: '',
      loading: false,
      hudVisible: false,
      panelOpen: false,
      statusText: msg
    });
  },

  // ---------- HUD / 播放列表面板 ----------
  showHud() {
    this.clearHudTimer();
    if (!this.data.videoSrc) {
      if (!this.data.hudVisible) this.setData({ hudVisible: true });
      return;
    }
    if (!this.data.hudVisible) this.setData({ hudVisible: true });
    var self = this;
    this._hudTimer = setTimeout(function () {
      self._hudTimer = null;
      if (self.data.panelOpen) return;
      self.setData({ hudVisible: false });
    }, HUD_MS);
  },

  clearHudTimer() {
    if (this._hudTimer) {
      clearTimeout(this._hudTimer);
      this._hudTimer = null;
    }
  },

  // 点画面：容器会切换播放/暂停并唤出原生控制条，这里同步把 HUD 一起亮出来。
  onScreenTap() {
    this.showHud();
  },

  openPanel() {
    // 按住 ↓/↑ 时容器会重复派发按键，面板已打开就直接忽略，避免反复 setData。
    if (this.data.panelOpen || !(this.data.playlist || []).length) return;
    this.clearHudTimer();
    this.setData({ panelOpen: true, hudVisible: true });
  },

  closePanel() {
    this.setData({ panelOpen: false });
    this.showHud();
  },

  onPick(e) {
    var i = Number(e.currentTarget.dataset.index);
    if (isNaN(i)) return;
    if (i === this.data.currentIndex) {
      this.closePanel();
      return;
    }
    this.saveProgress();
    this.loadItem(i);
  },

  // 页面按键优先于播放器默认键位（容器会先派发 onKeyDown）。
  // 摇杆只移动焦点，所以播放列表入口做成可聚焦的 HUD 按钮；
  // 十字键 ↑↓ 也直接唤出列表，两种操作都能进。
  onKeyDown(e) {
    var key = e && e.key;
    if (!key) return false;
    // + 键固定用来展开/收起播放列表。
    if (key === 'start') {
      if (this.data.panelOpen) {
        this.closePanel();
      } else if ((this.data.playlist || []).length) {
        this.openPanel();
      }
      return true;
    }
    if (this.data.panelOpen) {
      if (key === 'b') { this.closePanel(); return true; }
      // 面板里的上下移动与 A 播放交给容器的焦点导航、按钮激活。
      return false;
    }
    if (!this.data.videoSrc) return false;  // 加载/错误态保留默认焦点与返回逻辑

    if ((key === 'up' || key === 'down') && (this.data.playlist || []).length > 1) {
      this.openPanel();
      return true;
    }
    if (key === 'lt' && this.nudge(this.data.currentIndex - 1)) return true;
    if (key === 'rt' && this.nudge(this.data.currentIndex + 1)) return true;
    if (key === 'b') {
      this.saveProgress();
      wx.navigateBack({ delta: 1 });
      return true;
    }
    // 其余键（A / 左右 / Y）保持播放器默认行为，顺手把 HUD 亮回来。
    this.showHud();
    return false;
  },

  saveProgress() {
    if (this.currentTime > 0) {
      getApp().saveProgress(this.data.sourceId + ':' + this.data.itemId, this.currentTime);
    }
  },

  onBack() {
    this.saveProgress();
    wx.navigateBack({ delta: 1 });
  }
});
