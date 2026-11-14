// 中国象棋：单 canvas 渲染 + requestAnimationFrame
// 操作：触摸点选 / 手柄方向键移动光标 + A 选子落子
// X 切换模式与难度，Y 悔棋，START 重新开局

var W = 1280;
var H = 720;

var TEXT = {
  rK: '帅', rA: '仕', rB: '相', rN: '马', rR: '车', rC: '炮', rP: '兵',
  bK: '将', bA: '士', bB: '象', bN: '马', bR: '车', bC: '炮', bP: '卒'
};
var VALUE = { K: 10000, R: 900, C: 450, N: 400, B: 200, A: 200, P: 100 };
var LEVELS = {
  1: { depth: 1, limit: 2500, noise: 60 },
  2: { depth: 2, limit: 7000, noise: 8 },
  3: { depth: 3, limit: 12000, noise: 0 }
};
var DIRS = [[0, -1], [0, 1], [-1, 0], [1, 0]];
var DIAG = [[-1, -1], [-1, 1], [1, -1], [1, 1]];
var HORSE = [
  [-1, -2, 0, -1], [1, -2, 0, -1], [-1, 2, 0, 1], [1, 2, 0, 1],
  [-2, -1, -1, 0], [2, -1, 1, 0], [-2, 1, -1, 0], [2, 1, 1, 0]
];

// 棋盘几何
var BX = 36, BY = 48, BW = 592, BH = 626;
var GX = 84, GY = 82, CELL = 62, PR = 26;

function initialBoard() {
  var board = [];
  for (var i = 0; i < 90; i++) board.push('');
  var back = ['R', 'N', 'B', 'A', 'K', 'A', 'B', 'N', 'R'];
  for (var f = 0; f < 9; f++) {
    board[0 * 9 + f] = 'b' + back[f];
    board[9 * 9 + f] = 'r' + back[f];
  }
  board[2 * 9 + 1] = 'bC';
  board[2 * 9 + 7] = 'bC';
  board[7 * 9 + 1] = 'rC';
  board[7 * 9 + 7] = 'rC';
  for (var p = 0; p < 9; p += 2) {
    board[3 * 9 + p] = 'bP';
    board[6 * 9 + p] = 'rP';
  }
  return board;
}

function other(side) { return side === 'r' ? 'b' : 'r'; }

function inPalace(side, f, r) {
  if (f < 3 || f > 5) return false;
  return side === 'r' ? (r >= 7 && r <= 9) : (r >= 0 && r <= 2);
}

// 生成伪合法着法（不检查走后是否自陷被将军）
function genMoves(board, idx) {
  var p = board[idx];
  if (!p) return [];
  var side = p[0];
  var type = p[1];
  var f = idx % 9;
  var r = (idx - f) / 9;
  var out = [];
  var i, nf, nr, t;

  if (type === 'K') {
    for (i = 0; i < 4; i++) {
      nf = f + DIRS[i][0];
      nr = r + DIRS[i][1];
      if (!inPalace(side, nf, nr)) continue;
      t = board[nr * 9 + nf];
      if (!t || t[0] !== side) out.push(nr * 9 + nf);
    }
  } else if (type === 'A') {
    for (i = 0; i < 4; i++) {
      nf = f + DIAG[i][0];
      nr = r + DIAG[i][1];
      if (!inPalace(side, nf, nr)) continue;
      t = board[nr * 9 + nf];
      if (!t || t[0] !== side) out.push(nr * 9 + nf);
    }
  } else if (type === 'B') {
    for (i = 0; i < 4; i++) {
      nf = f + DIAG[i][0] * 2;
      nr = r + DIAG[i][1] * 2;
      if (nf < 0 || nf > 8 || nr < 0 || nr > 9) continue;
      if (side === 'r' && nr < 5) continue;
      if (side === 'b' && nr > 4) continue;
      if (board[(r + DIAG[i][1]) * 9 + (f + DIAG[i][0])]) continue;
      t = board[nr * 9 + nf];
      if (!t || t[0] !== side) out.push(nr * 9 + nf);
    }
  } else if (type === 'N') {
    for (i = 0; i < 8; i++) {
      nf = f + HORSE[i][0];
      nr = r + HORSE[i][1];
      if (nf < 0 || nf > 8 || nr < 0 || nr > 9) continue;
      if (board[(r + HORSE[i][3]) * 9 + (f + HORSE[i][2])]) continue;
      t = board[nr * 9 + nf];
      if (!t || t[0] !== side) out.push(nr * 9 + nf);
    }
  } else if (type === 'R' || type === 'C') {
    for (i = 0; i < 4; i++) {
      nf = f + DIRS[i][0];
      nr = r + DIRS[i][1];
      var screened = false;
      while (nf >= 0 && nf < 9 && nr >= 0 && nr < 10) {
        t = board[nr * 9 + nf];
        if (type === 'R') {
          if (!t) out.push(nr * 9 + nf);
          else {
            if (t[0] !== side) out.push(nr * 9 + nf);
            break;
          }
        } else if (!screened) {
          if (!t) out.push(nr * 9 + nf);
          else screened = true;
        } else if (t) {
          if (t[0] !== side) out.push(nr * 9 + nf);
          break;
        }
        nf += DIRS[i][0];
        nr += DIRS[i][1];
      }
    }
  } else if (type === 'P') {
    nr = r + (side === 'r' ? -1 : 1);
    if (nr >= 0 && nr < 10) {
      t = board[nr * 9 + f];
      if (!t || t[0] !== side) out.push(nr * 9 + f);
    }
    var crossed = side === 'r' ? r <= 4 : r >= 5;
    if (crossed) {
      for (i = -1; i <= 1; i += 2) {
        nf = f + i;
        if (nf < 0 || nf > 8) continue;
        t = board[r * 9 + nf];
        if (!t || t[0] !== side) out.push(r * 9 + nf);
      }
    }
  }
  return out;
}

