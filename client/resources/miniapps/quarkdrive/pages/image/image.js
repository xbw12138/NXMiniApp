Page({
  data: {
    fid: '',
    name: '',
    src: '',
    headers: '',
    errMsg: ''
  },

  onLoad(query) {
    var name = '图片';
    try {
      name = decodeURIComponent(query.name || '') || name;
    } catch (e) {
      name = query.name || name;
    }
    var cookie = getApp().cookie();
    this.setData({
      fid: query.fid || '',
      name: name,
      // 夸克的直链（download/preview）都校验 Cookie，需要一并透传给图片组件。
      headers: cookie ? 'Cookie: ' + cookie : ''
    });
    wx.setNavigationBarTitle({ title: name });
    this.reload();
  },

  reload() {
    var self = this;
    if (!self.data.fid) {
      self.setData({ errMsg: '缺少文件 ID' });
      return;
    }
    self.setData({ errMsg: '', src: '' });
    getApp().fileUrls(self.data.fid).then(function (d) {
      self.setData({ src: d.download_url || d.preview_url || '' });
      if (!self.data.src) self.setData({ errMsg: '未拿到图片地址' });
    }).catch(function (err) {
      self.setData({ errMsg: (err && err.message) || '图片加载失败' });
    });
  },

  onBack() {
    wx.navigateBack();
  }
});
