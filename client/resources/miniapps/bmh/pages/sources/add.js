// 容器的 Yoga 不支持 flex-wrap，类型卡按行切好再渲染。
function chunkRows(list, cols) {
  var rows = [];
  for (var i = 0; i < list.length; i += cols) {
    rows.push(list.slice(i, i + cols));
  }
  return rows;
}

var TYPE_COLS = 3;
var TYPES = [
  { key: 'emby', label: 'Emby', badge: 'E', desc: '媒体服务器' },
  { key: 'jellyfin', label: 'Jellyfin', badge: 'J', desc: '媒体服务器' },
  { key: 'webdav', label: 'WebDAV', badge: 'W', desc: 'NAS / 网盘' },
  { key: 'smb', label: 'SMB', badge: 'S', desc: '局域网共享' },
  { key: 'aliyun', label: '阿里云盘', badge: '阿', desc: '填写 token' },
  { key: 'baidu', label: '百度网盘', badge: '百', desc: '填写 token' }
];

Page({
  data: {
    editId: '',
    types: TYPES,
    typeRows: chunkRows(TYPES, TYPE_COLS),
    type: 'emby',
    name: '',
    // emby / jellyfin
    url: '',
    username: '',
    password: '',
    token: '',
    // webdav
    rootPath: '/',
    // smb
    server: '',
    share: '',
    domain: '',
    // aliyun
    refreshToken: '',
    // baidu
    accessToken: '',
    testing: false,
    testState: 'idle',
    testText: '',
    errText: ''
  },

  onLoad(query) {
    if (query.id) {
      var s = getApp().getSource(query.id);
      if (s) {
        var cfg = s.config || {};
        this.setData({
          editId: s.id,
          type: s.type,
          name: s.name,
          url: cfg.url || '',
          username: cfg.username || cfg.user || '',
          password: cfg.password || '',
          token: cfg.token || '',
          rootPath: cfg.rootPath || '/',
          server: cfg.server || cfg.url || '',
          share: cfg.share || '',
          domain: cfg.domain || '',
          refreshToken: cfg.refreshToken || '',
          accessToken: cfg.accessToken || ''
        });
        wx.setNavigationBarTitle({ title: '编辑数据源' });
        return;
      }
    }
    wx.setNavigationBarTitle({ title: '添加数据源' });
  },

  onType(e) {
    var key = e.currentTarget.dataset.key;
    if (!key || key === this.data.type) return;
    this.setData({ type: key, errText: '', testState: 'idle', testText: '' });
  },

  onInput(e) {
    var field = e.currentTarget.dataset.field;
    if (!field) return;
    var patch = { errText: '' };
    patch[field] = e.detail.value;
    this.setData(patch);
  },

  // 地址允许省略协议：192.168.1.10:8096 → http://192.168.1.10:8096
  normalizeUrl(value) {
    var v = (value || '').trim();
    if (!v) return '';
    if (!/^https?:\/\//i.test(v)) v = 'http://' + v;
    return v.replace(/\/+$/, '');
  },

  validate() {
    var d = this.data;
    if (d.type === 'emby' || d.type === 'jellyfin') {
      if (!this.normalizeUrl(d.url)) return '请填写服务器地址，例如 192.168.1.100:8096';
      if (!(d.token || '').trim() && !((d.username || '').trim() && (d.password || '').trim())) {
        return '请填写 API Key，或填完整的用户名和密码';
      }
    } else if (d.type === 'webdav') {
      if (!this.normalizeUrl(d.url)) return '请填写 WebDAV 地址，例如 192.168.1.100:5005';
    } else if (d.type === 'smb') {
      if (!(d.server || '').trim()) return '请填写 NAS 地址或主机名';
    } else if (d.type === 'aliyun') {
      if (!(d.refreshToken || '').trim()) return '请填写 refresh_token';
    } else if (d.type === 'baidu') {
      if (!(d.accessToken || '').trim()) return '请填写 access_token';
    }
    return '';
  },

  buildConfig() {
    var d = this.data;
    var cfg = {};
    if (d.type === 'emby' || d.type === 'jellyfin') {
      cfg.url = this.normalizeUrl(d.url);
      if ((d.username || '').trim()) cfg.username = d.username.trim();
      if ((d.password || '').trim()) cfg.password = d.password.trim();
      if ((d.token || '').trim()) cfg.token = d.token.trim();
    } else if (d.type === 'webdav') {
      cfg.url = this.normalizeUrl(d.url);
      cfg.username = (d.username || '').trim();
      cfg.password = d.password || '';
      cfg.rootPath = (d.rootPath || '/').trim() || '/';
    } else if (d.type === 'smb') {
      var addr = (d.server || '').trim().replace(/^smb:\/\//i, '').replace(/^\\\\?/, '');
      var shareName = (d.share || '').trim();
      // 允许把地址写成 host/share 或 \\host\share 的形式。
      var cut = addr.search(/[\/\\]/);
      if (cut >= 0) {
        if (!shareName) shareName = addr.slice(cut + 1);
        addr = addr.slice(0, cut);
      }
      cfg.server = addr.replace(/[\/\\]+$/, '');
      cfg.share = shareName.replace(/[\/\\]+$/, '');
      cfg.user = (d.username || '').trim();
      cfg.password = d.password || '';
      cfg.domain = (d.domain || '').trim();
    } else if (d.type === 'aliyun') {
      cfg.refreshToken = (d.refreshToken || '').trim();
    } else if (d.type === 'baidu') {
      cfg.accessToken = (d.accessToken || '').trim();
    }
    return cfg;
  },

  buildSource() {
    var app = getApp();
    var d = this.data;
    var source = {
      type: d.type,
      name: (d.name || '').trim() || app.sourceTypeLabel(d.type),
      config: this.buildConfig(),
      enabled: true
    };
    if (d.editId) {
      // 编辑时保留原来的 id 与启用状态，避免保存后被悄悄启用。
      var old = app.getSource(d.editId);
      if (old) {
        source.id = old.id;
        source.enabled = old.enabled !== false;
      }
    }
    return source;
  },

  onTest() {
    var err = this.validate();
    if (err) {
      this.setData({ errText: err });
      return;
    }
    var self = this;
    self.setData({ testing: true, testState: 'testing', testText: '正在连接…', errText: '' });
    getApp().testConnection(this.buildSource()).then(function (r) {
      self.setData({
        testing: false,
        testState: 'ok',
        testText: '连接成功 · 共 ' + r.count + ' 个视频'
      });
    }).catch(function (e) {
      self.setData({
        testing: false,
        testState: 'fail',
        testText: '连接失败：' + ((e && (e.errMsg || e.message)) || '请检查地址与凭证')
      });
    });
  },

  onSave() {
    var err = this.validate();
    if (err) {
      this.setData({ errText: err });
      return;
    }
    var app = getApp();
    var editing = !!this.data.editId;
    if (editing) {
      app.updateSource(this.data.editId, this.buildSource());
    } else {
      app.addSource(this.buildSource());
    }
    wx.showToast({ title: editing ? '已保存' : '已添加' });
    setTimeout(function () { wx.navigateBack({ delta: 1 }); }, 500);
  }
});
