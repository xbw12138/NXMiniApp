var MODES = [
  { key: 'focus', label: '专注 25', min: 25, accent: '#ff6b5b' },
  { key: 'short', label: '短休 5', min: 5, accent: '#34d399' },
  { key: 'long', label: '长休 15', min: 15, accent: '#38bdf8' }
];

function todayKey() {
  var d = new Date();
  return d.getFullYear() + '-' + (d.getMonth() + 1) + '-' + d.getDate();
}

Page({
  data: {
    modes: MODES,
    modeKey: 'focus',
    timeText: '25:00',
    stateText: '准备好就开始',
    percent: 0,
    accent: '#ff6b5b',
    running: false,
    todayCount: 0,
    todayMinutes: 0,
    tasks: [],
    doneCount: 0,
    newTask: '',
    msg: ''
  },

  onLoad() {
    this.total = 25 * 60;
    this.remaining = this.total;
    this.loadStats();
    this.loadTasks();
    this.renderTime();
    this.startTicker();
  },

  modeOf(key) {
    for (var i = 0; i < MODES.length; i++) {
      if (MODES[i].key === key) return MODES[i];
    }
    return MODES[0];
  },

  loadStats() {
    var stats = wx.getStorageSync('stats') || {};
    if (stats.date !== todayKey()) {
      stats = { date: todayKey(), count: 0, minutes: 0 };
      wx.setStorageSync('stats', stats);
    }
    this.setData({
      todayCount: stats.count || 0,
      todayMinutes: stats.minutes || 0
    });
  },

  saveStats(count, minutes) {
    wx.setStorageSync('stats', {
      date: todayKey(),
      count: count,
      minutes: minutes
    });
    this.setData({ todayCount: count, todayMinutes: minutes });
  },

  loadTasks() {
    var tasks = wx.getStorageSync('tasks') || [];
    this.setData({ tasks: tasks, doneCount: this.countDone(tasks) });
  },

  countDone(tasks) {
    var n = 0;
    for (var i = 0; i < tasks.length; i++) if (tasks[i].done) n++;
    return n;
  },

  renderTime() {
    var m = Math.floor(this.remaining / 60);
    var s = this.remaining % 60;
    var text = (m < 10 ? '0' + m : '' + m) + ':' + (s < 10 ? '0' + s : '' + s);
    var mode = this.modeOf(this.data.modeKey);
    var percent = this.total === 0 ? 0 : Math.round((this.total - this.remaining) / this.total * 100);
    var state = '准备好就开始';
    if (this.data.running) state = '计时中，保持节奏';
    else if (this.remaining < this.total) state = '已暂停，点开始继续';
    this.setData({ timeText: text, percent: percent, stateText: state, accent: mode.accent });
  },

  startTicker() {
    var self = this;
    this.timer = setInterval(function () {
      if (!self.data.running) return;
      self.remaining--;
      if (self.remaining <= 0) {
        self.remaining = 0;
        self.renderTime();
        self.finish();
        return;
      }
      self.renderTime();
    }, 1000);
  },

  finish() {
    var mode = this.modeOf(this.data.modeKey);
    this.setData({ running: false, percent: 100 });
    if (mode.key === 'focus') {
      var count = this.data.todayCount + 1;
      var minutes = this.data.todayMinutes + mode.min;
      this.saveStats(count, minutes);
      this.setData({ msg: '一个番茄完成，休息一下再继续。' });
      wx.showToast({ title: '番茄完成', icon: 'none' });
    } else {
      this.setData({ msg: '休息结束，回到专注状态吧。' });
      wx.showToast({ title: '休息结束', icon: 'none' });
    }
  },

  onMode(e) {
    var key = e.currentTarget.dataset.key;
    if (key === this.data.modeKey) return;
    var mode = this.modeOf(key);
    this.total = mode.min * 60;
    this.remaining = this.total;
    this.setData({ modeKey: key, running: false, msg: '', percent: 0 });
    this.renderTime();
  },

  onToggle() {
    if (this.data.running) {
      this.setData({ running: false });
      this.renderTime();
      return;
    }
    if (this.remaining <= 0) this.remaining = this.total;
    this.setData({ running: true, msg: '' });
    this.renderTime();
  },

  onReset() {
    this.remaining = this.total;
    this.setData({ running: false, msg: '', percent: 0 });
    this.renderTime();
  },

  onNext() {
    var next = this.data.modeKey === 'focus' ? 'short' : 'focus';
    this.onMode({ currentTarget: { dataset: { key: next } } });
  },

  onInput(e) {
    this.setData({ newTask: e.detail.value });
  },

  onAdd() {
    var text = (this.data.newTask || '').replace(/^\s+|\s+$/g, '');
    if (!text) {
      wx.showToast({ title: '先写点什么', icon: 'none' });
      return;
    }
    var tasks = this.data.tasks.slice();
    tasks.push({ id: '' + Date.now(), text: text, done: false });
    wx.setStorageSync('tasks', tasks);
    this.setData({
      tasks: tasks,
      doneCount: this.countDone(tasks),
      newTask: ''
    });
  },

  onToggleTask(e) {
    var id = e.currentTarget.dataset.id;
    var tasks = this.data.tasks.slice();
    for (var i = 0; i < tasks.length; i++) {
      if (tasks[i].id === id) {
        tasks[i] = { id: tasks[i].id, text: tasks[i].text, done: !tasks[i].done };
      }
    }
    wx.setStorageSync('tasks', tasks);
    this.setData({ tasks: tasks, doneCount: this.countDone(tasks) });
  },

  onDel(e) {
    var id = e.currentTarget.dataset.id;
    var tasks = this.data.tasks.filter(function (t) { return t.id !== id; });
    wx.setStorageSync('tasks', tasks);
    this.setData({ tasks: tasks, doneCount: this.countDone(tasks) });
  },

  onUnload() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }
});
