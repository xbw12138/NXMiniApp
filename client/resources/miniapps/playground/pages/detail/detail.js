Page({
  data: {
    queryText: '无',
    timeText: '',
    tick: 0,
    rows: []
  },

  onLoad(query) {
    const rows = [];
    for (let i = 1; i <= 24; i++) {
      rows.push({ id: i, text: '滚动项 ' + i });
    }
    this.setData({
      rows,
      queryText: JSON.stringify(query || {}),
      timeText: new Date().toLocaleString()
    });
  },

  onShow() {
    this.entered = Date.now();
    this.timer = setInterval(() => {
      this.setData({ tick: Math.floor((Date.now() - this.entered) / 1000) });
    }, 1000);
  },

  onHide() {
    if (this.timer) clearInterval(this.timer);
  },

  onUnload() {
    if (this.timer) clearInterval(this.timer);
  },

  onBack() {
    wx.navigateBack({ delta: 1 });
  }
});
