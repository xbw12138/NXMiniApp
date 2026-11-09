var DIFFS = [
  { key: 'easy', label: '简单' },
  { key: 'medium', label: '中等' },
  { key: 'hard', label: '困难' }
];

function shuffle(a) {
  for (var i = a.length - 1; i > 0; i--) {
    var j = Math.floor(Math.random() * (i + 1));
    var t = a[i]; a[i] = a[j]; a[j] = t;
  }
  return a;
}

function canPlace(b, pos, v) {
  var r = Math.floor(pos / 9);
  var c = pos % 9;
  var i;
  for (i = 0; i < 9; i++) {
    if (b[r * 9 + i] === v) return false;
    if (b[i * 9 + c] === v) return false;
  }
  var br = Math.floor(r / 3) * 3;
  var bc = Math.floor(c / 3) * 3;
  for (i = 0; i < 3; i++) {
    for (var j = 0; j < 3; j++) {
      if (b[(br + i) * 9 + bc + j] === v) return false;
    }
  }
  return true;
}

function fillSolution(b, pos) {
  if (pos >= 81) return true;
  if (b[pos] !== 0) return fillSolution(b, pos + 1);
  var nums = shuffle([1, 2, 3, 4, 5, 6, 7, 8, 9]);
  for (var i = 0; i < 9; i++) {
    if (canPlace(b, pos, nums[i])) {
      b[pos] = nums[i];
      if (fillSolution(b, pos + 1)) return true;
      b[pos] = 0;
    }
  }
  return false;
}

function countSolutions(b, limit) {
  var pos = -1;
  for (var i = 0; i < 81; i++) {
    if (b[i] === 0) { pos = i; break; }
  }
  if (pos < 0) return 1;
  var total = 0;
  for (var v = 1; v <= 9; v++) {
    if (!canPlace(b, pos, v)) continue;
    b[pos] = v;
    total += countSolutions(b, limit);
    b[pos] = 0;
    if (total >= limit) return total;
  }
  return total;
}

function makePuzzle(removals) {
  var sol = [];
  for (var i = 0; i < 81; i++) sol.push(0);
  fillSolution(sol, 0);
  var puzzle = sol.slice();
  var order = [];
  for (var k = 0; k < 81; k++) order.push(k);
  shuffle(order);
  var removed = 0;
  for (var j = 0; j < order.length; j++) {
    if (removed >= removals) break;
    var idx = order[j];
    var old = puzzle[idx];
    puzzle[idx] = 0;
    if (countSolutions(puzzle.slice(), 2) !== 1) {
      puzzle[idx] = old;
    } else {
      removed++;
    }
  }
  return { puzzle: puzzle, solution: sol };
}