function findKing(board, side) {
  var target = side + 'K';
  for (var i = 0; i < 90; i++) if (board[i] === target) return i;
  return -1;
}

// 判断 side 是否正被将军（含飞将）
function inCheck(board, side) {
  var k = findKing(board, side);
  if (k < 0) return true;
  var kf = k % 9;
  var kr = (k - kf) / 9;
  var opp = other(side);
  var d, nf, nr, t, i;

  for (d = 0; d < 4; d++) {
    nf = kf + DIRS[d][0];
    nr = kr + DIRS[d][1];
    var screens = 0;
    while (nf >= 0 && nf < 9 && nr >= 0 && nr < 10) {
      t = board[nr * 9 + nf];
      if (t) {
        if (screens === 0) {
          if (t[0] === opp && (t[1] === 'R' || (t[1] === 'K' && DIRS[d][0] === 0))) return true;
          screens = 1;
        } else {
          if (t[0] === opp && t[1] === 'C') return true;
          break;
        }
      }
      nf += DIRS[d][0];
      nr += DIRS[d][1];
    }
  }

  for (i = 0; i < 8; i++) {
    var hf = kf - HORSE[i][0];
    var hr = kr - HORSE[i][1];
    if (hf < 0 || hf > 8 || hr < 0 || hr > 9) continue;
    if (board[hr * 9 + hf] !== opp + 'N') continue;
    if (board[(hr + HORSE[i][3]) * 9 + (hf + HORSE[i][2])]) continue;
    return true;
  }

  var pawnRow = side === 'r' ? kr - 1 : kr + 1;
  if (pawnRow >= 0 && pawnRow < 10 && board[pawnRow * 9 + kf] === opp + 'P') return true;
  var crossedOK = side === 'r' ? kr >= 5 : kr <= 4;
  if (crossedOK) {
    for (i = -1; i <= 1; i += 2) {
      var pf = kf + i;
      if (pf < 0 || pf > 8) continue;
      if (board[kr * 9 + pf] === opp + 'P') return true;
    }
  }
  return false;
}

function applyMove(board, from, to) {
  var next = board.slice();
  next[to] = next[from];
  next[from] = '';
  return next;
}

// 过滤掉走后自己被将军的着法
function legalMoves(board, side) {
  var out = [];
  for (var idx = 0; idx < 90; idx++) {
    var p = board[idx];
    if (!p || p[0] !== side) continue;
    var targets = genMoves(board, idx);
    for (var i = 0; i < targets.length; i++) {
      var next = applyMove(board, idx, targets[i]);
      if (!inCheck(next, side)) out.push({ from: idx, to: targets[i], cap: board[targets[i]] });
    }
  }
  return out;
}

function material(board) {
  var score = 0;
  for (var i = 0; i < 90; i++) {
    var p = board[i];
    if (!p) continue;
    var v = VALUE[p[1]] || 0;
    if (p[1] === 'P') {
      var r = Math.floor(i / 9);
      if ((p[0] === 'r' && r <= 4) || (p[0] === 'b' && r >= 5)) v += 60;
    }
    score += p[0] === 'r' ? v : -v;
  }
  return score;
}

function evalFor(board, side) {
  var m = material(board);
  return side === 'r' ? m : -m;
}

function negamax(board, side, depth, alpha, beta, ctx) {
  ctx.nodes++;
  if (ctx.nodes > ctx.limit) return evalFor(board, side);
  if (depth <= 0) return evalFor(board, side);

  var moves = legalMoves(board, side);
  if (!moves.length) return -99000 + (10 - depth);
  moves.sort(function (a, b) {
    return (b.cap ? VALUE[b.cap[1]] : 0) - (a.cap ? VALUE[a.cap[1]] : 0);
  });

  var best = -Infinity;
  for (var i = 0; i < moves.length; i++) {
    var next = applyMove(board, moves[i].from, moves[i].to);
    var score = -negamax(next, other(side), depth - 1, -beta, -alpha, ctx);
    if (score > best) best = score;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;
  }
  return best;
}

