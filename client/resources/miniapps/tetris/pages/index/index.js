var COLS = 10;
var ROWS = 20;
var BEST_KEY = 'tet_best';

var SHAPES = {
  I: { color: 1, grid: ['....', '####', '....', '....'] },
  O: { color: 2, grid: ['##', '##'] },
  T: { color: 3, grid: ['.#.', '###', '...'] },
  S: { color: 4, grid: ['.##', '##.', '...'] },
  Z: { color: 5, grid: ['##.', '.##', '...'] },
  J: { color: 6, grid: ['#..', '###', '...'] },
  L: { color: 7, grid: ['..#', '###', '...'] }
};
var KINDS = ['I', 'O', 'T', 'S', 'Z', 'J', 'L'];

function toMatrix(rows) {
  var out = [];
  for (var y = 0; y < rows.length; y++) {
    var line = [];
    for (var x = 0; x < rows[y].length; x++) line.push(rows[y][x] === '#' ? 1 : 0);
    out.push(line);
  }
  return out;
}

for (var k in SHAPES) SHAPES[k].matrix = toMatrix(SHAPES[k].grid);

function emptyBoard() {
  var board = [];
  for (var i = 0; i < COLS * ROWS; i++) board.push(0);
  return board;
}

Page({
  data: {
    rows: [],
    nextRows: [],
    holdRows: [],
    score: 0,
    best: 0,
    level: 1,
    lines: 0,
    over: false,
    paused: false,
    pauseLabel: '暂停'
  },

  onLoad() {
    this.board = emptyBoard();
    this.piece = null;
    this.queue = [];
    this.held = null;
    this.holdUsed = false;
    this.timer = null;
    this.over = false;
    this.paused = false;

    var best = 0;
    try {
      best = parseInt(wx.getStorageSync(BEST_KEY), 10) || 0;
    } catch (e) {}
    this.best = best;

    this.start();
  },

  onUnload() {
    this.clearTimer();
  },

  // ---------- 游戏流程 ----------

  start() {
    this.board = emptyBoard();
    this.queue = [];
    this.held = null;
    this.holdUsed = false;
    this.over = false;
    this.paused = false;
    this.score = 0;
    this.lines = 0;
    this.level = 1;
    this.fillQueue();
    this.spawn();
    this.setData({ best: this.best, pauseLabel: '暂停' });
    this.render();
    this.restartTimer();
  },

  fillQueue() {
    while (this.queue.length < 8) {
      // 7-bag：每一袋把 7 种方块各发一次，避免长时间不出某个方块
      var bag = KINDS.slice();
      for (var i = bag.length - 1; i > 0; i--) {
        var j = Math.floor(Math.random() * (i + 1));
        var tmp = bag[i];
        bag[i] = bag[j];
        bag[j] = tmp;
      }
      this.queue = this.queue.concat(bag);
    }
  },

  makePiece(kind) {
    var shape = SHAPES[kind];
    var matrix = shape.matrix;
    return {
      kind: kind,
      color: shape.color,
      matrix: matrix.map(function (row) { return row.slice(); }),
      x: Math.floor((COLS - matrix[0].length) / 2),
      y: 0
    };
  },

  spawn(kind) {
    var nextKind = kind || this.queue.shift();
    this.fillQueue();
    var piece = this.makePiece(nextKind);
    this.piece = piece;
    this.holdUsed = false;
    if (this.collide(piece, 0, 0)) {
      this.gameOver();
    }
  },

  collide(piece, dx, dy) {
    var matrix = piece.matrix;
    for (var y = 0; y < matrix.length; y++) {
      for (var x = 0; x < matrix[y].length; x++) {
        if (!matrix[y][x]) continue;
        var bx = piece.x + x + dx;
        var by = piece.y + y + dy;
        if (bx < 0 || bx >= COLS || by >= ROWS) return true;
        if (by < 0) continue;
        if (this.board[by * COLS + bx]) return true;
      }
    }
    return false;
  },

  move(dx, dy) {
    if (!this.piece || this.over || this.paused) return false;
    if (this.collide(this.piece, dx, dy)) return false;
    this.piece.x += dx;
    this.piece.y += dy;
    return true;
  },

  rotate() {
    if (!this.piece || this.over || this.paused) return;
    if (this.piece.kind === 'O') return;
    var old = this.piece.matrix;
    var n = old.length;
    var rotated = [];
    for (var y = 0; y < n; y++) {
      var line = [];
      for (var x = 0; x < n; x++) line.push(old[n - 1 - x][y]);
      rotated.push(line);
    }
    var saved = this.piece.matrix;
    this.piece.matrix = rotated;
    // 贴墙时往左/右/上挪一点再试，避免「转不动」
    var kicks = [0, -1, 1, -2, 2];
    for (var i = 0; i < kicks.length; i++) {
      if (!this.collide(this.piece, kicks[i], 0)) {
        this.piece.x += kicks[i];
        this.render();
        return;
      }
    }
    this.piece.matrix = saved;
  },

  lock() {
    var piece = this.piece;
    var matrix = piece.matrix;
    for (var y = 0; y < matrix.length; y++) {
      for (var x = 0; x < matrix[y].length; x++) {
        if (!matrix[y][x]) continue;
        var by = piece.y + y;
        var bx = piece.x + x;
        if (by < 0 || by >= ROWS || bx < 0 || bx >= COLS) continue;
        this.board[by * COLS + bx] = piece.color;
      }
    }
    this.piece = null;
    this.clearLines();
    if (!this.over) this.spawn();
    this.render();
  },

  clearLines() {
    var kept = [];
    var cleared = 0;
    for (var y = 0; y < ROWS; y++) {
      var full = true;
      for (var x = 0; x < COLS; x++) {
        if (!this.board[y * COLS + x]) { full = false; break; }
      }
      if (full) cleared++;
      else kept.push(y);
    }
    if (!cleared) return;

    var board = [];
    for (var i = 0; i < cleared * COLS; i++) board.push(0);
    for (var k = 0; k < kept.length; k++) {
      var from = kept[k] * COLS;
      for (var c = 0; c < COLS; c++) board.push(this.board[from + c]);
    }
    this.board = board;

    var table = [0, 100, 300, 500, 800];
    var base = table[cleared] || 1200;
    this.score += base * this.level;
    this.lines += cleared;
    var level = Math.floor(this.lines / 10) + 1;
    if (level !== this.level) {
      this.level = level;
      this.restartTimer();
    }
    this.saveBest();
  },

  gameOver() {
    this.over = true;
    this.piece = null;
    this.clearTimer();
    this.saveBest();
    this.setData({ over: true });
  },

  saveBest() {
    if (this.score > this.best) {
      this.best = this.score;
      try { wx.setStorageSync(BEST_KEY, this.best); } catch (e) {}
    }
    this.setData({ best: this.best });
  },

  // ---------- 计时 ----------

  intervalMs() {
    var ms = 900 - (this.level - 1) * 80;
    return ms < 120 ? 120 : ms;
  },

  restartTimer() {
    this.clearTimer();
    if (this.over || this.paused) return;
    var self = this;
    this.timer = setInterval(function () {
      self.tick();
    }, this.intervalMs());
  },

  clearTimer() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  },

  tick() {
    if (!this.piece || this.over || this.paused) return;
    if (this.collide(this.piece, 0, 1)) {
      this.lock();
      return;
    }
    this.piece.y += 1;
    this.render();
  },

  // ---------- 渲染 ----------

  render() {
    var board = this.board;
    var piece = this.piece;
    var grid = [];
    for (var i = 0; i < COLS * ROWS; i++) grid.push(board[i]);

    if (piece) {
      var matrix = piece.matrix;
      for (var y = 0; y < matrix.length; y++) {
        for (var x = 0; x < matrix[y].length; x++) {
          if (!matrix[y][x]) continue;
          var by = piece.y + y;
          var bx = piece.x + x;
          if (by < 0 || by >= ROWS || bx < 0 || bx >= COLS) continue;
          grid[by * COLS + bx] = piece.color;
        }
      }
    }

    var rows = [];
    for (var r = 0; r < ROWS; r++) {
      var line = [];
      for (var c = 0; c < COLS; c++) {
        var v = grid[r * COLS + c];
        line.push(v ? 'c' + v : '');
      }
      rows.push(line);
    }

    this.setData({
      rows: rows,
      nextRows: this.preview(this.queue[0]),
      holdRows: this.preview(this.held),
      score: this.score,
      level: this.level,
      lines: this.lines
    });
  },

  preview(kind) {
    var out = [];
    for (var y = 0; y < 4; y++) {
      var line = [];
      for (var x = 0; x < 4; x++) line.push('');
      out.push(line);
    }
    if (!kind) return out;
    var matrix = SHAPES[kind].matrix;
    var size = matrix.length;
    var offX = Math.floor((4 - size) / 2);
    var offY = Math.floor((4 - size) / 2);
    for (var my = 0; my < size; my++) {
      for (var mx = 0; mx < size; mx++) {
        if (!matrix[my][mx]) continue;
        out[offY + my][offX + mx] = 'c' + SHAPES[kind].color;
      }
    }
    return out;
  },

  // ---------- 交互 ----------

  onLeft() {
    if (this.move(-1, 0)) this.render();
  },

  onRight() {
    if (this.move(1, 0)) this.render();
  },

  onDown() {
    if (this.over || this.paused) return;
    if (this.move(0, 1)) {
      this.score += 1;
      this.render();
    }
  },

  onRotate() {
    this.rotate();
  },

  onDrop() {
    if (!this.piece || this.over || this.paused) return;
    var dropped = 0;
    while (!this.collide(this.piece, 0, 1)) {
      this.piece.y += 1;
      dropped++;
    }
    this.score += dropped * 2;
    this.lock();
  },

  onHold() {
    if (!this.piece || this.over || this.paused || this.holdUsed) return;
    var current = this.piece.kind;
    var swap = this.held;
    this.held = current;
    this.piece = null;
    this.spawn(swap || undefined);
    // spawn 会重置 holdUsed，这里补回来，保证一个方块只能攒一次
    this.holdUsed = true;
    this.render();
  },

  onPause() {
    if (this.over) return;
    this.paused = !this.paused;
    if (this.paused) this.clearTimer();
    else this.restartTimer();
    this.setData({ paused: this.paused, pauseLabel: this.paused ? '继续' : '暂停' });
  },

  onRestart() {
    this.clearTimer();
    this.start();
    this.setData({ over: false, paused: false });
  }
});
