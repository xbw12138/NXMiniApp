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
    // SMB 扫描根目录：可手填，也可用「浏览目录」逐级选择
    smbRoot: '/',
    dirOpen: false,
    dirPath: '/',
    dirs: [],
    dirLoading: false,
    dirError: '',
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
          smbRoot: cfg.rootPath || '/',
          dirPath: cfg.rootPath || '/',
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

  // SMB 路径统一成 /a/b 形式，空值回落到共享根。
  normalizeSmbRoot(value) {
    var p = (value || '/').trim();
    if (p.charAt(0) !== '/') p = '/' + p;
    p = p.replace(/\/+$/, '');
    return p || '/';
  },

  // 把「地址」里可能混着的共享名拆出来，与保存时的规则保持一致。
  smbParts() {
    var d = this.data;
    var addr = (d.server || '').trim().replace(/^smb:\/\//i, '').replace(/^\\\\?/, '');
    var share = (d.share || '').trim();
    var cut = addr.search(/[\/\\]/);
    if (cut >= 0) {
      if (!share) share = addr.slice(cut + 1);
      addr = addr.slice(0, cut);
    }
    return {
      server: addr.replace(/[\/\\]+$/, ''),
      share: share.replace(/[\/\\]+$/, ''),
      user: (d.username || '').trim(),
      password: d.password || '',
      domain: (d.domain || '').trim()
    };
  },

  // 浏览目录时按当前表单凭证连接；地址/共享/账号变了就重连。
  connectForBrowse() {
    var self = this;
    var p = this.smbParts();
    var key = [p.server, p.share, p.user, p.password, p.domain].join('|');
    if (this._browseKey === key && this._browseId) return Promise.resolve(this._browseId);
    return new Promise(function (resolve, reject) {
      wx.smb.connect({
        server: p.server, share: p.share, user: p.user,
        password: p.password, domain: p.domain,
        success: function (res) {
          self._browseId = res.id;
          self._browseKey = key;
          resolve(res.id);
        },
        fail: function (err) {
          reject(new Error((err && err.errMsg) || 'SMB 连接失败'));
        }
      });
    });
  },

  loadDirs(path) {
    var self = this;
    path = this.normalizeSmbRoot(path);
    this.setData({ dirLoading: true, dirPath: path, dirError: '' });
    return new Promise(function (resolve) {
      wx.smb.listFiles({
        id: self._browseId,
        path: path,
        success: function (list) {
          var dirs = [];
          (list || []).forEach(function (e) {
            if (!e.isDir) return;
            if (/\/(\$RECYCLE|System Volume Information)/i.test(e.name || '')) return;
            var label = (e.name || '').split('/').pop() || e.name;
            dirs.push({ path: e.name, label: label });
          });
          dirs.sort(function (a, b) { return a.label < b.label ? -1 : (a.label > b.label ? 1 : 0); });
          self.setData({ dirs: dirs, dirLoading: false });
          resolve();
        },
        fail: function (err) {
          self.setData({
            dirs: [],
            dirLoading: false,
            dirError: (err && err.errMsg) || '读取目录失败，请检查凭证'
          });
          resolve();
        }
      });
    });
  },

  onBrowseDirs() {
    var d = this.data;
    if (!(d.server || '').trim()) {
      this.setData({ errText: '请先填写 NAS 地址' });
      return;
    }
    if (!(d.share || '').trim()) {
      this.setData({ errText: '请先填写共享名（例如 movies），再浏览目录' });
      return;
    }
    var self = this;
    this.setData({ dirOpen: true, dirLoading: true, dirError: '', errText: '' });
    this.connectForBrowse().then(function () {
      return self.loadDirs(self.data.smbRoot || '/');
    }).catch(function (e) {
      self.setData({
        dirLoading: false,
        dirs: [],
        dirError: (e && (e.errMsg || e.message)) || '无法读取目录'
      });
    });
  },

  onEnterDir(e) {
    var path = e.currentTarget.dataset.path;
    if (!path) return;
    this.loadDirs(path);
  },

  onParentDir() {
    var p = this.normalizeSmbRoot(this.data.dirPath);
    if (p === '/') return;
    var i = p.lastIndexOf('/');
    this.loadDirs(i <= 0 ? '/' : p.slice(0, i));
  },

  onUseDir() {
    this.setData({ smbRoot: this.data.dirPath, dirOpen: false });
  },

  onCloseDirs() {
    this.setData({ dirOpen: false });
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
      // 只扫用户指定的根目录，避免整盘递归。
      cfg.rootPath = this.normalizeSmbRoot(d.smbRoot);
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
    var source = this.buildSource();
    if (editing) {
      var old = app.getSource(this.data.editId);
      app.updateSource(this.data.editId, source);
      // SMB 根目录换了：旧索引和列表缓存都属于上一个目录，清掉让下次进首页重新扫。
      if (old && old.type === 'smb' && app.smbRootPath(old) !== app.smbRootPath(source)) {
        try { wx.removeStorageSync(app.moviesCacheKey(old.id)); } catch (e) {}
        try { wx.removeStorageSync(app.moviesCountKey(old.id)); } catch (e) {}
        try { wx.removeStorageSync('bmh_scan_' + old.id); } catch (e) {}
      }
    } else {
      app.addSource(source);
    }
    wx.showToast({ title: editing ? '已保存' : '已添加' });
    setTimeout(function () { wx.navigateBack({ delta: 1 }); }, 500);
  }
});
