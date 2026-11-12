// UNO · 单机牌局
// 标准 108 张牌，支持 2-4 人（玩家 + 电脑），完整实现跳过、反转、+2、+4、变色。

var COLORS = ['r', 'y', 'g', 'b'];
var COLOR_NAME = { r: '红', y: '黄', g: '绿', b: '蓝' };

function makeCard(id, color, kind, label, num) {
  return { id: id, color: color, kind: kind, label: label, num: num };
}

function buildDeck() {
  var deck = [];
  var id = 0;
  var i;
  var n;
  for (i = 0; i < COLORS.length; i++) {
    var c = COLORS[i];
    deck.push(makeCard(id++, c, 'num', '0', 0));
    for (n = 1; n <= 9; n++) {
      deck.push(makeCard(id++, c, 'num', String(n), n));
      deck.push(makeCard(id++, c, 'num', String(n), n));
    }
    for (n = 0; n < 2; n++) {
      deck.push(makeCard(id++, c, 'skip', '禁', -1));
      deck.push(makeCard(id++, c, 'rev', '反', -1));
      deck.push(makeCard(id++, c, 'd2', '+2', -1));
    }
  }
  for (i = 0; i < 4; i++) {
    deck.push(makeCard(id++, 'w', 'wild', '变', -1));
    deck.push(makeCard(id++, 'w', 'wd4', '+4', -1));
  }
  return deck;
}

function shuffle(deck) {
  for (var i = deck.length - 1; i > 0; i--) {
    var j = Math.floor(Math.random() * (i + 1));
    var t = deck[i];
    deck[i] = deck[j];
    deck[j] = t;
  }
  return deck;
}

function canPlay(card, top, color) {
  if (!card) return false;
  if (card.kind === 'wild' || card.kind === 'wd4') return true;
  if (card.color === color) return true;
  if (!top) return false;
  if (card.kind === 'num' && top.kind === 'num') return card.num === top.num;
  if (card.kind !== 'num' && card.kind === top.kind) return true;
  return false;
}

function drawOne(g) {
  if (!g.deck.length) {
    var top = g.discard.pop();
    g.deck = shuffle(g.discard.slice());
    g.discard = [top];
    if (!g.deck.length) g.deck = shuffle(buildDeck());
  }
  return g.deck.pop();
}

function drawTo(g, player, count) {
  for (var i = 0; i < count; i++) player.hand.push(drawOne(g));
}

function pickColor(player) {
  var counts = { r: 0, y: 0, g: 0, b: 0 };
  for (var i = 0; i < player.hand.length; i++) {
    var c = player.hand[i].color;
    if (counts[c] != null) counts[c] += 1;
  }
  var best = 'r';
  var bestCount = -1;
  for (var j = 0; j < COLORS.length; j++) {
    if (counts[COLORS[j]] > bestCount) {
      bestCount = counts[COLORS[j]];
      best = COLORS[j];
    }
  }
  if (bestCount <= 0) best = COLORS[Math.floor(Math.random() * 4)];
  return best;
}

function aiPick(cards) {
  var best = null;
  var bestRank = -1;
  for (var i = 0; i < cards.length; i++) {
    var card = cards[i];
    var rank;
    if (card.kind === 'wild') rank = 0;
    else if (card.kind === 'wd4') rank = 1;
    else if (card.kind === 'num') rank = 2;
    else if (card.kind === 'rev') rank = 3;
    else if (card.kind === 'skip') rank = 4;
    else rank = 5;
    if (rank > bestRank) {
      bestRank = rank;
      best = card;
    }
  }
  return best;
}

function cardView(card, playable, dim) {
  var cls = 'card c-' + card.color;
  if (dim) cls += ' card-off';
  if (playable) cls += ' card-on';
  var textCls = card.color === 'y' ? 'card-label card-dark-text' : 'card-label';
  return { id: card.id, label: card.label, cls: cls, textCls: textCls };
}

function chunk(list, size) {
  var rows = [];
  var row = [];
  for (var i = 0; i < list.length; i++) {
    row.push(list[i]);
    if (row.length === size) {
      rows.push(row);
      row = [];
    }
  }
  if (row.length) rows.push(row);
  return rows;
}

