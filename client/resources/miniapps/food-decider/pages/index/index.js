var MODES = [
  { key: 'food', label: '吃饭' },
  { key: 'dice', label: '骰子' },
  { key: 'coin', label: '硬币' }
];

var DEFAULTS = ['火锅', '烧烤', '日料', '麻辣烫', '家常菜', '汉堡', '面条', '轻食'];

Page({
  data: {
    modes: MODES,
    mode: 'food',
    tag: '吃什么',
    resultText: '？',
    resultSub: '添加菜单后，点下方按钮开始选择',
    spinLabel: '帮我决定',
    spinning: false,
    activeIndex: -1,
    options: [],
    presetRows: [
      { k: 0, names: ['火锅', '烧烤', '日料'] },
      { k: 1, names: ['麻辣烫', '家常菜', '汉堡'] },
      { k: 2, names: ['面条', '轻食', '饺子'] }
    ],
    newOption: '',
    history: [],
    infoTitle: '掷骰子',
    infoText: '点击左侧按钮掷出一颗 1-6 的骰子，适合决定谁去买单这类小问题。'
  },

  onLoad() {
    this.spinTimer = null;
    var stored = wx.getStorageSync('options');
    if (stored && stored.length) {
      this.options = stored;
    } else {
      this.options = DEFAULTS.slice();
      wx.setStorageSync('options', this.options);
    }
    this.history = wx.getStorageSync('history') || [];
    this.refreshOptions();
    this.setData({ history: this.history });
  },

  refreshOptions() {
    var list = [];
    for (var i = 0; i < this.options.length; i++) {
      list.push({ id: '' + i + '_' + this.options[i], idx: i + 1, name: this.options[i] });
    }
    this.setData({ options: list });
  },

  onMode(e) {
    var key = e.currentTarget.dataset.key;
    if (key === this.data.mode) return;
    this.clearSpin();
    var patch = { mode: key, spinning: false, activeIndex: -1, resultText: '？' };
    if (key === 'food') {
      patch.tag = '吃什么';
      patch.resultSub = '添加菜单后，点下方按钮开始选择';
      patch.spinLabel = '帮我决定';
      patch.infoTitle = '';
      patch.infoText = '';
    } else if (key === 'dice') {
      patch.tag = '掷骰子';
      patch.resultSub = '点击下方按钮掷出 1-6';
      patch.spinLabel = '掷骰子';
      patch.infoTitle = '骰子模式';
      patch.infoText = '随机掷出一颗 1-6 的骰子，适合决定谁去买单、谁先动手这类小问题。';
    } else {
      patch.tag = '抛硬币';
      patch.resultSub = '点击下方按钮抛出正反面';
      patch.spinLabel = '抛硬币';
      patch.infoTitle = '硬币模式';
      patch.infoText = '随机抛出正面或反面，二选一的犹豫时刻交给它就好。';
    }
    this.setData(patch);
  },

  onSpin() {
    if (this.data.spinning) return;
    if (this.data.mode === 'dice') {
      var n = Math.floor(Math.random() * 6) + 1;
      this.showResult('' + n, '点数为 ' + n, '掷骰子');
      return;
    }
    if (this.data.mode === 'coin') {
      var face = Math.random() < 0.5 ? '正面' : '反面';
      this.showResult(face, '硬币落定，结果是' + face, '抛硬币');
      return;
    }
    if (this.options.length === 0) {
      wx.showToast({ title: '先添加几个想吃的', icon: 'none' });
      return;
    }
    this.startSpin();
  },

  startSpin() {
    var self = this;
    var steps = 0;
    var total = 20;
    this.setData({ spinning: true, spinLabel: '正在挑选…' });
    this.clearSpin();
    this.spinTimer = setInterval(function () {
      steps++;
      var idx = steps % self.options.length;
      self.setData({ activeIndex: idx, resultText: self.options[idx] });
      if (steps >= total) {
        self.clearSpin();
        var finalIdx = Math.floor(Math.random() * self.options.length);
        self.setData({ spinning: false, activeIndex: finalIdx, spinLabel: '再选一次' });
        self.showResult(self.options[finalIdx], '今天就吃这个吧', '吃什么', finalIdx + 1);
      }
    }, 70);
  },

  clearSpin() {
    if (this.spinTimer) {
      clearInterval(this.spinTimer);
      this.spinTimer = null;
    }
  },

  showResult(text, sub, from, choice) {
    this.setData({ resultText: text, resultSub: sub });
    var h = [{ id: '' + Date.now(), text: text }];
    var history = h.concat(this.history).slice(0, 6);
    this.history = history;
    wx.setStorageSync('history', history);
    this.setData({ history: history });
  },

  onInput(e) {
    this.setData({ newOption: e.detail.value });
  },

  onAdd() {
    var name = (this.data.newOption || '').replace(/^\s+|\s+$/g, '');
    if (!name) {
      wx.showToast({ title: '先写个想吃的', icon: 'none' });
      return;
    }
    if (this.options.indexOf(name) >= 0) {
      wx.showToast({ title: '已经在菜单里了', icon: 'none' });
      this.setData({ newOption: '' });
      return;
    }
    this.options.push(name);
    wx.setStorageSync('options', this.options);
    this.setData({ newOption: '' });
    this.refreshOptions();
  },

  onPreset(e) {
    var name = e.currentTarget.dataset.name;
    if (this.options.indexOf(name) >= 0) {
      wx.showToast({ title: '已经有了', icon: 'none' });
      return;
    }
    this.options.push(name);
    wx.setStorageSync('options', this.options);
    this.refreshOptions();
  },

  onDel(e) {
    var id = e.currentTarget.dataset.id;
    var parts = id.split('_');
    var pos = parseInt(parts[0], 10);
    if (isNaN(pos)) return;
    this.options.splice(pos, 1);
    wx.setStorageSync('options', this.options);
    this.refreshOptions();
  },

  onUnload() {
    this.clearSpin();
  }
});
