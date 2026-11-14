// 俄罗斯方块 · 单 canvas 渲染版
// 操作：← → 移动，↑ / X 旋转，↓ 软降，A 直落，Y 暂存，START 暂停，BACK 重开，B 由容器返回

var W = 1280;
var H = 720;
var COLS = 10;
var ROWS = 20;
var CELL = 32;
var BOARD_X = 466;
var BOARD_Y = 48;
var BOARD_W = COLS * CELL;
var BOARD_H = ROWS * CELL;
var BEST_KEY = 'tet_best';
var CLEAR_DUR = 0.24;
var LOCK_DELAY = 0.36;
var MOVE_REPEAT = 0.08;
var SOFT_REPEAT = 0.05;

var KINDS = ['I', 'O', 'T', 'S', 'Z', 'J', 'L'];
// 下标与 SHAPES.color 对应：0 空，1~7 七种方块
var COLORS = ['#000000', '#22d3ee', '#facc15', '#a855f7', '#34d399', '#f87171', '#60a5fa', '#fb923c'];

var SHAPES = {
  I: { color: 1, grid: ['....', '####', '....', '....'] },
  O: { color: 2, grid: ['##', '##'] },
  T: { color: 3, grid: ['.#.', '###', '...'] },
  S: { color: 4, grid: ['.##', '##.', '...'] },
  Z: { color: 5, grid: ['##.', '.##', '...'] },
  J: { color: 6, grid: ['#..', '###', '...'] },
  L: { color: 7, grid: ['..#', '###', '...'] }
};

function toMatrix(rows) {
  var out = [];
  for (var y = 0; y < rows.length; y++) {
    var line = [];
    for (var x = 0; x < rows[y].length; x++) line.push(rows[y][x] === '#' ? 1 : 0);
    out.push(line);
  }
  return out;
}

for (var shapeKey in SHAPES) SHAPES[shapeKey].matrix = toMatrix(SHAPES[shapeKey].grid);

function emptyBoard() {
  var board = [];
  for (var i = 0; i < COLS * ROWS; i++) board.push(0);
  return board;
}

