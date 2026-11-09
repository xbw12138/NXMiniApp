Page({
  data: {
    rows: [],
    score: 0,
    best: 0,
    undoCount: 0,
    over: false,
    won: false
  },

  onLoad() {
    var best = wx.getStorageSync('best') || 0;
    var save = wx.getStorageSync('save');
    this.undoStack = [];
    if (save && save.board && save.board.length === 16) {
      this.board = save.board.slice();
      this.won = !!save.won;
      this.setData({
        score: save.score || 0,
        best: Math.max(best, save.score || 0),
        won: this.won
      });
      this.render();
      this.checkOver();
    } else {
      this.board = this.empty();
      this.won = false;
      this.setData({ best: best });
      this.spawn();
      this.spawn();
      this.render();
    }
  },

  empty() {
    var a = [];
    for (var i = 0; i < 16; i++) a.push(0);
    return a;
  },

  clsOf(v) {
    if (v === 0) return '';
    if (v > 2048) return 'vbig';
    return 'v' + v;
  },

  render() {
    var rows = [];
    for (var r = 0; r < 4; r++) {
      var cells = [];
      for (var c = 0; c < 4; c++) {
        var i = r * 4 + c;
        var v = this.board[i];
        cells.push({ i: i, label: v ? '' + v : '', cls: this.clsOf(v) });
      }
      rows.push({ r: r, cells: cells });
    }
    this.setData({ rows: rows });
  },

  save() {
    wx.setStorageSync('save', {
      board: this.board,
      score: this.data.score,
      won: this.won
    });
    if (this.data.score > this.data.best) {
      wx.setStorageSync('best', this.data.score);
      this.setData({ best: this.data.score });
    }
  },

  spawn() {
    var zeros = [];
    for (var i = 0; i < 16; i++) {
      if (this.board[i] === 0) zeros.push(i);
    }
    if (zeros.length === 0) return;
    var idx = zeros[Math.floor(Math.random() * zeros.length)];
    this.board[idx] = Math.random() < 0.9 ? 2 : 4;
  },

  slideLine(line) {
    var vals = [];
    var i;
    for (i = 0; i < line.length; i++) {
      if (line[i]) vals.push(line[i]);
    }
    var out = [];
    var gained = 0;
    for (i = 0; i < vals.length; i++) {
      if (i + 1 < vals.length && vals[i] === vals[i + 1]) {
        var merged = vals[i] * 2;
        out.push(merged);
        gained += merged;
        i++;
      } else {
        out.push(vals[i]);
      }
    }
    while (out.length < line.length) out.push(0);
    return { line: out, gained: gained };
  },

  move(dir) {
    var old = this.board.slice();
    var next = this.empty();
    var gained = 0;
    var changed = false;
    var r, c, i;
    for (var n = 0; n < 4; n++) {
      var line = [];
      for (i = 0; i < 4; i++) {
        if (dir === 'left') line.push(this.board[n * 4 + i]);
        else if (dir === 'right') line.push(this.board[n * 4 + (3 - i)]);
        else if (dir === 'up') line.push(this.board[i * 4 + n]);
        else line.push(this.board[(3 - i) * 4 + n]);
      }
      var res = this.slideLine(line);
      gained += res.gained;
      var out = res.line;
      for (i = 0; i < 4; i++) {
        var v = dir === 'left' || dir === 'up' ? out[i] : out[3 - i];
        if (dir === 'left') next[n * 4 + i] = v;
        else if (dir === 'right') next[n * 4 + (3 - i)] = v;
        else if (dir === 'up') next[i * 4 + n] = v;
        else next[(3 - i) * 4 + n] = v;
      }
      for (i = 0; i < 4; i++) {
        if (line[i] !== out[i]) changed = true;
      }
    }
    if (!changed) {
      wx.showToast({ title: '这个方向动不了', icon: 'none' });
      return;
    }
    this.undoStack.push({ board: old, score: this.data.score, won: this.won });
    if (this.undoStack.length > 8) this.undoStack.shift();
    this.board = next;
    this.spawn();
    this.afterMove(gained);
  },

  afterMove(gained) {
    var score = this.data.score + gained;
    if (!this.won && this.board.indexOf(2048) >= 0) {
      this.won = true;
      wx.showToast({ title: '2048 达成！', icon: 'none' });
    }
    this.setData({
      score: score,
      best: Math.max(this.data.best, score),
      won: this.won,
      undoCount: this.undoStack.length,
      over: false
    });
    this.save();
    this.render();
    this.checkOver();
  },

  checkOver() {
    var hasZero = this.board.indexOf(0) >= 0;
    if (hasZero) {
      this.setData({ over: false });
      return;
    }
    for (var r = 0; r < 4; r++) {
      for (var c = 0; c < 4; c++) {
        var v = this.board[r * 4 + c];
        if (c < 3 && v === this.board[r * 4 + c + 1]) return;
        if (r < 3 && v === this.board[(r + 1) * 4 + c]) return;
      }
    }
    this.setData({ over: true });
    if (this.data.score > this.data.best) {
      wx.setStorageSync('best', this.data.score);
      this.setData({ best: this.data.score });
    }
  },

  onUp() { this.move('up'); },
  onDown() { this.move('down'); },
  onLeft() { this.move('left'); },
  onRight() { this.move('right'); },

  onUndo() {
    var snap = this.undoStack.pop();
    if (!snap) {
      wx.showToast({ title: '没有可撤销的步骤', icon: 'none' });
      return;
    }
    this.board = snap.board.slice();
    this.won = snap.won;
    this.setData({
      score: snap.score,
      won: snap.won,
      undoCount: this.undoStack.length,
      over: false
    });
    this.save();
    this.render();
    this.checkOver();
  },

  onNew() {
    var self = this;
    if (this.data.score === 0) {
      this.reset();
      return;
    }
    wx.showModal({
      title: '重新开始',
      content: '当前进度会清空，确定重开一局吗？',
      success: function (res) {
        if (res.confirm) self.reset();
      }
    });
  },

  reset() {
    this.undoStack = [];
    this.board = this.empty();
    this.won = false;
    this.setData({ score: 0, over: false, won: false, undoCount: 0 });
    this.spawn();
    this.spawn();
    this.render();
    this.save();
  },

  noop() {}
});
