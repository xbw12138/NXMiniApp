Page({
  data: {
    count: 0,
    enabled: true,
    accept: false,
    plan: 'a',
    volume: 60,
    nick: '',
    saved: '尚未写入存储',
    systemLine: '',
    videoSrc: 'https://www.w3schools.com/html/mov_bbb.mp4',
    videoStatus: '点击视频或按 A 播放',
    richNodes: '<b>富文本</b>：标签已剥离，仅保留文字内容',
    items: [
      { name: '小林', score: 96 },
      { name: '阿青', score: 82 },
      { name: 'Nova', score: 68 },
      { name: 'Mika', score: 74 },
      { name: 'Zed', score: 91 }
    ]
  },

  onLoad() {
    const info = wx.getSystemInfoSync();
    const base = wx.getAppBaseInfo();
    const storage = wx.getStorageInfoSync();
    this.setData({
      systemLine: info.windowWidth + 'x' + info.windowHeight + ' · ' + base.platform +
        ' · 存储键 ' + storage.keys.length
    });

    // 每两秒自增，用于验证定时器与 setData 的重绘链路。
    this.counter = setInterval(() => {
      this.setData({ count: this.data.count + 1 });
    }, 2000);
  },

  onUnload() {
    if (this.counter) clearInterval(this.counter);
  },

  onAdd() {
    this.setData({ count: this.data.count + 1 });
  },

  onToast() {
    wx.showToast({ title: '操作成功 · ' + this.data.count, icon: 'success' });
  },

  onModal() {
    wx.showModal({
      title: '确认操作',
      content: '这是宿主自绘的模态框。A 触发当前按钮，B 关闭。',
      success: (res) => {
        wx.showToast({ title: res.confirm ? '已确定' : '已取消' });
      }
    });
  },

  onLoading() {
    wx.showLoading({ title: '加载中' });
    setTimeout(() => {
      wx.hideLoading();
      wx.showToast({ title: '加载完成' });
    }, 1200);
  },

  onSwitch(e) {
    this.setData({ enabled: e.detail.value });
  },

  onCheckbox(e) {
    this.setData({ accept: e.detail.value });
  },

  onRadio(e) {
    this.setData({ plan: e.currentTarget.dataset.value });
  },

  onSlider(e) {
    this.setData({ volume: e.detail.value });
  },

  onInput(e) {
    this.setData({ nick: e.detail.value });
  },

  onConfirm(e) {
    const nick = e.detail.value || this.data.nick;
    this.setData({ nick });
    wx.showToast({ title: '昵称已确认' });
  },

  onSave() {
    const nick = this.data.nick || 'guest';
    wx.setStorageSync('nick', nick);
    this.setData({ saved: '已写入：' + nick });
  },

  onRead() {
    const nick = wx.getStorageSync('nick');
    this.setData({ saved: nick ? '读取到：' + nick : '暂无存储' });
  },

  onVideoPlayTap() {
    wx.createVideoContext('demoVideo').play();
  },

  onVideoPauseTap() {
    wx.createVideoContext('demoVideo').pause();
  },

  onVideoPlay() {
    this.setData({ videoStatus: '播放中' });
  },

  onVideoPause() {
    this.setData({ videoStatus: '已暂停' });
  },

  onVideoEnded() {
    this.setData({ videoStatus: '播放结束' });
  },

  onVideoTime(e) {
    const t = Math.floor(e.detail.currentTime || 0);
    this.setData({ videoStatus: '时间 ' + t + 's' });
  },

  onVideoError(e) {
    this.setData({ videoStatus: '视频错误：' + (e.detail.errMsg || '') });
  }
});