function inRect(r, x, y) {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

var BTN_HOLD = { x: 824, y: 120, w: 204, h: 104 };
var BTN_ROTATE = { x: 1052, y: 120, w: 204, h: 104 };
var BTN_LEFT = { x: 824, y: 240, w: 204, h: 104 };
var BTN_RIGHT = { x: 1052, y: 240, w: 204, h: 104 };
var BTN_SOFT = { x: 824, y: 360, w: 204, h: 104 };
var BTN_DROP = { x: 1052, y: 360, w: 204, h: 104 };
var BTN_PAUSE = { x: 824, y: 480, w: 204, h: 104 };
var BTN_RESTART = { x: 1052, y: 480, w: 204, h: 104 };

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.board = emptyBoard();
    this.piece = null;
    this.queue = [];
    this.held = null;
    this.holdUsed = false;
    this.score = 0;
    this.lines = 0;
    this.level = 1;
    this.state = 'play';
    this.clearing = null;
    this.prevPad = {};
    this.lastTs = 0;
    this.hDir = 0;
    this.hTimer = 0;
    this.softTimer = 0;
    this.touch = null;
    this.touchHold = null;
    this.dead = false;

    this.loadBest();
    this.resetGame();

    var loop = function (ts) {
      if (this.dead) return;
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  onUnload: function () {
    this.dead = true;
  },

  // ---------------- 存储 ----------------

  loadBest: function () {
    var v = 0;
    try {
      v = parseInt(wx.getStorageSync(BEST_KEY), 10) || 0;
    } catch (e) {
      v = 0;
    }
    this.best = v;
  },

  saveBest: function () {
    if (this.score > this.best) {
      this.best = this.score;
      try {
        wx.setStorageSync(BEST_KEY, this.best);
      } catch (e) {}
    }
  },

  // ---------------- 游戏流程 ----------------

  resetGame: function () {
    this.board = emptyBoard();
    this.queue = [];
    this.held = null;
    this.holdUsed = false;
    this.score = 0;
    this.lines = 0;
    this.level = 1;
    this.state = 'play';
    this.piece = null;
    this.clearing = null;
    this.dropTimer = 0;
    this.lockTimer = 0;
    this.lockResets = 0;
    this.softTimer = 0;
    this.hDir = 0;
    this.hTimer = 0;
    this.touch = null;
    this.touchHold = null;
    this.fillQueue();
    this.spawn();
  },

  fillQueue: function () {
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

  makePiece: function (kind) {
    var shape = SHAPES[kind];
    var matrix = shape.matrix;
    var copy = [];
    for (var y = 0; y < matrix.length; y++) copy.push(matrix[y].slice());
    return {
      kind: kind,
      color: shape.color,
      matrix: copy,
      x: Math.floor((COLS - matrix[0].length) / 2),
      y: 0
    };
  },

  spawn: function (kind) {
    var nextKind = kind || this.queue.shift();
    this.fillQueue();
    this.piece = this.makePiece(nextKind);
    this.holdUsed = false;
    this.dropTimer = 0;
    this.lockTimer = 0;
    this.lockResets = 0;
    if (this.collide(this.piece, 0, 0)) this.gameOver();
  },

  collide: function (piece, dx, dy) {
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

  ghostY: function () {
    var d = 0;
    while (!this.collide(this.piece, 0, d + 1)) d += 1;
    return this.piece.y + d;
  },

  refreshLock: function () {
    if (!this.collide(this.piece, 0, 1)) {
      this.lockTimer = 0;
      this.lockResets = 0;
      return;
    }
    if (this.lockResets < 10) {
      this.lockTimer = 0;
      this.lockResets += 1;
    }
  },

  // ---------------- 操作 ----------------

  doMove: function (dx) {
    if (this.state !== 'play' || !this.piece || this.clearing) return;
    if (this.collide(this.piece, dx, 0)) return;
    this.piece.x += dx;
    this.refreshLock();
  },

  doRotate: function () {
    if (this.state !== 'play' || !this.piece || this.clearing) return;
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
        this.refreshLock();
        return;
      }
    }
    this.piece.matrix = saved;
  },

  softDrop: function () {
    if (this.state !== 'play' || !this.piece || this.clearing) return;
    if (this.collide(this.piece, 0, 1)) return;
    this.piece.y += 1;
    this.score += 1;
    this.dropTimer = 0;
    this.lockTimer = 0;
    this.lockResets = 0;
  },

  hardDrop: function () {
    if (this.state !== 'play' || !this.piece || this.clearing) return;
    var dropped = 0;
    while (!this.collide(this.piece, 0, 1)) {
      this.piece.y += 1;
      dropped += 1;
    }
    this.score += dropped * 2;
    this.lockPiece();
  },

  doHold: function () {
    if (this.state !== 'play' || !this.piece || this.clearing || this.holdUsed) return;
    var current = this.piece.kind;
    var swap = this.held;
    this.held = current;
    this.piece = null;
    this.spawn(swap || undefined);
    // spawn 会重置 holdUsed，这里补回来，保证一个方块只能攒一次
    this.holdUsed = true;
  },

  togglePause: function () {
    if (this.state === 'over') return;
    if (this.state === 'paused') {
      this.state = 'play';
      this.lastTs = 0;
    } else {
      this.state = 'paused';
    }
  },

  lockPiece: function () {
    var piece = this.piece;
    if (!piece) return;
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
    this.lockTimer = 0;
    this.lockResets = 0;

    var full = [];
    for (var r = 0; r < ROWS; r++) {
      var ok = true;
      for (var c = 0; c < COLS; c++) {
        if (!this.board[r * COLS + c]) {
          ok = false;
          break;
        }
      }
      if (ok) full.push(r);
    }

    if (full.length) {
      this.clearing = { rows: full, t: CLEAR_DUR };
      this.addLines(full.length);
      wx.vibrateShort({ type: 'light' });
    } else {
      this.spawn();
    }
  },

  addLines: function (n) {
    var table = [0, 100, 300, 500, 800];
    var base = table[n] || 1200;
    this.score += base * this.level;
    this.lines += n;
    var level = Math.floor(this.lines / 10) + 1;
    if (level !== this.level) this.level = level;
    this.saveBest();
  },

  removeRows: function (rows) {
    var board = [];
    var i;
    for (i = 0; i < rows.length * COLS; i++) board.push(0);
    for (var y = 0; y < ROWS; y++) {
      var drop = false;
      for (i = 0; i < rows.length; i++) {
        if (rows[i] === y) drop = true;
      }
      if (drop) continue;
      for (var x = 0; x < COLS; x++) board.push(this.board[y * COLS + x]);
    }
    this.board = board;
  },

  gameOver: function () {
    this.state = 'over';
    this.piece = null;
    this.clearing = null;
    this.saveBest();
    wx.vibrateShort({ type: 'heavy' });
  },

  dropInterval: function () {
    var ms = 900 - (this.level - 1) * 80;
    if (ms < 120) ms = 120;
    return ms / 1000;
  },

  // ---------------- 输入 ----------------

  onTouchStart: function (e) {
    var x = e.detail.x;
    var y = e.detail.y;
    if (this.state === 'over') {
      this.resetGame();
      return;
    }
    this.touch = null;
    this.touchHold = null;
    var id = this.hitButton(x, y);
    if (id) {
      this.pressButton(id);
      if (id === 'left' || id === 'right') this.touchHold = { id: id, timer: 0.16 };
      else if (id === 'soft') this.touchHold = { id: id, timer: 0.12 };
      return;
    }
    if (this.state === 'paused') {
      this.togglePause();
      return;
    }
    this.touch = { x: x, y: y };
  },

  onTouchMove: function () {},

  onTouchEnd: function (e) {
    var t = this.touch;
    this.touch = null;
    this.touchHold = null;
    if (!t) return;
    var dx = e.detail.x - t.x;
    var dy = e.detail.y - t.y;
    // 下滑手势直落
    if (dy > 70 && dy > Math.abs(dx) * 1.2) this.hardDrop();
  },

  hitButton: function (x, y) {
    if (inRect(BTN_HOLD, x, y)) return 'hold';
    if (inRect(BTN_ROTATE, x, y)) return 'rotate';
    if (inRect(BTN_LEFT, x, y)) return 'left';
    if (inRect(BTN_RIGHT, x, y)) return 'right';
    if (inRect(BTN_SOFT, x, y)) return 'soft';
    if (inRect(BTN_DROP, x, y)) return 'drop';
    if (inRect(BTN_PAUSE, x, y)) return 'pause';
    if (inRect(BTN_RESTART, x, y)) return 'restart';
    return null;
  },

  pressButton: function (id) {
    if (id === 'hold') this.doHold();
    else if (id === 'rotate') this.doRotate();
    else if (id === 'left') this.doMove(-1);
    else if (id === 'right') this.doMove(1);
    else if (id === 'soft') this.softDrop();
    else if (id === 'drop') this.hardDrop();
    else if (id === 'pause') this.togglePause();
    else if (id === 'restart') this.resetGame();
  },

  handleInput: function (dt) {
    var pad = wx.getGamepadState ? wx.getGamepadState() : null;
    var b = (pad && pad.buttons) || {};
    var axes = (pad && pad.axes) || [0, 0, 0, 0];
    var lx = axes[0] || 0;
    var ly = axes[1] || 0;
    var cur = {
      left: !!b.left || lx < -0.55,
      right: !!b.right || lx > 0.55,
      down: !!b.down || ly > 0.55,
      rotate: !!b.up || !!b.x,
      drop: !!b.a,
      hold: !!b.y,
      pause: !!b.start,
      restart: !!b.back
    };
    var prev = this.prevPad || {};
    this.prevPad = cur;

    if (cur.pause && !prev.pause) this.togglePause();
    if (cur.restart && !prev.restart) this.resetGame();

    if (this.state === 'over') {
      if ((cur.drop && !prev.drop) || (cur.hold && !prev.hold)) this.resetGame();
      return;
    }

    if (cur.rotate && !prev.rotate) this.doRotate();
    if (cur.drop && !prev.drop) this.hardDrop();
    if (cur.hold && !prev.hold) this.doHold();

    var dir = 0;
    if (cur.left && !cur.right) dir = -1;
    else if (cur.right && !cur.left) dir = 1;
    if (dir !== this.hDir) {
      this.hDir = dir;
      if (dir !== 0) {
        this.doMove(dir);
        this.hTimer = 0.16;
      }
    } else if (dir !== 0) {
      this.hTimer -= dt;
      if (this.hTimer <= 0) {
        this.doMove(dir);
        this.hTimer = MOVE_REPEAT;
      }
    }

    if (cur.down) {
      this.softTimer -= dt;
      if (this.softTimer <= 0) {
        this.softDrop();
        this.softTimer = SOFT_REPEAT;
      }
    } else {
      this.softTimer = 0;
    }

    this.updateTouchHold(dt);
  },

  updateTouchHold: function (dt) {
    var th = this.touchHold;
    if (!th) return;
    th.timer -= dt;
    if (th.timer > 0) return;
    if (th.id === 'left') {
      this.doMove(-1);
      th.timer = MOVE_REPEAT;
    } else if (th.id === 'right') {
      this.doMove(1);
      th.timer = MOVE_REPEAT;
    } else if (th.id === 'soft') {
      this.softDrop();
      th.timer = SOFT_REPEAT;
    }
  },

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;
    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    this.handleInput(dt);

    if (this.state === 'play' && this.clearing) {
      this.clearing.t -= dt;
      if (this.clearing.t <= 0) {
        var rows = this.clearing.rows;
        this.clearing = null;
        this.removeRows(rows);
        this.spawn();
      }
      return;
    }

    if (this.state !== 'play' || !this.piece) return;

    this.dropTimer += dt;
    if (this.dropTimer >= this.dropInterval()) {
      this.dropTimer = 0;
      if (!this.collide(this.piece, 0, 1)) {
        this.piece.y += 1;
        this.lockTimer = 0;
        this.lockResets = 0;
      }
    }

    if (this.collide(this.piece, 0, 1)) {
      this.lockTimer += dt;
      if (this.lockTimer >= LOCK_DELAY) this.lockPiece();
    } else {
      this.lockTimer = 0;
    }
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setGlobalAlpha(1);
    ctx.setFillStyle('#0a0d12');
    ctx.fillRect(0, 0, W, H);

    ctx.setFillStyle('rgba(255,255,255,0.022)');
    var gx;
    var gy;
    for (gx = 0; gx < W; gx += 80) ctx.fillRect(gx, 0, 1, H);
    for (gy = 0; gy < H; gy += 80) ctx.fillRect(0, gy, W, 1);

    this.drawLeft(ctx);
    this.drawBoard(ctx);
    this.drawRight(ctx);
    this.drawBottom(ctx);

    if (this.state === 'paused') this.drawPause(ctx);
    else if (this.state === 'over') this.drawOver(ctx);

    ctx.draw();
  },

  drawLeft: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFontSize(54);
    ctx.setFillStyle('#f2f6ff');
    ctx.fillText('俄罗斯方块', 36, 88);
    ctx.setFontSize(20);
    ctx.setFillStyle('#6b7688');
    ctx.fillText('消行 · 加速 · 冲高分', 38, 120);

    this.drawStat(ctx, 36, 146, 190, 92, '得分', this.score);
    this.drawStat(ctx, 242, 146, 190, 92, '最高', this.best);
    this.drawStat(ctx, 36, 250, 190, 92, '等级', this.level);
    this.drawStat(ctx, 242, 250, 190, 92, '消行', this.lines);

    this.drawPreview(ctx, 36, 362, 190, 158, '暂存', this.held, this.holdUsed);
    this.drawPreview(ctx, 242, 362, 190, 158, '下一个', this.queue[0], false);

    ctx.setTextAlign('left');
    ctx.setFontSize(18);
    ctx.setFillStyle('#5d6779');
    ctx.fillText('7-bag 随机发牌', 38, 556);
    ctx.fillText('每消 10 行升 1 级', 38, 584);
    ctx.fillText('消 1/2/3/4 行', 38, 612);
    ctx.fillText('100/300/500/800 分', 38, 640);
  },

  drawStat: function (ctx, x, y, w, h, label, value) {
    ctx.setFillStyle('#141a24');
    ctx.fillRect(x, y, w, h);
    ctx.setStrokeStyle('#1f2836');
    ctx.setLineWidth(2);
    ctx.strokeRect(x, y, w, h);
    ctx.setTextAlign('left');
    ctx.setFontSize(20);
    ctx.setFillStyle('#6d798c');
    ctx.fillText(label, x + 18, y + 32);
    ctx.setFontSize(34);
    ctx.setFillStyle('#8fd0ff');
    ctx.fillText('' + value, x + 18, y + 76);
  },

  drawPreview: function (ctx, x, y, w, h, title, kind, dim) {
    ctx.setFillStyle('#10151d');
    ctx.fillRect(x, y, w, h);
    ctx.setStrokeStyle('#222c3b');
    ctx.setLineWidth(2);
    ctx.strokeRect(x, y, w, h);

    ctx.setTextAlign('left');
    ctx.setFontSize(18);
    ctx.setFillStyle('#6d798c');
    ctx.fillText(title, x + 16, y + 30);

    var cell = 26;
    var gridSize = cell * 4;
    var ox = x + Math.floor((w - gridSize) / 2);
    var oy = y + 48;

    if (!kind) {
      ctx.setTextAlign('center');
      ctx.setFontSize(20);
      ctx.setFillStyle('#3c4759');
      ctx.fillText('空', x + w / 2, oy + 64);
      ctx.setTextAlign('left');
      return;
    }

    var matrix = SHAPES[kind].matrix;
    var n = matrix.length;
    var offX = Math.floor((4 - n) / 2);
    var offY = Math.floor((4 - n) / 2);
    var color = COLORS[SHAPES[kind].color];

    if (dim) ctx.setGlobalAlpha(0.35);
    for (var my = 0; my < n; my++) {
      for (var mx = 0; mx < n; mx++) {
        if (!matrix[my][mx]) continue;
        var cx = ox + (offX + mx) * cell;
        var cy = oy + (offY + my) * cell;
        ctx.setFillStyle(color);
        ctx.fillRect(cx + 1, cy + 1, cell - 2, cell - 2);
        ctx.setFillStyle('rgba(255,255,255,0.25)');
        ctx.fillRect(cx + 3, cy + 3, cell - 6, 3);
      }
    }
    if (dim) ctx.setGlobalAlpha(1);

    if (dim) {
      ctx.setTextAlign('center');
      ctx.setFontSize(16);
      ctx.setFillStyle('#7d8698');
      ctx.fillText('本块已用', x + w / 2, y + h - 12);
      ctx.setTextAlign('left');
    }
  },

  drawBoard: function (ctx) {
    ctx.setFillStyle('rgba(0,0,0,0.45)');
    ctx.fillRect(BOARD_X + 6, BOARD_Y + 8, BOARD_W, BOARD_H);
    ctx.setFillStyle('#10151d');
    ctx.fillRect(BOARD_X, BOARD_Y, BOARD_W, BOARD_H);

    ctx.setFillStyle('rgba(255,255,255,0.055)');
    var c;
    var r;
    for (c = 1; c < COLS; c++) ctx.fillRect(BOARD_X + c * CELL, BOARD_Y, 1, BOARD_H);
    for (r = 1; r < ROWS; r++) ctx.fillRect(BOARD_X, BOARD_Y + r * CELL, BOARD_W, 1);

    var x;
    var y;
    for (y = 0; y < ROWS; y++) {
      for (x = 0; x < COLS; x++) {
        var v = this.board[y * COLS + x];
        if (v) this.drawCell(ctx, x, y, COLORS[v]);
      }
    }

    if (this.piece && !this.clearing) {
      // 幽灵落点
      var gy = this.ghostY();
      if (gy > this.piece.y) {
        var gm = this.piece.matrix;
        var gcol = COLORS[this.piece.color];
        ctx.setGlobalAlpha(0.3);
        ctx.setFillStyle(gcol);
        ctx.setStrokeStyle(gcol);
        ctx.setLineWidth(2);
        for (var gmy = 0; gmy < gm.length; gmy++) {
          for (var gmx = 0; gmx < gm[gmy].length; gmx++) {
            if (!gm[gmy][gmx]) continue;
            var gbx = this.piece.x + gmx;
            var gby = gy + gmy;
            if (gby < 0 || gby >= ROWS || gbx < 0 || gbx >= COLS) continue;
            var gx2 = BOARD_X + gbx * CELL;
            var gy2 = BOARD_Y + gby * CELL;
            ctx.fillRect(gx2 + 2, gy2 + 2, CELL - 4, CELL - 4);
            ctx.strokeRect(gx2 + 3, gy2 + 3, CELL - 6, CELL - 6);
          }
        }
        ctx.setGlobalAlpha(1);
      }

      // 当前方块
      var pm = this.piece.matrix;
      for (var py = 0; py < pm.length; py++) {
        for (var px = 0; px < pm[py].length; px++) {
          if (!pm[py][px]) continue;
          var pby = this.piece.y + py;
          var pbx = this.piece.x + px;
          if (pby < 0 || pby >= ROWS || pbx < 0 || pbx >= COLS) continue;
          this.drawCell(ctx, pbx, pby, COLORS[this.piece.color]);
        }
      }
    }

    // 消行闪白
    if (this.clearing) {
      var p = 1 - this.clearing.t / CLEAR_DUR;
      if (p < 0) p = 0;
      ctx.setGlobalAlpha(0.85 * (1 - p));
      ctx.setFillStyle('#ffffff');
      for (var i = 0; i < this.clearing.rows.length; i++) {
        var ry = this.clearing.rows[i];
        ctx.fillRect(BOARD_X, BOARD_Y + ry * CELL, BOARD_W, CELL);
      }
      ctx.setGlobalAlpha(1);
    }

    ctx.setStrokeStyle('#2a3646');
    ctx.setLineWidth(3);
    ctx.strokeRect(BOARD_X - 1, BOARD_Y - 1, BOARD_W + 2, BOARD_H + 2);
  },

  drawCell: function (ctx, bx, by, color) {
    var x = BOARD_X + bx * CELL;
    var y = BOARD_Y + by * CELL;
    ctx.setFillStyle(color);
    ctx.fillRect(x + 1, y + 1, CELL - 2, CELL - 2);
    ctx.setFillStyle('rgba(255,255,255,0.28)');
    ctx.fillRect(x + 3, y + 3, CELL - 6, 4);
    ctx.setFillStyle('rgba(0,0,0,0.3)');
    ctx.fillRect(x + 3, y + CELL - 7, CELL - 6, 4);
  },

  drawRight: function (ctx) {
    ctx.setTextAlign('left');
    ctx.setFontSize(20);
    ctx.setFillStyle('#6d798c');
    ctx.fillText('触摸按钮 / 手柄按键', 824, 104);

    this.drawButton(ctx, BTN_HOLD, '暂存', 'Y', false);
    this.drawButton(ctx, BTN_ROTATE, '旋转', 'X / ↑', true);
    this.drawButton(ctx, BTN_LEFT, '左移', '←', false);
    this.drawButton(ctx, BTN_RIGHT, '右移', '→', false);
    this.drawButton(ctx, BTN_SOFT, '软降', '↓', false);
    this.drawButton(ctx, BTN_DROP, '直落', 'A', true);
    this.drawButton(ctx, BTN_PAUSE, this.state === 'paused' ? '继续' : '暂停', 'START', false);
    this.drawButton(ctx, BTN_RESTART, '重新开始', 'BACK', false);

    ctx.setTextAlign('left');
    ctx.setFontSize(19);
    ctx.setFillStyle('#5d6779');
    ctx.fillText('方向键 / 左摇杆 移动方块', 824, 612);
    ctx.fillText('上 / X 旋转 · 下 软降 · A 直落', 824, 640);
    ctx.fillText('Y 暂存 · START 暂停 · BACK 重开', 824, 668);
    ctx.fillText('下滑屏幕可直接落底 · B 返回', 824, 696);
  },

  drawButton: function (ctx, r, label, key, main) {
    var fill = main ? '#1d4ed8' : '#161d28';
    var border = main ? '#2563eb' : '#26313f';
    var textColor = main ? '#ffffff' : '#cfdaea';
    var keyColor = main ? '#bcd2ff' : '#6d798c';

    ctx.setFillStyle(fill);
    ctx.fillRect(r.x, r.y, r.w, r.h);
    ctx.setStrokeStyle(border);
    ctx.setLineWidth(2);
    ctx.strokeRect(r.x, r.y, r.w, r.h);

    ctx.setTextAlign('center');
    ctx.setFontSize(30);
    ctx.setFillStyle(textColor);
    ctx.fillText(label, r.x + r.w / 2, r.y + 52);
    ctx.setFontSize(17);
    ctx.setFillStyle(keyColor);
    ctx.fillText(key, r.x + r.w / 2, r.y + 80);
    ctx.setTextAlign('left');
  },

  drawBottom: function (ctx) {
    ctx.setTextAlign('center');
    ctx.setFontSize(17);
    ctx.setFillStyle('#4d586a');
    ctx.fillText('← → 移动 · ↑/X 旋转 · ↓ 软降 · A 直落 · Y 暂存 · START 暂停', 626, 708);
    ctx.setTextAlign('left');
  },

  drawMask: function (ctx) {
    ctx.setFillStyle('rgba(6,8,14,0.78)');
    ctx.fillRect(0, 0, W, H);
  },

  drawPause: function (ctx) {
    this.drawMask(ctx);
    ctx.setFillStyle('#141a24');
    ctx.fillRect(390, 232, 500, 250);
    ctx.setStrokeStyle('#2a3646');
    ctx.setLineWidth(3);
    ctx.strokeRect(390, 232, 500, 250);

    ctx.setTextAlign('center');
    ctx.setFontSize(52);
    ctx.setFillStyle('#8fd0ff');
    ctx.fillText('已暂停', 640, 320);
    ctx.setFontSize(24);
    ctx.setFillStyle('#9aa4b5');
    ctx.fillText('按 START 或点击屏幕继续', 640, 376);
    ctx.setFontSize(20);
    ctx.setFillStyle('#6d798c');
    ctx.fillText('得分 ' + this.score + ' · 等级 ' + this.level, 640, 424);
    ctx.setTextAlign('left');
  },

  drawOver: function (ctx) {
    this.drawMask(ctx);
    ctx.setFillStyle('#1a1218');
    ctx.fillRect(370, 214, 540, 292);
    ctx.setStrokeStyle('#7f2b45');
    ctx.setLineWidth(3);
    ctx.strokeRect(370, 214, 540, 292);

    ctx.setTextAlign('center');
    ctx.setFontSize(52);
    ctx.setFillStyle('#ff7a8a');
    ctx.fillText('堆到顶了', 640, 300);
    ctx.setFontSize(26);
    ctx.setFillStyle('#dfe8f5');
    ctx.fillText('本局 ' + this.score + ' 分 · 最高 ' + this.best + ' 分', 640, 358);
    ctx.setFontSize(22);
    ctx.setFillStyle('#a08aa8');
    ctx.fillText('消行 ' + this.lines + ' · 等级 ' + this.level, 640, 398);
    ctx.setFontSize(24);
    ctx.setFillStyle('#ffd166');
    ctx.fillText('按 A / Y 或点击屏幕重开', 640, 456);
    ctx.setTextAlign('left');
  }
});