Page({
  data: {
    loading: true,
    diffs: DIFFS,
    difficulty: 'easy',
    rows: [],
    numRows: [
      { k: 0, nums: [1, 2, 3] },
      { k: 1, nums: [4, 5, 6] },
      { k: 2, nums: [7, 8, 9] }
    ],
    timeText: '00:00',
    mistakes: 0,
    hintsLeft: 3,
    done: false
  },

  onLoad() {
    var save = wx.getStorageSync('save');
    if (save && save.grid && save.grid.length === 81 && save.solution) {
      this.restore(save);
      return;
    }
    this.startNew('easy', 40);
  },

  restore(save) {
    this.grid = save.grid.slice();
    this.given = save.given.slice();
    this.solution = save.solution.slice();
    this.hintCells = save.hintCells || [];
    this.seconds = save.seconds || 0;
    this.done = !!save.done;
    this.setData({
      loading: false,
      difficulty: save.difficulty || 'easy',
      mistakes: save.mistakes || 0,
      hintsLeft: save.hintsLeft === undefined ? 3 : save.hintsLeft,
      done: this.done,
      timeText: this.fmt(this.seconds)
    });
    this.selected = -1;
    this.errCells = [];
    this.render();
    if (!this.done) this.startTimer();
  },

  startNew(diff, removals) {
    var self = this;
    this.stopTimer();
    this.setData({ loading: true, difficulty: diff, done: false });
    setTimeout(function () {
      var p = makePuzzle(removals);
      self.solution = p.solution;
      self.grid = p.puzzle.slice();
      self.given = [];
      for (var i = 0; i < 81; i++) self.given.push(p.puzzle[i] !== 0);
      self.hintCells = [];
      self.selected = -1;
      self.errCells = [];
      self.seconds = 0;
      self.done = false;
      self.setData({
        loading: false,
        mistakes: 0,
        hintsLeft: 3,
        done: false,
        timeText: '00:00'
      });
      self.render();
      self.persist();
      self.startTimer();
    }, 40);
  },

  onDifficulty(e) {
    var key = e.currentTarget.dataset.key;
    if (this.data.loading || key === this.data.difficulty) return;
    var self = this;
    wx.showModal({
      title: '切换难度',
      content: '将开始一盘新的棋局，当前进度会清空。',
      success: function (res) {
        if (res.confirm) {
          self.startNew(key, key === 'easy' ? 40 : (key === 'medium' ? 48 : 54));
        }
      }
    });
  },

  onNewGame() {
    var self = this;
    wx.showModal({
      title: '新棋局',
      content: '当前进度会清空，确定重新生成吗？',
      success: function (res) {
        if (res.confirm) {
          var d = self.data.difficulty;
          self.startNew(d, d === 'easy' ? 40 : (d === 'medium' ? 48 : 54));
        }
      }
    });
  },

  onCell(e) {
    if (this.done) return;
    var i = e.currentTarget.dataset.i;
    if (this.given[i]) {
      wx.showToast({ title: '这是题目数字', icon: 'none' });
      return;
    }
    this.selected = i;
    this.render();
  },

  onNumber(e) {
    if (this.done) return;
    var n = e.currentTarget.dataset.n;
    if (this.selected < 0) {
      wx.showToast({ title: '先选一个空格', icon: 'none' });
      return;
    }
    var i = this.selected;
    var wasEmpty = this.grid[i] === 0;
    this.grid[i] = n;
    this.computeErrors();
    if (this.errCells.indexOf(i) >= 0 && wasEmpty) {
      this.setData({ mistakes: this.data.mistakes + 1 });
    }
    this.render();
    this.persist();
    this.checkWin();
  },

  onErase() {
    if (this.done) return;
    if (this.selected < 0 || this.given[this.selected]) {
      wx.showToast({ title: '请先选一个可编辑格', icon: 'none' });
      return;
    }
    this.grid[this.selected] = 0;
    this.computeErrors();
    this.render();
    this.persist();
  },

  onHint() {
    if (this.done) return;
    if (this.data.hintsLeft <= 0) {
      wx.showToast({ title: '提示已用完', icon: 'none' });
      return;
    }
    var target = this.selected;
    if (target < 0 || this.given[target] || this.grid[target] === this.solution[target]) {
      target = -1;
      for (var i = 0; i < 81; i++) {
        if (this.grid[i] !== this.solution[i]) { target = i; break; }
      }
    }
    if (target < 0) {
      wx.showToast({ title: '已经填完了', icon: 'none' });
      return;
    }
    this.grid[target] = this.solution[target];
    this.given[target] = true;
    this.hintCells.push(target);
    this.selected = target;
    this.computeErrors();
    this.setData({ hintsLeft: this.data.hintsLeft - 1 });
    this.render();
    this.persist();
    this.checkWin();
  },

  computeErrors() {
    var errs = [];
    for (var i = 0; i < 81; i++) {
      var v = this.grid[i];
      if (v === 0 || this.given[i]) continue;
      var r = Math.floor(i / 9);
      var c = i % 9;
      var bad = false;
      for (var k = 0; k < 9; k++) {
        if (r * 9 + k !== i && this.grid[r * 9 + k] === v) bad = true;
        if (k * 9 + c !== i && this.grid[k * 9 + c] === v) bad = true;
      }
      var br = Math.floor(r / 3) * 3;
      var bc = Math.floor(c / 3) * 3;
      for (var a = 0; a < 3; a++) {
        for (var b = 0; b < 3; b++) {
          var j = (br + a) * 9 + bc + b;
          if (j !== i && this.grid[j] === v) bad = true;
        }
      }
      if (bad) errs.push(i);
    }
    this.errCells = errs;
  },

  checkWin() {
    for (var i = 0; i < 81; i++) {
      if (this.grid[i] !== this.solution[i]) return;
    }
    this.done = true;
    this.stopTimer();
    this.setData({ done: true });
    this.persist();
    var self = this;
    wx.showModal({
      title: '漂亮！数独完成',
      content: '用时 ' + this.fmt(this.seconds) + '，错误 ' + this.data.mistakes + ' 次。',
      confirmText: '再来一局',
      cancelText: '欣赏一下',
      success: function (res) {
        if (res.confirm) {
          var d = self.data.difficulty;
          self.startNew(d, d === 'easy' ? 40 : (d === 'medium' ? 48 : 54));
        }
      }
    });
  },

  render() {
    var rows = [];
    for (var r = 0; r < 9; r++) {
      var cells = [];
      for (var c = 0; c < 9; c++) {
        var i = r * 9 + c;
        var v = this.grid[i];
        var cls = 'cell b' + (((Math.floor(r / 3) + Math.floor(c / 3)) % 2) ? 1 : 0);
        if (this.given[i]) cls += ' given';
        if (this.hintCells.indexOf(i) >= 0) cls += ' hintv';
        if (i === this.selected) cls += ' sel';
        if (this.errCells.indexOf(i) >= 0) cls += ' err';
        cells.push({ i: i, label: v ? '' + v : '', cls: cls });
      }
      rows.push({ r: r, cells: cells });
    }
    this.setData({ rows: rows });
  },

  startTimer() {
    var self = this;
    this.stopTimer();
    this.timer = setInterval(function () {
      self.seconds++;
      self.setData({ timeText: self.fmt(self.seconds) });
      if (self.seconds % 10 === 0) self.persist();
    }, 1000);
  },

  stopTimer() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  },

  fmt(s) {
    var m = Math.floor(s / 60);
    var sec = s % 60;
    return (m < 10 ? '0' + m : '' + m) + ':' + (sec < 10 ? '0' + sec : '' + sec);
  },

  persist() {
    if (!this.grid) return;
    wx.setStorageSync('save', {
      grid: this.grid,
      given: this.given,
      solution: this.solution,
      hintCells: this.hintCells,
      seconds: this.seconds,
      mistakes: this.data.mistakes,
      hintsLeft: this.data.hintsLeft,
      difficulty: this.data.difficulty,
      done: this.done
    });
  },

  onUnload() {
    this.stopTimer();
  }
});
