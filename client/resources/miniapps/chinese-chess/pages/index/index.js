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
      // 兵过河之后更值钱，鼓励推兵
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
  // 吃子优先，让 alpha-beta 剪枝更有效
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

Page({
  data: {
    topRows: [],
    bottomRows: [],
    turnText: '红方',
    status: '红方走棋',
    result: '',
    mode: 'ai',
    level: 2
  },

  onLoad() {
    this.board = initialBoard();
    this.turn = 'r';
    this.selected = -1;
    this.targets = {};
    this.history = [];
    this.over = false;
    this.thinking = false;
    this.render();
  },

  onUnload() {
    this.over = true;
  },

  render() {
    var board = this.board;
    var selected = this.selected;
    var targets = this.targets;
    var top = [];
    var bottom = [];

    for (var r = 0; r < 10; r++) {
      var row = [];
      for (var f = 0; f < 9; f++) {
        var idx = r * 9 + f;
        var p = board[idx];
        var dot = !!targets[idx];
        row.push({
          idx: idx,
          text: p ? TEXT[p] : '',
          side: p ? p[0] : '',
          sel: idx === selected,
          dot: dot,
          cls: dot && p ? 'last' : ''
        });
      }
      if (r < 5) top.push(row);
      else bottom.push(row);
    }

    var turnText = this.turn === 'r' ? '红方' : '黑方';
    if (this.data.mode === 'ai' && this.turn === 'b') turnText = '电脑（黑）';

    var status = this.data.result;
    if (!status) {
      if (this.thinking) status = 'AI 思考中…';
      else if (this.turn === 'r') status = '红方走棋';
      else status = this.data.mode === 'ai' ? '电脑走棋' : '黑方走棋';
    }

    this.setData({
      topRows: top,
      bottomRows: bottom,
      turnText: turnText,
      status: status
    });
  },

  onTap(e) {
    var idx = e.currentTarget.dataset.idx;
    idx = parseInt(idx, 10);
    if (isNaN(idx)) return;
    if (this.over || this.thinking) return;
    if (this.data.mode === 'ai' && this.turn !== 'r') return;

    var board = this.board;
    var piece = board[idx];

    if (this.selected >= 0 && this.targets[idx]) {
      this.doMove(this.selected, idx);
      return;
    }
    if (piece && piece[0] === this.turn) {
      this.selected = idx;
      this.targets = {};
      var moves = genMoves(board, idx);
      for (var i = 0; i < moves.length; i++) {
        var next = applyMove(board, idx, moves[i]);
        if (!inCheck(next, this.turn)) this.targets[moves[i]] = true;
      }
      this.render();
      return;
    }
    if (this.selected >= 0) {
      this.selected = -1;
      this.targets = {};
      this.render();
    }
  },

  doMove(from, to) {
    this.history.push({ board: this.board.slice(), turn: this.turn });
    this.board = applyMove(this.board, from, to);
    this.selected = -1;
    this.targets = {};
    this.turn = other(this.turn);
    this.checkEnd();
    this.render();
    if (this.over) return;
    if (this.data.mode === 'ai' && this.turn === 'b') this.aiMove();
  },

  checkEnd() {
    var moves = legalMoves(this.board, this.turn);
    if (moves.length) return false;
    this.over = true;
    var winner = this.turn === 'r' ? '黑方' : '红方';
    this.setData({ result: winner + '胜 · 点击重新开局' });
    return true;
  },

  aiMove() {
    var self = this;
    this.thinking = true;
    this.render();
    setTimeout(function () {
      if (self.over) return;
      var cfg = LEVELS[self.data.level] || LEVELS[2];
      var ctx = { nodes: 0, limit: cfg.limit };
      var moves = legalMoves(self.board, 'b');
      if (!moves.length) {
        self.thinking = false;
        self.checkEnd();
        self.render();
        return;
      }
      moves.sort(function (a, b) {
        return (b.cap ? VALUE[b.cap[1]] : 0) - (a.cap ? VALUE[a.cap[1]] : 0);
      });

      var best = null;
      var bestScore = -Infinity;
      for (var i = 0; i < moves.length; i++) {
        var next = applyMove(self.board, moves[i].from, moves[i].to);
        var score = -negamax(next, 'r', cfg.depth - 1, -Infinity, Infinity, ctx);
        score += (Math.random() - 0.5) * cfg.noise;
        if (score > bestScore) {
          bestScore = score;
          best = moves[i];
        }
      }
      self.thinking = false;
      if (!best) {
        self.checkEnd();
        self.render();
        return;
      }
      self.board = applyMove(self.board, best.from, best.to);
      self.turn = 'r';
      self.checkEnd();
      self.render();
    }, 60);
  },

  onUndo() {
    if (this.thinking || !this.history.length) {
      wx.showToast({ title: '没有可悔的棋', icon: 'none' });
      return;
    }
    var step = this.data.mode === 'ai' ? 2 : 1;
    while (step > 0 && this.history.length) {
      var last = this.history.pop();
      this.board = last.board;
      this.turn = last.turn;
      step--;
    }
    this.selected = -1;
    this.targets = {};
    this.over = false;
    this.setData({ result: '' });
    this.render();
  },

  onRestart() {
    this.board = initialBoard();
    this.turn = 'r';
    this.selected = -1;
    this.targets = {};
    this.history = [];
    this.over = false;
    this.thinking = false;
    this.setData({ result: '' });
    this.render();
  },

  onModeAi() {
    this.setData({ mode: 'ai' });
    this.onRestart();
  },

  onModePvp() {
    this.setData({ mode: 'pvp' });
    this.onRestart();
  },

  onLevel1() { this.setData({ level: 1 }); },
  onLevel2() { this.setData({ level: 2 }); },
  onLevel3() { this.setData({ level: 3 }); }
});
