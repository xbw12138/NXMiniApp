var SIZE = 10;
var LEVELS = [
  { key: 'easy', label: '初级', mines: 10 },
  { key: 'mid', label: '中级', mines: 20 },
  { key: 'hard', label: '高级', mines: 32 }
];

Page({
  data: {
    diffs: LEVELS,
    level: 'easy',
    rows: [],
    mineLeft: 10,
    timeText: '00:00',
    flagMode: false,
    over: false,
    win: false,
    bestText: '--'
  },

  onLoad() {
    this.seconds = 0;
    this.timer = null;
    this.started = false;
    this.reset('easy');
  },

  levelOf(key) {
    for (var i = 0; i < LEVELS.length; i++) {
      if (LEVELS[i].key === key) return LEVELS[i];
    }
    return LEVELS[0];
  },

  reset(key) {
    this.stopTimer();
    this.seconds = 0;
    this.started = false;
    this.data.level = key;
    this.mineCount = this.levelOf(key).mines;
    this.grid = [];
    for (var i = 0; i < SIZE * SIZE; i++) {
      this.grid.push({ mine: false, open: false, flag: false, n: 0 });
    }
    this.flagTotal = 0;
    this.over = false;
    this.win = false;
    var best = wx.getStorageSync('best_' + key) || 0;
    this.setData({
      level: key,
      mineLeft: this.mineCount,
      timeText: '00:00',
      over: false,
      win: false,
      bestText: best ? this.fmt(best) : '--'
    });
    this.render();
  },

  fmt(s) {
    var m = Math.floor(s / 60);
    var sec = s % 60;
    return (m < 10 ? '0' + m : '' + m) + ':' + (sec < 10 ? '0' + sec : '' + sec);
  },

  seedMines(safe) {
    var pool = [];
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (Math.abs(Math.floor(i / SIZE) - Math.floor(safe / SIZE)) <= 1 &&
          Math.abs((i % SIZE) - (safe % SIZE)) <= 1) continue;
      pool.push(i);
    }
    for (var k = pool.length - 1; k > 0; k--) {
      var j = Math.floor(Math.random() * (k + 1));
      var t = pool[k]; pool[k] = pool[j]; pool[j] = t;
    }
    var n = Math.min(this.mineCount, pool.length);
    for (var m = 0; m < n; m++) this.grid[pool[m]].mine = true;
    for (var p = 0; p < SIZE * SIZE; p++) {
      this.grid[p].n = this.neighborMines(p);
    }
  },

  neighborMines(i) {
    var r = Math.floor(i / SIZE);
    var c = i % SIZE;
    var count = 0;
    for (var dr = -1; dr <= 1; dr++) {
      for (var dc = -1; dc <= 1; dc++) {
        if (dr === 0 && dc === 0) continue;
        var nr = r + dr;
        var nc = c + dc;
        if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE) continue;
        if (this.grid[nr * SIZE + nc].mine) count++;
      }
    }
    return count;
  },

  render() {
    var rows = [];
    for (var r = 0; r < SIZE; r++) {
      var cells = [];
      for (var c = 0; c < SIZE; c++) {
        var i = r * SIZE + c;
        var g = this.grid[i];
        var cls = 'cell';
        var label = '';
        if (g.open) {
          cls += ' cell-open';
          if (g.mine) {
            cls += this.boomIndex === i ? ' cell-boom' : ' cell-mine';
            label = '✳';
          } else {
            if (g.n > 0) {
              cls += ' n' + g.n;
              label = '' + g.n;
            }
          }
        } else if (g.flag) {
          cls += ' cell-flag';
          label = '⚑';
        }
        cells.push({ i: i, label: label, cls: cls });
      }
      rows.push({ r: r, cells: cells });
    }
    this.setData({ rows: rows });
  },

  onCell(e) {
    if (this.over) return;
    var i = e.currentTarget.dataset.i;
    var g = this.grid[i];
    if (g.open) return;

    if (this.data.flagMode) {
      if (g.flag) {
        g.flag = false;
        this.flagTotal--;
      } else {
        if (this.flagTotal >= this.mineCount) {
          wx.showToast({ title: '旗子用完了', icon: 'none' });
          return;
        }
        g.flag = true;
        this.flagTotal++;
      }
      this.setData({ mineLeft: this.mineCount - this.flagTotal });
      this.render();
      return;
    }

    if (g.flag) {
      wx.showToast({ title: '这里有旗子', icon: 'none' });
      return;
    }
    if (!this.started) {
      this.seedMines(i);
      this.started = true;
      this.startTimer();
    }
    if (g.mine) {
      this.boomIndex = i;
      this.lose();
      return;
    }
    this.reveal(i);
    this.render();
    this.checkWin();
  },

  reveal(start) {
    var stack = [start];
    while (stack.length) {
      var i = stack.pop();
      var g = this.grid[i];
      if (g.open || g.flag || g.mine) continue;
      g.open = true;
      g.n = this.neighborMines(i);
      if (g.n === 0) {
        var r = Math.floor(i / SIZE);
        var c = i % SIZE;
        for (var dr = -1; dr <= 1; dr++) {
          for (var dc = -1; dc <= 1; dc++) {
            var nr = r + dr;
            var nc = c + dc;
            if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE) continue;
            var ni = nr * SIZE + nc;
            if (!this.grid[ni].open && !this.grid[ni].flag) stack.push(ni);
          }
        }
      }
    }
  },

  checkWin() {
    var opened = 0;
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (this.grid[i].open && !this.grid[i].mine) opened++;
    }
    if (opened !== SIZE * SIZE - this.mineCount) return;
    this.over = true;
    this.win = true;
    this.stopTimer();
    for (var k = 0; k < SIZE * SIZE; k++) {
      if (this.grid[k].mine && !this.grid[k].flag) {
        this.grid[k].flag = true;
      }
    }
    this.flagTotal = this.mineCount;
    var best = wx.getStorageSync('best_' + this.data.level) || 0;
    if (!best || this.seconds < best) {
      wx.setStorageSync('best_' + this.data.level, this.seconds);
      best = this.seconds;
    }
    this.setData({
      over: true,
      win: true,
      mineLeft: 0,
      bestText: this.fmt(best)
    });
    this.render();
    wx.showToast({ title: '扫雷成功', icon: 'none' });
  },

  lose() {
    this.over = true;
    this.win = false;
    this.stopTimer();
    for (var i = 0; i < SIZE * SIZE; i++) {
      if (this.grid[i].mine) this.grid[i].open = true;
    }
    this.setData({ over: true, win: false });
    this.render();
    wx.showToast({ title: '踩到雷了', icon: 'none' });
  },

  onToggleMode() {
    this.setData({ flagMode: !this.data.flagMode });
  },

  onLevel(e) {
    var key = e.currentTarget.dataset.key;
    if (key === this.data.level) return;
    this.boomIndex = -1;
    this.reset(key);
  },

  onRestart() {
    this.boomIndex = -1;
    this.reset(this.data.level);
  },

  startTimer() {
    var self = this;
    this.stopTimer();
    this.timer = setInterval(function () {
      self.seconds++;
      self.setData({ timeText: self.fmt(self.seconds) });
    }, 1000);
  },

  stopTimer() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  },

  onUnload() {
    this.stopTimer();
  }
});
