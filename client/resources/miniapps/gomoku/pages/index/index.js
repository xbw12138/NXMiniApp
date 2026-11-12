var N = 15;

Page({
  data: {
    rows: [],
    turnText: '黑棋',
    turnBlack: true,
    mode: 'pve',
    moves: 0,
    over: false,
    resultText: '',
    resultSub: ''
  },

  onLoad() {
    this.reset();
  },

  reset() {
    this.board = [];
    for (var i = 0; i < N * N; i++) this.board.push(0);
    this.cur = 1;
    this.moves = 0;
    this.over = false;
    this.lastIndex = -1;
    this.winCells = [];
    this.history = [];
    this.aiTimer = null;
    this.setData({
      over: false,
      moves: 0,
      resultText: '',
      resultSub: '',
      mode: this.data.mode
    });
    this.updateTurn();
    this.render();
  },

  updateTurn() {
    this.setData({
      turnBlack: this.cur === 1,
      turnText: this.cur === 1 ? '黑棋' : '白棋'
    });
  },

  render() {
    var rows = [];
    for (var r = 0; r < N; r++) {
      var cells = [];
      for (var c = 0; c < N; c++) {
        var i = r * N + c;
        var v = this.board[i];
        var cls = 'cell';
        var label = '';
        if (v === 1) {
          cls += ' black';
          label = '●';
        } else if (v === 2) {
          cls += ' white';
          label = '●';
        }
        if (this.winCells.indexOf(i) >= 0) {
          cls += ' win';
        } else if (i === this.lastIndex) {
          cls += ' last';
        }
        cells.push({ i: i, label: label, cls: cls });
      }
      rows.push({ r: r, cells: cells });
    }
    this.setData({ rows: rows, moves: this.moves });
  },

  onCell(e) {
    if (this.over) return;
    if (this.data.mode === 'pve' && this.cur !== 1) {
      wx.showToast({ title: '等电脑落子', icon: 'none' });
      return;
    }
    var i = e.currentTarget.dataset.i;
    if (this.board[i] !== 0) return;
    this.place(i);
  },

  place(i) {
    this.board[i] = this.cur;
    this.moves++;
    this.history.push(i);
    this.lastIndex = i;
    var line = this.findWin(i, this.cur);
    if (line.length >= 5) {
      this.winCells = line;
      this.over = true;
      this.render();
      var name = this.cur === 1 ? '黑棋' : '白棋';
      this.setData({
        over: true,
        resultText: name + '五子连珠，胜利！',
        resultSub: '共 ' + this.moves + ' 手'
      });
      wx.showToast({ title: name + '获胜', icon: 'none' });
      return;
    }
    if (this.moves >= N * N) {
      this.over = true;
      this.render();
      this.setData({ over: true, resultText: '棋盘已满，平局', resultSub: '共 ' + this.moves + ' 手' });
      return;
    }
    this.cur = this.cur === 1 ? 2 : 1;
    this.updateTurn();
    this.render();
    if (this.data.mode === 'pve' && this.cur === 2 && !this.over) {
      this.scheduleAi();
    }
  },

  scheduleAi() {
    var self = this;
    if (this.aiTimer) clearTimeout(this.aiTimer);
    this.aiTimer = setTimeout(function () {
      if (self.over || self.cur !== 2) return;
      var mv = self.pickMove(2, 1);
      if (mv >= 0) self.place(mv);
    }, 260);
  },

  findWin(i, p) {
    var dirs = [[1, 0], [0, 1], [1, 1], [1, -1]];
    var r = Math.floor(i / N);
    var c = i % N;
    for (var d = 0; d < 4; d++) {
      var dx = dirs[d][0];
      var dy = dirs[d][1];
      var line = [i];
      var nr = r + dx;
      var nc = c + dy;
      while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
        line.push(nr * N + nc);
        nr += dx;
        nc += dy;
      }
      nr = r - dx;
      nc = c - dy;
      while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
        line.unshift(nr * N + nc);
        nr -= dx;
        nc -= dy;
      }
      if (line.length >= 5) return line;
    }
    return [];
  },

  isNear(i) {
    var r = Math.floor(i / N);
    var c = i % N;
    for (var dr = -2; dr <= 2; dr++) {
      for (var dc = -2; dc <= 2; dc++) {
        var nr = r + dr;
        var nc = c + dc;
        if (nr < 0 || nc < 0 || nr >= N || nc >= N) continue;
        if (this.board[nr * N + nc] !== 0) return true;
      }
    }
    return false;
  },

  patternScore(cnt, open) {
    if (open === 0) return 0;
    if (cnt >= 5) return 500000;
    if (cnt === 4) return open === 2 ? 50000 : 4200;
    if (cnt === 3) return open === 2 ? 3200 : 320;
    if (cnt === 2) return open === 2 ? 210 : 40;
    if (cnt === 1) return open === 2 ? 16 : 5;
    return 0;
  },

  dirScore(idx, p, dx, dy) {
    var r = Math.floor(idx / N);
    var c = idx % N;
    var cnt = 1;
    var open = 0;
    var nr = r + dx;
    var nc = c + dy;
    while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
      cnt++;
      nr += dx;
      nc += dy;
    }
    if (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === 0) open++;
    nr = r - dx;
    nc = c - dy;
    while (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === p) {
      cnt++;
      nr -= dx;
      nc -= dy;
    }
    if (nr >= 0 && nr < N && nc >= 0 && nc < N && this.board[nr * N + nc] === 0) open++;
    return this.patternScore(cnt, open);
  },

  pointScore(idx, p) {
    var dirs = [[1, 0], [0, 1], [1, 1], [1, -1]];
    var total = 0;
    for (var d = 0; d < 4; d++) {
      total += this.dirScore(idx, p, dirs[d][0], dirs[d][1]);
    }
    return total;
  },

  pickMove(me, opp) {
    var best = -1;
    var bestScore = -1;
    var any = false;
    for (var i = 0; i < N * N; i++) {
      if (this.board[i] !== 0) continue;
      if (!this.isNear(i)) continue;
      any = true;
      var s = this.pointScore(i, me) * 1.15 + this.pointScore(i, opp);
      var cr = Math.floor(i / N);
      var cc = i % N;
      s += (14 - Math.abs(cr - 7) - Math.abs(cc - 7)) * 2;
      if (s > bestScore) {
        bestScore = s;
        best = i;
      }
    }
    if (!any) {
      return Math.floor((N * N - 1) / 2);
    }
    return best;
  },

  onMode() {
    var next = this.data.mode === 'pve' ? 'pvp' : 'pve';
    this.setData({ mode: next });
    this.reset();
    wx.showToast({ title: next === 'pve' ? '人机对战' : '双人对战', icon: 'none' });
  },

  onUndo() {
    if (this.over) {
      wx.showToast({ title: '本局已结束', icon: 'none' });
      return;
    }
    if (this.moves === 0) {
      wx.showToast({ title: '还没有落子', icon: 'none' });
      return;
    }
    if (this.aiTimer) clearTimeout(this.aiTimer);
    var count = this.data.mode === 'pve' ? 2 : 1;
    while (count > 0 && this.history.length > 0) {
      var idx = this.history.pop();
      this.board[idx] = 0;
      this.moves--;
      count--;
    }
    this.lastIndex = this.history.length ? this.history[this.history.length - 1] : -1;
    this.cur = this.moves % 2 === 0 ? 1 : 2;
    this.updateTurn();
    this.render();
  },

  onUnload() {
    if (this.aiTimer) clearTimeout(this.aiTimer);
  }
});