Page({
  data: {
    phase: 'menu',
    opponents: [],
    handRows: [],
    discard: { cls: 'big-card c-w', textCls: 'big-label', label: 'UNO' },
    deckCount: 0,
    colorName: '红',
    colorDot: 'c-r',
    turnText: '',
    directionText: '顺时针',
    message: '选择人数开始牌局',
    winnerText: '',
    overText: '',
    playerCount: 2
  },

  onLoad() {
    this.game = null;
    this.pending = null;
  },

  onUnload() {
    this.clearTimer();
  },

  clearTimer() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  },

  onStart(e) {
    var count = Number(e.currentTarget.dataset.count) || 2;
    this.startGame(count);
  },

  startGame(count) {
    this.clearTimer();
    this.pending = null;
    var deck = shuffle(buildDeck());
    var players = [];
    var i;
    for (i = 0; i < count; i++) {
      players.push({
        id: i,
        name: i === 0 ? '你' : '电脑 ' + i,
        ai: i !== 0,
        hand: []
      });
    }
    for (i = 0; i < 7; i++) {
      for (var p = 0; p < players.length; p++) players[p].hand.push(deck.pop());
    }
    var first = null;
    while (deck.length) {
      var card = deck.pop();
      if (card.kind === 'num') {
        first = card;
        break;
      }
      deck.unshift(card);
    }
    if (!first) first = makeCard(999, 'r', 'num', '0', 0);

    this.game = {
      deck: deck,
      discard: [first],
      color: first.color,
      players: players,
      turn: 0,
      dir: 1,
      drawn: false,
      phase: 'play',
      winner: -1,
      msg: '牌局开始，你先出牌'
    };
    this.render();
  },

  render() {
    var g = this.game;
    if (!g) return;
    var top = g.discard[g.discard.length - 1];
    var me = g.players[0];
    var myTurn = g.turn === 0 && g.phase === 'play';
    var hand = [];
    for (var i = 0; i < me.hand.length; i++) {
      var card = me.hand[i];
      hand.push(cardView(card, myTurn && canPlay(card, top, g.color), !myTurn));
    }

    var opponents = [];
    for (var j = 1; j < g.players.length; j++) {
      var p = g.players[j];
      opponents.push({
        id: p.id,
        name: p.name,
        count: p.hand.length,
        cls: g.turn === p.id && g.phase === 'play' ? 'opp opp-on' : 'opp'
      });
    }

    var winnerText = '';
    if (g.winner === 0) winnerText = '你赢了！';
    else if (g.winner > 0) winnerText = g.players[g.winner].name + ' 赢了';

    this.setData({
      phase: g.phase,
      opponents: opponents,
      handRows: chunk(hand, 8),
      discard: {
        cls: 'big-card c-' + top.color,
        textCls: top.color === 'y' ? 'big-label big-dark' : 'big-label',
        label: top.label
      },
      deckCount: g.deck.length,
      colorName: COLOR_NAME[g.color] || '',
      colorDot: 'c-' + g.color,
      turnText: g.phase === 'over' ? '牌局结束' :
        (g.turn === 0 ? '轮到你出牌' : g.players[g.turn].name + ' 思考中'),
      directionText: g.dir === 1 ? '顺时针' : '逆时针',
      message: g.msg,
      winnerText: winnerText,
      overText: g.phase === 'over' ? '先出完手牌即获胜，再来一局试试。' : '',
      playerCount: g.players.length
    });
  },

  onCard(e) {
    var g = this.game;
    if (!g || g.phase !== 'play' || g.turn !== 0) return;
    var id = Number(e.currentTarget.dataset.id);
    var card = null;
    for (var i = 0; i < g.players[0].hand.length; i++) {
      if (g.players[0].hand[i].id === id) {
        card = g.players[0].hand[i];
        break;
      }
    }
    if (!card) return;
    var top = g.discard[g.discard.length - 1];
    if (!canPlay(card, top, g.color)) {
      wx.showToast({ title: '这张牌现在不能出', icon: 'none' });
      return;
    }
    if (card.kind === 'wild' || card.kind === 'wd4') {
      this.pending = card;
      g.phase = 'color';
      g.msg = '选择万能牌的颜色';
      this.render();
      return;
    }
    this.playCard(card, null);
  },

  onColor(e) {
    var g = this.game;
    if (!g || g.phase !== 'color' || !this.pending) return;
    var color = e.currentTarget.dataset.color;
    if (COLORS.indexOf(color) < 0) return;
    var card = this.pending;
    this.pending = null;
    g.phase = 'play';
    this.playCard(card, color);
  },

  playCard(card, chosenColor) {
    var g = this.game;
    if (!g || g.phase !== 'play') return;
    var player = g.players[g.turn];
    var idx = -1;
    for (var i = 0; i < player.hand.length; i++) {
      if (player.hand[i].id === card.id) {
        idx = i;
        break;
      }
    }
    if (idx < 0) return;
    player.hand.splice(idx, 1);
    g.discard.push(card);
    if (card.kind === 'wild' || card.kind === 'wd4') {
      g.color = chosenColor || 'r';
    }

    var n = g.players.length;
    var step = 1;
    var msg = player.name + ' 打出 ' + card.label;
    if (card.kind === 'rev') {
      g.dir *= -1;
      step = n === 2 ? 2 : 1;
      msg += '，方向反转';
    } else if (card.kind === 'skip') {
      step = 2;
      msg += '，下家被跳过';
    } else if (card.kind === 'd2') {
      var victim = g.players[((g.turn + g.dir) % n + n) % n];
      drawTo(g, victim, 2);
      step = 2;
      msg += '，' + victim.name + ' 摸 2 张';
    } else if (card.kind === 'wd4') {
      var victim2 = g.players[((g.turn + g.dir) % n + n) % n];
      drawTo(g, victim2, 4);
      step = 2;
      msg += '，' + victim2.name + ' 摸 4 张';
    } else if (card.kind === 'wild') {
      msg += '，颜色变为' + COLOR_NAME[g.color];
    }
    g.msg = msg;
    g.drawn = false;

    if (player.hand.length === 0) {
      g.phase = 'over';
      g.winner = player.id;
      g.msg = player.name + ' 出完了所有手牌';
      this.render();
      return;
    }

    g.turn = ((g.turn + g.dir * step) % n + n) % n;
    this.render();
    this.scheduleAI();
  },

  onDraw() {
    var g = this.game;
    if (!g || g.phase !== 'play' || g.turn !== 0) return;
    if (g.drawn) {
      wx.showToast({ title: '本回合已经抽过牌了', icon: 'none' });
      return;
    }
    var card = drawOne(g);
    g.players[0].hand.push(card);
    g.drawn = true;
    var top = g.discard[g.discard.length - 1];
    if (canPlay(card, top, g.color)) {
      g.msg = '抽到 ' + card.label + '，可以出牌，也可以过牌';
      this.render();
    } else {
      g.msg = '抽到 ' + card.label + '，不能出，自动过牌';
      this.render();
      var self = this;
      this.clearTimer();
      this.timer = setTimeout(function () {
        self.passTurn();
      }, 800);
    }
  },

  onPass() {
    var g = this.game;
    if (!g || g.phase !== 'play' || g.turn !== 0) return;
    if (!g.drawn) {
      wx.showToast({ title: '先抽一张再说过牌', icon: 'none' });
      return;
    }
    // 抽到废牌时会挂一个自动过牌定时器，手动过牌前先取消，避免连过两家。
    this.clearTimer();
    this.passTurn();
  },

  passTurn() {
    var g = this.game;
    if (!g || g.phase !== 'play') return;
    this.clearTimer();
    var n = g.players.length;
    g.drawn = false;
    g.turn = ((g.turn + g.dir) % n + n) % n;
    g.msg = '轮到你过牌' ;
    if (g.turn !== 0) g.msg = '等待 ' + g.players[g.turn].name;
    this.render();
    this.scheduleAI();
  },

  scheduleAI() {
    var g = this.game;
    if (!g || g.phase !== 'play') return;
    if (!g.players[g.turn].ai) return;
    var self = this;
    this.clearTimer();
    this.timer = setTimeout(function () {
      self.aiMove();
    }, 750);
  },

  aiMove() {
    var g = this.game;
    if (!g || g.phase !== 'play') return;
    var p = g.players[g.turn];
    if (!p.ai) return;
    var top = g.discard[g.discard.length - 1];
    var playable = [];
    for (var i = 0; i < p.hand.length; i++) {
      if (canPlay(p.hand[i], top, g.color)) playable.push(p.hand[i]);
    }
    if (!playable.length) {
      var card = drawOne(g);
      p.hand.push(card);
      g.msg = p.name + ' 摸了一张牌';
      if (canPlay(card, top, g.color)) {
        this.playCard(card, card.kind === 'wild' || card.kind === 'wd4' ? pickColor(p) : null);
      } else {
        var n = g.players.length;
        g.turn = ((g.turn + g.dir) % n + n) % n;
        this.render();
        this.scheduleAI();
      }
      return;
    }
    var pick = aiPick(playable);
    this.playCard(pick, pick.kind === 'wild' || pick.kind === 'wd4' ? pickColor(p) : null);
  },

  onMenu() {
    this.clearTimer();
    this.pending = null;
    this.game = null;
    this.setData({
      phase: 'menu',
      opponents: [],
      handRows: [],
      discard: { cls: 'big-card c-w', textCls: 'big-label', label: 'UNO' },
      deckCount: 0,
      colorName: '红',
      colorDot: 'c-r',
      turnText: '',
      directionText: '顺时针',
      message: '选择人数开始牌局',
      winnerText: '',
      overText: ''
    });
  }
});