var BUTTONS = [
  { id: 'ai', x: 660, y: 192, w: 284, h: 64 },
  { id: 'pvp', x: 956, y: 192, w: 284, h: 64 },
  { id: 'l1', x: 660, y: 272, w: 182, h: 58 },
  { id: 'l2', x: 855, y: 272, w: 182, h: 58 },
  { id: 'l3', x: 1050, y: 272, w: 190, h: 58 },
  { id: 'undo', x: 660, y: 350, w: 284, h: 70 },
  { id: 'restart', x: 956, y: 350, w: 284, h: 70 }
];

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.dead = false;
    this.lastTs = 0;
    this.pulse = 0;
    this.mode = 'ai';
    this.level = 2;
    this.prev = {};
    this.dir = '';
    this.dirTimer = 0;
    this.touch = null;
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

  // ---------------- 状态 ----------------

  resetGame: function () {
    this.board = initialBoard();
    this.turn = 'r';
    this.selected = -1;
    this.targets = {};
    this.history = [];
    this.over = false;
    this.resultText = '';
    this.thinking = false;
    this.thinkTimer = 0;
    this.anim = null;
    this.lastFrom = -1;
    this.lastTo = -1;
    this.checkFlash = 0;
    this.inCheckNow = false;
    this.cursorF = 4;
    this.cursorR = 6;
  },

  restart: function () {
    this.resetGame();
    wx.vibrateShort({ type: 'light' });
  },

  cycleMode: function () {
    if (this.mode === 'pvp') {
      this.mode = 'ai';
      this.level = 1;
    } else if (this.level < 3) {
      this.level++;
    } else {
      this.mode = 'pvp';
    }
    this.resetGame();
    wx.vibrateShort({ type: 'light' });
  },

  setMode: function (m) {
    if (this.mode === m) return;
    this.mode = m;
    this.resetGame();
    wx.vibrateShort({ type: 'light' });
  },

  setLevel: function (n) {
    this.level = n;
    if (this.mode === 'pvp') {
      this.mode = 'ai';
      this.resetGame();
    }
  },

  selectPiece: function (idx) {
    var moves = genMoves(this.board, idx);
    var allowed = {};
    for (var i = 0; i < moves.length; i++) {
      var next = applyMove(this.board, idx, moves[i]);
      if (!inCheck(next, this.turn)) allowed[moves[i]] = true;
    }
    this.selected = idx;
    this.targets = allowed;
  },

  tapCell: function (idx) {
    if (this.anim || this.thinking) return;
    if (this.mode === 'ai' && this.turn !== 'r') return;

    if (this.selected >= 0 && this.targets[idx]) {
      this.doMove(this.selected, idx);
      return;
    }
    var piece = this.board[idx];
    if (piece && piece[0] === this.turn) {
      this.selectPiece(idx);
      return;
    }
    if (this.selected >= 0) {
      this.selected = -1;
      this.targets = {};
    }
  },

  doMove: function (from, to) {
    if (this.anim) return;
    var piece = this.board[from];
    if (!piece) return;
    this.history.push({
      board: this.board.slice(),
      turn: this.turn,
      lastFrom: this.lastFrom,
      lastTo: this.lastTo
    });
    this.anim = {
      from: from,
      to: to,
      piece: piece,
      cap: this.board[to],
      t: 0,
      dur: 0.2
    };
    this.board = applyMove(this.board, from, to);
    this.lastFrom = from;
    this.lastTo = to;
    this.selected = -1;
    this.targets = {};
    this.turn = other(this.turn);
    this.inCheckNow = false;
    this.checkFlash = 0;
    wx.vibrateShort({ type: this.anim.cap ? 'heavy' : 'light' });
  },

  afterAnim: function () {
    var chk = inCheck(this.board, this.turn);
    this.inCheckNow = chk;
    if (chk) {
      this.checkFlash = 1.8;
      wx.vibrateShort({ type: 'heavy' });
    }
    this.checkEnd();
    if (this.over) return;
    if (this.mode === 'ai' && this.turn === 'b') {
      this.thinking = true;
      this.thinkTimer = 0.2;
    }
  },

  checkEnd: function () {
    var moves = legalMoves(this.board, this.turn);
    if (moves.length) return false;
    this.over = true;
    this.thinking = false;
    this.resultText = (this.turn === 'r' ? '黑方' : '红方') + ' 胜';
    wx.vibrateShort({ type: 'heavy' });
    return true;
  },

  aiStep: function (dt) {
    if (!this.thinking || this.over) return;
    this.thinkTimer -= dt;
    if (this.thinkTimer > 0) return;
    this.thinking = false;

    var cfg = LEVELS[this.level] || LEVELS[2];
    var ctx = { nodes: 0, limit: cfg.limit };
    var moves = legalMoves(this.board, 'b');
    if (!moves.length) {
      this.checkEnd();
      return;
    }
    moves.sort(function (a, b) {
      return (b.cap ? VALUE[b.cap[1]] : 0) - (a.cap ? VALUE[a.cap[1]] : 0);
    });

    var best = null;
    var bestScore = -Infinity;
    for (var i = 0; i < moves.length; i++) {
      var next = applyMove(this.board, moves[i].from, moves[i].to);
      var score = -negamax(next, 'r', cfg.depth - 1, -Infinity, Infinity, ctx);
      score += (Math.random() - 0.5) * cfg.noise;
      if (score > bestScore) {
        bestScore = score;
        best = moves[i];
      }
    }
    if (!best) {
      this.checkEnd();
      return;
    }
    this.doMove(best.from, best.to);
  },

  undo: function () {
    if (this.thinking || this.anim) return;
    if (!this.history.length) {
      wx.showToast({ title: '没有可悔的棋', icon: 'none' });
      return;
    }
    var step = this.mode === 'ai' ? 2 : 1;
    while (step > 0 && this.history.length) {
      var last = this.history.pop();
      this.board = last.board;
      this.turn = last.turn;
      this.lastFrom = last.lastFrom;
      this.lastTo = last.lastTo;
      step--;
    }
    this.selected = -1;
    this.targets = {};
    this.over = false;
    this.resultText = '';
    this.thinking = false;
    this.thinkTimer = 0;
    this.checkFlash = 0;
    this.inCheckNow = inCheck(this.board, this.turn);
    wx.vibrateShort({ type: 'light' });
  },

  // ---------------- 输入 ----------------

  readPad: function (dt) {
    var pad = wx.getGamepadState() || {};
    var b = pad.buttons || {};
    var axes = pad.axes || [0, 0];
    var prev = this.prev || {};

    var eA = b.a && !prev.a;
    var eX = b.x && !prev.x;
    var eY = b.y && !prev.y;
    var eS = b.start && !prev.start;
    this.prev = { a: !!b.a, x: !!b.x, y: !!b.y, start: !!b.start };

    if (eA) {
      if (this.over) this.restart();
      else this.pressA();
    }
    if (eX) this.cycleMode();
    if (eY) this.undo();
    if (eS) this.restart();

    var d = '';
    if (b.up || axes[1] < -0.6) d = 'up';
    else if (b.down || axes[1] > 0.6) d = 'down';
    else if (b.left || axes[0] < -0.6) d = 'left';
    else if (b.right || axes[0] > 0.6) d = 'right';

    if (d) {
      if (d !== this.dir) {
        this.dir = d;
        this.moveCursor(d);
        this.dirTimer = 0.34;
      } else {
        this.dirTimer -= dt;
        if (this.dirTimer <= 0) {
          this.moveCursor(d);
          this.dirTimer = 0.14;
        }
      }
    } else {
      this.dir = '';
    }
  },

  moveCursor: function (d) {
    if (d === 'up') this.cursorR = Math.max(0, this.cursorR - 1);
    else if (d === 'down') this.cursorR = Math.min(9, this.cursorR + 1);
    else if (d === 'left') this.cursorF = Math.max(0, this.cursorF - 1);
    else if (d === 'right') this.cursorF = Math.min(8, this.cursorF + 1);
  },

  pressA: function () {
    if (this.anim || this.thinking) return;
    if (this.mode === 'ai' && this.turn !== 'r') return;
    this.tapCell(this.cursorR * 9 + this.cursorF);
  },

  onTouchStart: function (e) {
    var p = this.pointOf(e);
    if (!p) return;
    this.touch = { x: p.x, y: p.y, sx: p.x, sy: p.y, moved: false };
  },

  onTouchMove: function (e) {
    if (!this.touch) return;
    var p = this.pointOf(e);
    if (!p) return;
    this.touch.x = p.x;
    this.touch.y = p.y;
    var dx = p.x - this.touch.sx;
    var dy = p.y - this.touch.sy;
    if (dx * dx + dy * dy > 400) this.touch.moved = true;
  },

  onTouchEnd: function (e) {
    if (!this.touch) return;
    var t = this.touch;
    var p = this.pointOf(e) || t;
    this.touch = null;
    if (!t.moved && Math.abs(p.x - t.sx) < 24 && Math.abs(p.y - t.sy) < 24) {
      this.handleTap(p.x, p.y);
    }
  },

  pointOf: function (e) {
    if (!e) return null;
    var d = e.detail || e;
    var x = d.x;
    var y = d.y;
    if (x === undefined || y === undefined) {
      var list = e.changedTouches || e.touches;
      if (!list || !list.length) return null;
      x = list[0].clientX;
      y = list[0].clientY;
    }
    if (typeof x !== 'number' || typeof y !== 'number') return null;
    return { x: x, y: y };
  },

  handleTap: function (x, y) {
    if (this.over) {
      this.restart();
      return;
    }
    var i, b;
    for (i = 0; i < BUTTONS.length; i++) {
      b = BUTTONS[i];
      if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
        this.onButton(b.id);
        return;
      }
    }
    var f = Math.round((x - GX) / CELL);
    var r = Math.round((y - GY) / CELL);
    if (f < 0 || f > 8 || r < 0 || r > 9 ||
        Math.abs(x - (GX + f * CELL)) > CELL * 0.52 ||
        Math.abs(y - (GY + r * CELL)) > CELL * 0.52) {
      if (this.selected >= 0) {
        this.selected = -1;
        this.targets = {};
      }
      return;
    }
    this.cursorF = f;
    this.cursorR = r;
    this.tapCell(r * 9 + f);
  },

  onButton: function (id) {
    if (id === 'ai') this.setMode('ai');
    else if (id === 'pvp') this.setMode('pvp');
    else if (id === 'l1') this.setLevel(1);
    else if (id === 'l2') this.setLevel(2);
    else if (id === 'l3') this.setLevel(3);
    else if (id === 'undo') this.undo();
    else if (id === 'restart') this.restart();
  },

  // ---------------- 循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;

    this.readPad(dt);
    this.update(dt);
    this.draw();
  },

  update: function (dt) {
    this.pulse += dt;
    if (this.checkFlash > 0) this.checkFlash = Math.max(0, this.checkFlash - dt);

    var a = this.anim;
    if (a) {
      a.t += dt;
      if (a.t >= a.dur) {
        this.anim = null;
        this.afterAnim();
      }
    } else {
      this.aiStep(dt);
    }
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setFillStyle('#120e0a');
    ctx.fillRect(0, 0, W, H);
    this.drawTop(ctx);
    this.drawBoard(ctx);
    this.drawMarks(ctx);
    this.drawPieces(ctx);
    this.drawCursor(ctx);
    this.drawPanel(ctx);
    this.drawFooter(ctx);
    if (this.over) this.drawOver(ctx);
    ctx.draw();
  },

  drawTop: function (ctx) {
    var status = this.statusText();
    var danger = this.inCheckNow || this.checkFlash > 0;
    ctx.setTextAlign('left');
    ctx.setFontSize(34);
    ctx.setFillStyle('#f7eeda');
    ctx.fillText('中国象棋', 40, 40);
    ctx.setFontSize(18);
    ctx.setFillStyle('#8a7a63');
    ctx.fillText('标准 9×10 · 完整规则 · 内置 AI', 194, 39);

    ctx.setTextAlign('right');
    ctx.setFontSize(26);
    ctx.setFillStyle(danger ? '#ff6b5a' : '#e8b464');
    ctx.fillText(status, 1240, 40);
    ctx.setTextAlign('left');
  },

  drawBoard: function (ctx) {
    var i, f, r, x, y;

    ctx.setFillStyle('rgba(0,0,0,0.35)');
    ctx.fillRect(BX + 10, BY + 12, BW, BH);
    ctx.setFillStyle('#5d3a1a');
    ctx.fillRect(BX - 6, BY - 6, BW + 12, BH + 12);
    ctx.setFillStyle('#8a5a2b');
    ctx.fillRect(BX, BY, BW, BH);
    ctx.setFillStyle('#d9b07a');
    ctx.fillRect(BX + 8, BY + 8, BW - 16, BH - 16);

    // 木纹
    ctx.setFillStyle('rgba(138,90,43,0.13)');
    for (i = 0; i < 7; i++) {
      y = BY + 34 + i * 82;
      ctx.fillRect(BX + 16, y, BW - 32 - (i % 3) * 40, 3);
    }

    // 横线
    ctx.setStrokeStyle('#6b4522');
    ctx.setLineWidth(2);
    for (r = 0; r < 10; r++) {
      y = GY + r * CELL;
      ctx.beginPath();
      ctx.moveTo(GX, y);
      ctx.lineTo(GX + 8 * CELL, y);
      ctx.stroke();
    }

    // 竖线（河界处中间断开）
    for (f = 0; f < 9; f++) {
      x = GX + f * CELL;
      if (f === 0 || f === 8) {
        ctx.beginPath();
        ctx.moveTo(x, GY);
        ctx.lineTo(x, GY + 9 * CELL);
        ctx.stroke();
      } else {
        ctx.beginPath();
        ctx.moveTo(x, GY);
        ctx.lineTo(x, GY + 4 * CELL);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(x, GY + 5 * CELL);
        ctx.lineTo(x, GY + 9 * CELL);
        ctx.stroke();
      }
    }

    // 九宫斜线
    ctx.setLineWidth(2);
    ctx.beginPath();
    ctx.moveTo(GX + 3 * CELL, GY);
    ctx.lineTo(GX + 5 * CELL, GY + 2 * CELL);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(GX + 5 * CELL, GY);
    ctx.lineTo(GX + 3 * CELL, GY + 2 * CELL);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(GX + 3 * CELL, GY + 7 * CELL);
    ctx.lineTo(GX + 5 * CELL, GY + 9 * CELL);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(GX + 5 * CELL, GY + 7 * CELL);
    ctx.lineTo(GX + 3 * CELL, GY + 9 * CELL);
    ctx.stroke();

    // 楚河汉界
    ctx.setTextAlign('center');
    ctx.setFontSize(30);
    ctx.setFillStyle('#8a5a2b');
    ctx.fillText('楚  河', GX + 2 * CELL, GY + 4 * CELL + 32);
    ctx.fillText('汉  界', GX + 6 * CELL, GY + 4 * CELL + 32);

    // 将军提示
    if (this.checkFlash > 0) {
      var flash = 0.6 + 0.4 * Math.sin(this.pulse * 10);
      ctx.setGlobalAlpha(flash);
      ctx.setFillStyle('#c0392b');
      ctx.setFontSize(40);
      ctx.fillText('将  军', GX + 4 * CELL, GY + 4 * CELL + 40);
      ctx.setGlobalAlpha(1);
    }
    ctx.setTextAlign('left');
  },

  drawMarks: function (ctx) {
    var idx, x, y;

    // 上一步落点
    var spots = [this.lastFrom, this.lastTo];
    ctx.setFillStyle('rgba(255,209,102,0.18)');
    for (var s = 0; s < spots.length; s++) {
      idx = spots[s];
      if (idx < 0) continue;
      x = GX + (idx % 9) * CELL;
      y = GY + Math.floor(idx / 9) * CELL;
      ctx.fillRect(x - 24, y - 24, 48, 48);
    }

    // 选中格
    if (this.selected >= 0) {
      idx = this.selected;
      x = GX + (idx % 9) * CELL;
      y = GY + Math.floor(idx / 9) * CELL;
      ctx.setFillStyle('rgba(47,125,63,0.22)');
      ctx.fillRect(x - 26, y - 26, 52, 52);
      ctx.setStrokeStyle('#2f7d3f');
      ctx.setLineWidth(4);
      ctx.beginPath();
      ctx.arc(x, y, PR + 5, 0, Math.PI * 2);
      ctx.stroke();
    }

    // 合法落点
    for (var key in this.targets) {
      if (!this.targets[key]) continue;
      idx = parseInt(key, 10);
      x = GX + (idx % 9) * CELL;
      y = GY + Math.floor(idx / 9) * CELL;
      if (this.board[idx]) {
        ctx.setStrokeStyle('rgba(69,196,108,0.95)');
        ctx.setLineWidth(4);
        ctx.beginPath();
        ctx.arc(x, y, PR + 4, 0, Math.PI * 2);
        ctx.stroke();
      } else {
        ctx.setFillStyle('rgba(69,196,108,0.9)');
        ctx.beginPath();
        ctx.arc(x, y, 8, 0, Math.PI * 2);
        ctx.fill();
        ctx.setStrokeStyle('rgba(69,196,108,0.5)');
        ctx.setLineWidth(2);
        ctx.beginPath();
        ctx.arc(x, y, 15, 0, Math.PI * 2);
        ctx.stroke();
      }
    }

    // 被将军的将/帅
    if (this.inCheckNow || this.checkFlash > 0) {
      var k = findKing(this.board, this.turn);
      if (k >= 0) {
        x = GX + (k % 9) * CELL;
        y = GY + Math.floor(k / 9) * CELL;
        var alpha = 0.45 + 0.45 * Math.sin(this.pulse * 7);
        ctx.setGlobalAlpha(alpha);
        ctx.setStrokeStyle('#e04b3a');
        ctx.setLineWidth(5);
        ctx.beginPath();
        ctx.arc(x, y, PR + 9, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setGlobalAlpha(1);
      }
    }
  },

  drawPieces: function (ctx) {
    var idx, x, y;
    for (idx = 0; idx < 90; idx++) {
      var code = this.board[idx];
      if (!code) continue;
      if (this.anim && idx === this.anim.to) continue;
      x = GX + (idx % 9) * CELL;
      y = GY + Math.floor(idx / 9) * CELL;
      this.drawPiece(ctx, x, y, code, idx === this.selected);
    }

    var a = this.anim;
    if (a) {
      var t = a.t / a.dur;
      if (t > 1) t = 1;
      var e = 1 - (1 - t) * (1 - t);
      var fx = GX + (a.from % 9) * CELL;
      var fy = GY + Math.floor(a.from / 9) * CELL;
      var tx = GX + (a.to % 9) * CELL;
      var ty = GY + Math.floor(a.to / 9) * CELL;

      if (a.cap) {
        ctx.setGlobalAlpha(1 - t);
        this.drawPiece(ctx, tx, ty, a.cap, false);
        ctx.setGlobalAlpha(1);
      }

      var mx = fx + (tx - fx) * e;
      var my = fy + (ty - fy) * e;
      var sc = 1 + 0.12 * Math.sin(t * Math.PI);
      ctx.save();
      ctx.translate(mx, my);
      ctx.scale(sc, sc);
      this.drawPiece(ctx, 0, 0, a.piece, false);
      ctx.restore();
    }
  },

  drawPiece: function (ctx, x, y, code, selected) {
    var side = code[0];
    var color = side === 'r' ? '#b3261e' : '#1d1a16';

    ctx.setFillStyle('rgba(0,0,0,0.25)');
    ctx.beginPath();
    ctx.arc(x + 2, y + 3, PR, 0, Math.PI * 2);
    ctx.fill();

    ctx.setFillStyle('#f6e7cc');
    ctx.beginPath();
    ctx.arc(x, y, PR, 0, Math.PI * 2);
    ctx.fill();

    ctx.setStrokeStyle(color);
    ctx.setLineWidth(3);
    ctx.beginPath();
    ctx.arc(x, y, PR, 0, Math.PI * 2);
    ctx.stroke();

    ctx.setStrokeStyle(side === 'r' ? 'rgba(179,38,30,0.35)' : 'rgba(29,26,22,0.35)');
    ctx.setLineWidth(2);
    ctx.beginPath();
    ctx.arc(x, y, PR - 6, 0, Math.PI * 2);
    ctx.stroke();

    ctx.setTextAlign('center');
    ctx.setFontSize(30);
    ctx.setFillStyle(color);
    ctx.fillText(TEXT[code], x, y + 10);
    ctx.setTextAlign('left');

    if (selected) {
      var alphaRing = (0.55 + 0.45 * Math.sin(this.pulse * 6)).toFixed(2);
      ctx.setStrokeStyle('rgba(63,159,82,' + alphaRing + ')');
      ctx.setLineWidth(4);
      ctx.beginPath();
      ctx.arc(x, y, PR + 7, 0, Math.PI * 2);
      ctx.stroke();
    }
  },

  drawCursor: function (ctx) {
    var x = GX + this.cursorF * CELL;
    var y = GY + this.cursorR * CELL;
    var alpha = 0.5 + 0.5 * Math.sin(this.pulse * 4);
    var s = PR + 8;
    ctx.setGlobalAlpha(alpha);
    ctx.setStrokeStyle('#ffd166');
    ctx.setLineWidth(3);
    ctx.strokeRect(x - s, y - s, s * 2, s * 2);
    ctx.setGlobalAlpha(1);
  },

  drawPanel: function (ctx) {
    var turnText = this.turn === 'r' ? '红方' : (this.mode === 'ai' ? '电脑（黑）' : '黑方');
    var modeText = this.mode === 'ai' ? '人机对战 · 难度' + this.level : '双人对战';
    var danger = this.inCheckNow || this.checkFlash > 0;

    ctx.setFillStyle('#1f1913');
    ctx.fillRect(660, 64, 580, 110);
    ctx.setStrokeStyle('#3a2f21');
    ctx.setLineWidth(2);
    ctx.strokeRect(660, 64, 580, 110);

    ctx.setTextAlign('left');
    ctx.setFontSize(20);
    ctx.setFillStyle('#8a7a63');
    ctx.fillText('当前走子', 678, 96);

    ctx.setFontSize(40);
    ctx.setFillStyle('#f0d9a8');
    ctx.fillText(turnText, 678, 148);

    ctx.setTextAlign('right');
    ctx.setFontSize(22);
    ctx.setFillStyle(danger ? '#ff6b5a' : '#8a7a63');
    ctx.fillText(danger ? '被将军' : modeText, 1220, 96);
    ctx.setFillStyle('#c9b894');
    ctx.setFontSize(20);
    ctx.fillText(this.mode === 'ai' ? '你执红先行' : '红黑轮流走', 1220, 148);
    ctx.setTextAlign('left');

    this.drawButton(ctx, BUTTONS[0], '人机对战', this.mode === 'ai', false, false);
    this.drawButton(ctx, BUTTONS[1], '双人对战', this.mode === 'pvp', false, false);
    this.drawButton(ctx, BUTTONS[2], '简单', this.mode === 'ai' && this.level === 1, false, this.mode === 'pvp');
    this.drawButton(ctx, BUTTONS[3], '普通', this.mode === 'ai' && this.level === 2, false, this.mode === 'pvp');
    this.drawButton(ctx, BUTTONS[4], '困难', this.mode === 'ai' && this.level === 3, false, this.mode === 'pvp');
    this.drawButton(ctx, BUTTONS[5], '悔棋  Y', false, false, false);
    this.drawButton(ctx, BUTTONS[6], '重新开局  START', false, true, false);

    ctx.setFillStyle('#17120d');
    ctx.fillRect(660, 440, 580, 222);
    ctx.setStrokeStyle('#30251a');
    ctx.setLineWidth(2);
    ctx.strokeRect(660, 440, 580, 222);

    ctx.setFontSize(22);
    ctx.setFillStyle('#c9b894');
    ctx.fillText('已走 ' + this.history.length + ' 步', 680, 478);
    ctx.setTextAlign('right');
    ctx.setFillStyle('#8a7a63');
    ctx.fillText(this.mode === 'ai' ? '电脑等级 ' + this.level : '同屏双人', 1220, 478);
    ctx.setTextAlign('left');

    ctx.setFillStyle('#30251a');
    ctx.fillRect(680, 496, 540, 2);

    ctx.setFontSize(19);
    ctx.setFillStyle('#8a7a63');
    var lines = [
      '触摸：点自己的棋子，再点绿点落子',
      '手柄：方向键移光标，A 选子 / 落子',
      'X 切换模式与难度 · Y 悔棋 · START 重开',
      '点空处或再次点棋子可取消选择'
    ];
    for (var i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], 680, 532 + i * 32);
    }
  },

  drawButton: function (ctx, b, label, on, main, dim) {
    var fill = '#241b13';
    var stroke = '#4a3826';
    var color = '#d8c7a8';
    if (on) {
      fill = '#2b3f26';
      stroke = '#5c8a44';
      color = '#c8e6a0';
    }
    if (main) {
      fill = '#7a4a1c';
      stroke = '#b0742c';
      color = '#fff4e2';
    }
    if (dim) color = '#6a5a45';

    ctx.setFillStyle(fill);
    ctx.fillRect(b.x, b.y, b.w, b.h);
    ctx.setStrokeStyle(stroke);
    ctx.setLineWidth(2);
    ctx.strokeRect(b.x, b.y, b.w, b.h);

    ctx.setTextAlign('center');
    ctx.setFontSize(26);
    ctx.setFillStyle(color);
    ctx.fillText(label, b.x + b.w / 2, b.y + b.h / 2 + 9);
    ctx.setTextAlign('left');
  },

  drawFooter: function (ctx) {
    ctx.setFillStyle('#0d0a07');
    ctx.fillRect(0, 676, W, 44);
    ctx.setFillStyle('#2a2016');
    ctx.fillRect(0, 676, W, 2);
    ctx.setTextAlign('center');
    ctx.setFontSize(22);
    ctx.setFillStyle('#7a6c58');
    ctx.fillText('A 选子 / 落子 · 方向键移动光标 · X 模式与难度 · Y 悔棋 · START 重新开局', W / 2, 707);
    ctx.setTextAlign('left');
  },

  drawOver: function (ctx) {
    ctx.setFillStyle('rgba(8,5,3,0.78)');
    ctx.fillRect(0, 0, W, H);
    ctx.setFillStyle('#1f1913');
    ctx.fillRect(390, 236, 500, 248);
    ctx.setStrokeStyle('#8c5a22');
    ctx.setLineWidth(3);
    ctx.strokeRect(390, 236, 500, 248);

    ctx.setTextAlign('center');
    ctx.setFontSize(56);
    ctx.setFillStyle('#f0c274');
    ctx.fillText(this.resultText, 640, 330);

    ctx.setFontSize(24);
    ctx.setFillStyle('#c9b894');
    ctx.fillText('将死 / 无子可动，胜负已分', 640, 388);

    ctx.setFontSize(22);
    ctx.setFillStyle('#8a7a63');
    ctx.fillText('点击屏幕 / START 重新开局', 640, 442);
    ctx.setTextAlign('left');
  },

  statusText: function () {
    if (this.over) return this.resultText;
    if (this.thinking) return '电脑思考中…';
    if (this.anim) return '走子中…';
    if (this.inCheckNow) return (this.turn === 'r' ? '红方' : '黑方') + '被将军！';
    if (this.turn === 'r') return '红方走棋';
    return this.mode === 'ai' ? '电脑走棋' : '黑方走棋';
  }
});
