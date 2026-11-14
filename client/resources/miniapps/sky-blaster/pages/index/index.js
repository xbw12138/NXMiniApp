// 星海突击 · 竖版飞机大战
// 操作：左摇杆 / 方向键 移动（自动开火），A / X 炸弹，START 暂停，BACK 重开，B 返回容器
// 触屏：拖拽任意位置移动飞船，右下角炸弹按钮；点击开始 / 重开

var W = 1280;
var H = 720;
var SPEED = 470;
var BOSS_INTERVAL = 40;
var BEST_KEY = 'sky_blaster_best';
var MAX_PARTICLES = 260;

var STAR_LAYERS = [
  { color: '#1c2b45', speed: 16, size: 1.4, count: 58 },
  { color: '#3c5f8c', speed: 54, size: 1.9, count: 40 },
  { color: '#9cc4ef', speed: 116, size: 2.4, count: 22 }
];

var BTN_BOMB = { x: 1168, y: 618, r: 54 };

function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function rand(a, b) { return a + Math.random() * (b - a); }
function pick(list) { return list[Math.floor(Math.random() * list.length)]; }
function hitCircle(ax, ay, ar, bx, by, br) {
  var dx = ax - bx;
  var dy = ay - by;
  var r = ar + br;
  return dx * dx + dy * dy <= r * r;
}

Page({
  data: {},

  onLoad: function () {
    this.ctx = wx.createCanvasContext('game');
    this.dead = false;
    this.prevPad = {};
    this.touch = null;
    this.touchId = null;
    this.best = 0;
    try { this.best = parseInt(wx.getStorageSync(BEST_KEY), 10) || 0; } catch (e) { this.best = 0; }

    this.initAudio();
    this.initStars();
    this.resetAll();

    var loop = function (ts) {
      if (this.dead) return;
      this.tick(ts);
      requestAnimationFrame(loop);
    }.bind(this);
    requestAnimationFrame(loop);
  },

  onShow: function () {
    if (this.state === 'play') this.state = 'paused';
  },

  onHide: function () {
    if (this.state === 'play') this.state = 'paused';
  },

  onUnload: function () {
    this.dead = true;
    for (var name in this.sfx) {
      try { this.sfx[name].destroy(); } catch (e) {}
    }
    if (this.bgm) {
      try { this.bgm.destroy(); } catch (e) {}
      this.bgm = null;
    }
  },

  // ---------------- 音频 ----------------

  initAudio: function () {
    this.sfx = {};
    this.sfxLast = {};
    var files = {
      boom: '/assets/boom.wav',
      hit: '/assets/hit.wav',
      power: '/assets/power.wav',
      bomb: '/assets/bomb.wav',
      boss: '/assets/boss.wav'
    };
    for (var name in files) {
      var audio = wx.createInnerAudioContext();
      audio.src = files[name];
      audio.volume = 0.55;
      this.sfx[name] = audio;
      this.sfxLast[name] = 0;
    }

    this.bgm = wx.createInnerAudioContext();
    this.bgm.src = '/assets/bgm.wav';
    this.bgm.loop = true;
    this.bgm.volume = 0.26;
    try { this.bgm.play(); } catch (e) {}
  },

  // 容器共用单个 mpv 音轨，高频音效要限制频率，避免反复打断音轨造成卡顿
  playSfx: function (name, gap) {
    var audio = this.sfx[name];
    if (!audio) return;
    var now = Date.now();
    if (now - this.sfxLast[name] < (gap || 110)) return;
    this.sfxLast[name] = now;
    try {
      audio.seek(0);
      audio.play();
    } catch (e) {}
  },

  // ---------------- 初始化 ----------------

  initStars: function () {
    this.stars = [];
    for (var i = 0; i < STAR_LAYERS.length; i++) {
      var layer = STAR_LAYERS[i];
      for (var n = 0; n < layer.count; n++) {
        this.stars.push({
          layer: i,
          x: rand(0, W),
          y: rand(0, H),
          s: layer.size * rand(0.7, 1.3)
        });
      }
    }
  },

  resetAll: function () {
    this.state = 'ready';
    this.anim = 0;
    this.time = 0;
    this.level = 1;
    this.score = 0;
    this.kills = 0;
    this.streak = 0;
    this.streakTimer = 0;
    this.shake = 0;
    this.flash = 0;
    this.flashColor = '#ffffff';
    this.spawnTimer = 1.3;
    this.bossIndex = 0;
    this.boss = null;
    this.banner = null;
    this.hint = '';
    this.hintT = 0;
    this.enemies = [];
    this.pBullets = [];
    this.eBullets = [];
    this.pickups = [];
    this.particles = [];
    this.player = {
      x: W / 2,
      y: H - 130,
      vx: 0,
      vy: 0,
      tilt: 0,
      hp: 3,
      maxHp: 5,
      bombs: 3,
      weapon: 1,
      shield: 0,
      invuln: 0,
      fireCd: 0,
      flame: 0
    };
  },

  startGame: function () {
    this.state = 'play';
    this.time = 0;
    this.spawnTimer = 1.0;
    this.setBanner('第 1 波', '撑住阵线', 1.5);
    this.playSfx('power', 400);
  },

  restart: function () {
    var best = this.best;
    this.resetAll();
    this.best = best;
    this.startGame();
  },

  setBanner: function (title, sub, dur) {
    this.banner = { title: title, sub: sub, t: 0, dur: dur || 1.6 };
  },

  showHint: function (text) {
    this.hint = text;
    this.hintT = 1.4;
  },

  // ---------------- 主循环 ----------------

  tick: function (ts) {
    var dt = this.lastTs ? (ts - this.lastTs) / 1000 : 0.016;
    this.lastTs = ts;
    if (dt > 0.05) dt = 0.05;
    if (dt <= 0) dt = 0.016;

    this.readInput(dt);
    if (this.state === 'play') this.update(dt);
    else this.updateIdle(dt);
    this.draw();
  },

  updateIdle: function (dt) {
    this.anim += dt;
    this.moveStars(dt);
    this.updateParticles(dt);
    if (this.shake > 0) this.shake = Math.max(0, this.shake - dt * 3.4);
    if (this.flash > 0) this.flash = Math.max(0, this.flash - dt * 2.6);
    if (this.hintT > 0) this.hintT = Math.max(0, this.hintT - dt);
    if (this.banner) {
      this.banner.t += dt;
      if (this.banner.t > this.banner.dur + 0.6) this.banner = null;
    }
  },

  moveStars: function (dt) {
    for (var i = 0; i < this.stars.length; i++) {
      var st = this.stars[i];
      st.y += STAR_LAYERS[st.layer].speed * dt;
      if (st.y > H) {
        st.y -= H;
        st.x = rand(0, W);
      }
    }
  },

  // ---------------- 输入 ----------------

  readInput: function (dt) {
    var pad = null;
    try { pad = wx.getGamepadState ? wx.getGamepadState() : null; } catch (e) { pad = null; }
    var b = (pad && pad.buttons) || {};
    var axes = (pad && pad.axes) || [0, 0, 0, 0];
    var lx = clamp(axes[0] || 0, -1, 1);
    var ly = clamp(axes[1] || 0, -1, 1);
    if (Math.abs(lx) < 0.18) lx = 0;
    if (Math.abs(ly) < 0.18) ly = 0;

    var dx = lx !== 0 ? lx : ((b.right ? 1 : 0) - (b.left ? 1 : 0));
    var dy = ly !== 0 ? ly : ((b.down ? 1 : 0) - (b.up ? 1 : 0));

    var cur = {
      bomb: !!(b.a || b.x),
      pause: !!b.start,
      restart: !!b.back
    };
    var prev = this.prevPad || {};
    this.prevPad = cur;

    if (cur.pause && !prev.pause) this.togglePause();
    if (cur.restart && !prev.restart) {
      if (this.state !== 'ready') this.restart();
    }
    if (cur.bomb && !prev.bomb) {
      if (this.state === 'ready' || this.state === 'over') this.restart();
      else if (this.state === 'play') this.useBomb();
    }

    this.inputX = dx;
    this.inputY = dy;

    if (this.state === 'play' && this.touch) {
      var p = this.player;
      p.x = clamp(this.touch.ox + (this.touch.x - this.touch.sx), 34, W - 34);
      p.y = clamp(this.touch.oy + (this.touch.y - this.touch.sy), 90, H - 80);
      p.tilt = 0;
    }
  },

  togglePause: function () {
    if (this.state === 'play') this.state = 'paused';
    else if (this.state === 'paused') this.state = 'play';
    else if (this.state === 'ready' || this.state === 'over') this.restart();
  },

  onTouchStart: function (e) {
    var x = e.detail.x;
    var y = e.detail.y;

    if (this.state === 'ready' || this.state === 'over') {
      this.restart();
      return;
    }
    if (this.state === 'paused') {
      this.state = 'play';
      return;
    }

    var dx = x - BTN_BOMB.x;
    var dy = y - BTN_BOMB.y;
    if (dx * dx + dy * dy <= BTN_BOMB.r * BTN_BOMB.r) {
      this.useBomb();
      return;
    }

    this.touch = {
      sx: x,
      sy: y,
      x: x,
      y: y,
      ox: this.player.x,
      oy: this.player.y
    };
  },

  onTouchMove: function (e) {
    if (!this.touch) return;
    this.touch.x = e.detail.x;
    this.touch.y = e.detail.y;
    if (this.state === 'play') {
      var p = this.player;
      p.x = clamp(this.touch.ox + (this.touch.x - this.touch.sx), 34, W - 34);
      p.y = clamp(this.touch.oy + (this.touch.y - this.touch.sy), 90, H - 80);
    }
  },

  onTouchEnd: function () {
    this.touch = null;
  },

  // ---------------- 更新 ----------------

  update: function (dt) {
    this.anim += dt;
    this.time += dt;
    this.moveStars(dt);
    this.level = Math.min(10, 1 + Math.floor(this.time / 22));
    if (this.hintT > 0) this.hintT = Math.max(0, this.hintT - dt);
    if (this.banner) {
      this.banner.t += dt;
      if (this.banner.t > this.banner.dur + 0.6) this.banner = null;
    }
    if (this.streakTimer > 0) {
      this.streakTimer -= dt;
      if (this.streakTimer <= 0) this.streak = 0;
    }

    this.updatePlayer(dt);
    this.updateBullets(dt);
    this.updateSpawn(dt);
    this.updateBoss(dt);
    this.updateEnemies(dt);
    this.collide(dt);
    this.updatePickups(dt);
    this.updateParticles(dt);

    if (this.shake > 0) this.shake = Math.max(0, this.shake - dt * 3.4);
    if (this.flash > 0) this.flash = Math.max(0, this.flash - dt * 2.6);
  },

  updatePlayer: function (dt) {
    var p = this.player;
    p.flame += dt * 26;
    if (p.invuln > 0) p.invuln = Math.max(0, p.invuln - dt);

    // 手柄：带一点惯性，摇杆手感更顺
    var targetVx = this.inputX * SPEED;
    var targetVy = this.inputY * SPEED;
    p.vx += (targetVx - p.vx) * Math.min(1, dt * 16);
    p.vy += (targetVy - p.vy) * Math.min(1, dt * 16);
    p.x = clamp(p.x + p.vx * dt, 34, W - 34);
    p.y = clamp(p.y + p.vy * dt, 90, H - 80);
    p.tilt += ((this.inputX * 0.22) - p.tilt) * Math.min(1, dt * 12);

    // 自动开火
    p.fireCd -= dt;
    if (p.fireCd <= 0) {
      this.firePlayer();
      p.fireCd = 0.15 - (p.weapon - 1) * 0.012;
    }
  },

  firePlayer: function () {
    var p = this.player;
    var y = p.y - 26;
    var lvl = p.weapon;
    var shots = [];
    if (lvl === 1) {
      shots.push({ x: 0, y: 0, vx: 0 });
    } else if (lvl === 2) {
      shots.push({ x: -9, y: 4, vx: 0 });
      shots.push({ x: 9, y: 4, vx: 0 });
    } else {
      shots.push({ x: 0, y: -4, vx: 0 });
      shots.push({ x: -12, y: 6, vx: 0 });
      shots.push({ x: 12, y: 6, vx: 0 });
      if (lvl >= 4) {
        shots.push({ x: -16, y: 10, vx: -215 });
        shots.push({ x: 16, y: 10, vx: 215 });
      }
    }
    for (var i = 0; i < shots.length; i++) {
      var s = shots[i];
      this.pBullets.push({
        x: p.x + s.x,
        y: y + s.y,
        vx: s.vx,
        vy: -960,
        dmg: 1
      });
    }
  },

  updateBullets: function (dt) {
    var i;
    for (i = this.pBullets.length - 1; i >= 0; i--) {
      var pb = this.pBullets[i];
      pb.x += pb.vx * dt;
      pb.y += pb.vy * dt;
      if (pb.y < -30 || pb.x < -30 || pb.x > W + 30) this.pBullets.splice(i, 1);
    }
    for (i = this.eBullets.length - 1; i >= 0; i--) {
      var eb = this.eBullets[i];
      eb.x += eb.vx * dt;
      eb.y += eb.vy * dt;
      if (eb.y > H + 40 || eb.y < -60 || eb.x < -40 || eb.x > W + 40) this.eBullets.splice(i, 1);
    }
  },

  updateSpawn: function (dt) {
    if (this.boss) return;
    this.spawnTimer -= dt;
    if (this.spawnTimer > 0) return;
    this.spawnWave();
    var base = Math.max(0.42, 1.15 - this.level * 0.055);
    this.spawnTimer = base * rand(0.8, 1.25);
    if (this.time >= (this.bossIndex + 1) * BOSS_INTERVAL) this.spawnBoss();
  },

  spawnWave: function () {
    var lvl = this.level;
    var roll = Math.random();
    var x = rand(120, W - 120);

    if (lvl >= 3 && roll < 0.16) {
      this.enemies.push(this.makeEnemy('gunship', x, -70));
      return;
    }
    if (lvl >= 4 && roll < 0.30) {
      this.enemies.push(this.makeEnemy('kamikaze', x, -40));
      return;
    }
    if (lvl >= 2 && roll < 0.62) {
      this.enemies.push(this.makeEnemy('darter', x, -50));
      return;
    }

    this.enemies.push(this.makeEnemy('scout', x, -40));
    if (lvl >= 5 && Math.random() < 0.5) {
      this.enemies.push(this.makeEnemy('scout', clamp(x + rand(-150, 150), 90, W - 90), -80));
    }
  },

  makeEnemy: function (kind, x, y) {
    var lvl = this.level;
    var e = {
      kind: kind,
      x: x,
      baseX: x,
      y: y,
      t: 0,
      phase: rand(0, Math.PI * 2),
      fire: rand(0.7, 1.7),
      flash: 0,
      dir: Math.random() < 0.5 ? -1 : 1,
      r: 20,
      hp: 1,
      maxHp: 1,
      score: 100,
      speed: 130
    };
    if (kind === 'scout') {
      e.r = 20;
      e.hp = 1 + Math.floor(lvl / 4);
      e.speed = 140 + lvl * 8;
      e.score = 100;
    } else if (kind === 'darter') {
      e.r = 19;
      e.hp = 2 + Math.floor(lvl / 3);
      e.speed = 175 + lvl * 6;
      e.score = 250;
    } else if (kind === 'gunship') {
      e.r = 34;
      e.hp = 8 + lvl * 2;
      e.speed = 74;
      e.score = 600;
    } else if (kind === 'kamikaze') {
      e.r = 19;
      e.hp = 3;
      e.speed = 250 + lvl * 8;
      e.score = 300;
    }
    e.maxHp = e.hp;
    return e;
  },

  spawnBoss: function () {
    var maxHp = 150 + this.bossIndex * 90;
    this.boss = {
      x: W / 2,
      y: -170,
      t: 0,
      hp: maxHp,
      maxHp: maxHp,
      r: 74,
      entering: true,
      flash: 0,
      fire: 1.6,
      minion: 6,
      spiral: 0,
      pattern: 0,
      score: 6000
    };
    this.setBanner('警  告', 'BOSS 舰队旗舰进入战场', 2.0);
    this.playSfx('boss', 600);
    wx.vibrateShort({ type: 'heavy' });
  },

  updateBoss: function (dt) {
    var boss = this.boss;
    if (!boss) return;
    boss.t += dt;
    boss.flash = Math.max(0, boss.flash - dt);

    if (boss.entering) {
      boss.y += 110 * dt;
      if (boss.y >= 138) {
        boss.y = 138;
        boss.entering = false;
      }
      return;
    }

    var enraged = boss.hp < boss.maxHp * 0.45;
    boss.x = W / 2 + Math.sin(boss.t * 0.55) * (W / 2 - 220);
    boss.y = 138 + Math.sin(boss.t * 1.5) * 12;

    boss.fire -= dt;
    if (boss.fire <= 0) {
      this.bossAttack(enraged);
      boss.fire = enraged ? 0.95 : 1.45;
    }

    boss.minion -= dt;
    if (boss.minion <= 0) {
      boss.minion = enraged ? 6.5 : 9;
      var x = rand(180, W - 180);
      this.enemies.push(this.makeEnemy(this.bossIndex > 0 && Math.random() < 0.5 ? 'darter' : 'scout', x, -40));
    }
  },

  bossAttack: function (enraged) {
    var boss = this.boss;
    var p = this.player;
    var i;
    var pattern = this.boss.pattern++ % 3;

    if (pattern === 0) {
      // 扇形弹幕
      var count = enraged ? 11 : 7;
      var span = enraged ? 1.5 : 1.1;
      for (i = 0; i < count; i++) {
        var a = Math.PI / 2 - span / 2 + span * (i / (count - 1));
        this.pushEnemyBullet(boss.x, boss.y + 50, Math.cos(a) * 240, Math.sin(a) * 240, 8, '#ff5c8a');
      }
    } else if (pattern === 1) {
      // 瞄准三连
      var base = Math.atan2(p.y - boss.y, p.x - boss.x);
      for (i = -1; i <= 1; i++) {
        var ang = base + i * 0.14;
        this.pushEnemyBullet(boss.x, boss.y + 40, Math.cos(ang) * 320, Math.sin(ang) * 320, 7, '#ffb347');
      }
      if (enraged) {
        for (i = 0; i < 6; i++) {
          var ca = this.boss.spiral + i * Math.PI / 3;
          this.pushEnemyBullet(boss.x, boss.y + 30, Math.cos(ca) * 180, Math.sin(ca) * 180, 6, '#ff8fa8');
        }
        this.boss.spiral += 0.5;
      }
    } else {
      // 双翼速射
      var offsets = [-52, 52];
      for (i = 0; i < offsets.length; i++) {
        var tx = boss.x + offsets[i];
        var ta = Math.atan2(p.y - boss.y, p.x - tx);
        this.pushEnemyBullet(tx, boss.y + 34, Math.cos(ta) * 300, Math.sin(ta) * 300, 6, '#ff7a90');
      }
    }
    this.playSfx('boom', 240);
  },

  pushEnemyBullet: function (x, y, vx, vy, r, color) {
    if (this.eBullets.length > 260) return;
    this.eBullets.push({ x: x, y: y, vx: vx, vy: vy, r: r, color: color });
  },

  updateEnemies: function (dt) {
    var p = this.player;
    for (var i = this.enemies.length - 1; i >= 0; i--) {
      var e = this.enemies[i];
      e.t += dt;
      if (e.flash > 0) e.flash = Math.max(0, e.flash - dt);

      if (e.kind === 'scout') {
        e.y += e.speed * dt;
        e.x = e.baseX + Math.sin(e.t * 1.9 + e.phase) * 58;
      } else if (e.kind === 'darter') {
        e.y += e.speed * dt;
        e.x += e.dir * 92 * dt;
        if (e.x < 70) e.dir = 1;
        if (e.x > W - 70) e.dir = -1;
        e.fire -= dt;
        if (e.fire <= 0 && e.y > 50 && e.y < H * 0.72) {
          e.fire = rand(1.5, 2.2);
          var a = Math.atan2(p.y - e.y, p.x - e.x);
          this.pushEnemyBullet(e.x, e.y + 16, Math.cos(a) * 290, Math.sin(a) * 290, 6, '#ff7a90');
        }
      } else if (e.kind === 'gunship') {
        var slow = e.y > 140 ? 16 : e.speed;
        e.y += slow * dt;
        e.x = e.baseX + Math.sin(e.t * 0.8 + e.phase) * 86;
        e.fire -= dt;
        if (e.fire <= 0 && e.y > 90) {
          e.fire = rand(1.7, 2.4);
          for (var k = -1; k <= 1; k++) {
            var ga = Math.PI / 2 + k * 0.30;
            this.pushEnemyBullet(e.x, e.y + 26, Math.cos(ga) * 230, Math.sin(ga) * 230, 6, '#ffb347');
          }
        }
      } else if (e.kind === 'kamikaze') {
        var ang = Math.atan2(p.y - e.y, p.x - e.x);
        e.x += Math.cos(ang) * e.speed * dt;
        e.y += Math.sin(ang) * e.speed * dt;
      }

      if (e.y > H + 90 || e.x < -120 || e.x > W + 120) this.enemies.splice(i, 1);
    }
  },

  collide: function (dt) {
    var i;
    var j;
    var p = this.player;

    // 玩家子弹 → 敌机 / BOSS
    for (i = this.pBullets.length - 1; i >= 0; i--) {
      var pb = this.pBullets[i];
      var consumed = false;

      for (j = this.enemies.length - 1; j >= 0; j--) {
        var e = this.enemies[j];
        if (hitCircle(pb.x, pb.y, 6, e.x, e.y, e.r)) {
          this.damageEnemy(e, pb.dmg, e.x, e.y);
          consumed = true;
          break;
        }
      }
      if (!consumed && this.boss && !this.boss.entering &&
          hitCircle(pb.x, pb.y, 6, this.boss.x, this.boss.y, this.boss.r)) {
        this.damageBoss(pb.dmg);
        consumed = true;
      }
      if (consumed) {
        this.spark(pb.x, pb.y, '#9fe8ff', 3);
        this.pBullets.splice(i, 1);
      }
    }

    if (p.invuln > 0 || this.state !== 'play') return;

    // 敌方子弹 → 玩家
    for (i = this.eBullets.length - 1; i >= 0; i--) {
      var eb = this.eBullets[i];
      if (hitCircle(eb.x, eb.y, eb.r, p.x, p.y - 4, 11)) {
        this.eBullets.splice(i, 1);
        this.hurtPlayer();
        return;
      }
    }

    // 敌机撞击 → 玩家
    for (i = this.enemies.length - 1; i >= 0; i--) {
      var en = this.enemies[i];
      if (hitCircle(en.x, en.y, en.r * 0.8, p.x, p.y - 4, 12)) {
        this.damageEnemy(en, 999, en.x, en.y);
        this.hurtPlayer();
        return;
      }
    }

    if (this.boss && !this.boss.entering &&
        hitCircle(this.boss.x, this.boss.y, this.boss.r * 0.85, p.x, p.y - 4, 12)) {
      this.hurtPlayer();
    }
  },

  damageEnemy: function (e, dmg, hx, hy) {
    e.hp -= dmg;
    e.flash = 0.08;
    if (e.hp > 0) {
      this.spark(hx, hy, '#ffe0a3', 2);
      return;
    }
    var idx = this.enemies.indexOf(e);
    if (idx >= 0) this.enemies.splice(idx, 1);
    this.onKill(e);
  },

  onKill: function (e) {
    this.kills++;
    this.streak++;
    this.streakTimer = 2.4;
    var mult = 1 + Math.min(4, Math.floor(this.streak / 6));
    this.score += e.score * mult;
    this.boom(e.x, e.y, e.r * 1.15);
    this.playSfx('boom', 120);
    if (e.r >= 30) wx.vibrateShort({ type: 'light' });

    if (Math.random() < 0.14) this.dropPickup(e.x, e.y);
  },

  damageBoss: function (dmg) {
    var boss = this.boss;
    if (!boss) return;
    boss.hp -= dmg;
    boss.flash = 0.06;
    if (boss.hp > 0) return;

    this.score += boss.score;
    this.kills++;
    this.bossIndex++;
    this.boss = null;
    this.eBullets.length = 0;
    this.shake = 1.2;
    this.flash = 0.7;
    this.flashColor = '#ffd9a8';
    this.setBanner('旗舰击破', '奖励 ' + boss.score + ' 分', 1.8);
    for (var i = 0; i < 7; i++) {
      this.boom(rand(W / 2 - 160, W / 2 + 160), rand(80, 280), rand(50, 90));
    }
    this.dropPickup(W / 2 - 70, 220);
    this.dropPickup(W / 2 + 70, 220);
    this.playSfx('bomb', 500);
    wx.vibrateShort({ type: 'heavy' });
  },

  hurtPlayer: function () {
    var p = this.player;
    if (p.invuln > 0 || this.state !== 'play') return;

    if (p.shield > 0) {
      p.shield--;
      p.invuln = 1.0;
      this.shake = 0.5;
      this.spark(p.x, p.y, '#8fe6ff', 14);
      this.playSfx('hit', 200);
      return;
    }

    p.hp--;
    p.invuln = 1.8;
    this.shake = 1;
    this.flash = 0.45;
    this.flashColor = '#ff5566';
    this.streak = 0;
    this.boom(p.x, p.y, 38);
    this.playSfx('hit', 200);
    wx.vibrateShort({ type: 'heavy' });

    // 受伤瞬间清掉身边一圈弹幕，给玩家喘息空间
    for (var i = this.eBullets.length - 1; i >= 0; i--) {
      var eb = this.eBullets[i];
      if (hitCircle(eb.x, eb.y, eb.r, p.x, p.y, 190)) {
        this.spark(eb.x, eb.y, '#ffd6de', 3);
        this.eBullets.splice(i, 1);
      }
    }

    if (p.hp <= 0) this.gameOver();
  },

  gameOver: function () {
    this.state = 'over';
    this.touch = null;
    this.flash = 0.8;
    this.flashColor = '#ff5566';
    for (var i = 0; i < 4; i++) this.boom(this.player.x + rand(-40, 40), this.player.y + rand(-40, 40), rand(30, 55));
    if (this.score > this.best) {
      this.best = this.score;
      try { wx.setStorageSync(BEST_KEY, this.best); } catch (e) {}
    }
  },

  useBomb: function () {
    var p = this.player;
    if (this.state !== 'play') return;
    if (p.bombs <= 0) {
      this.showHint('炸弹用完了');
      return;
    }
    p.bombs--;
    this.flash = 0.9;
    this.flashColor = '#bfe9ff';
    this.shake = 1.1;

    for (var i = this.eBullets.length - 1; i >= 0; i--) {
      this.spark(this.eBullets[i].x, this.eBullets[i].y, '#cdefff', 2);
    }
    this.eBullets.length = 0;

    for (var j = this.enemies.length - 1; j >= 0; j--) {
      this.damageEnemy(this.enemies[j], 14, this.enemies[j].x, this.enemies[j].y);
    }
    if (this.boss) this.damageBoss(26);

    this.ring(p.x, p.y, '#9fe3ff');
    this.playSfx('bomb', 400);
    wx.vibrateShort({ type: 'heavy' });
  },

  dropPickup: function (x, y) {
    var roll = Math.random();
    var kind = roll < 0.46 ? 'weapon' : (roll < 0.76 ? 'bomb' : 'hp');
    this.pickups.push({ kind: kind, x: x, y: y, vy: 92, t: 0, r: 18 });
  },

  updatePickups: function (dt) {
    var p = this.player;
    for (var i = this.pickups.length - 1; i >= 0; i--) {
      var u = this.pickups[i];
      u.t += dt;
      u.y += u.vy * dt;
      u.x += Math.sin(u.t * 2.6) * 22 * dt;
      if (u.y > H + 40) {
        this.pickups.splice(i, 1);
        continue;
      }
      if (hitCircle(u.x, u.y, u.r + 12, p.x, p.y, 20)) {
        this.pickups.splice(i, 1);
        this.applyPickup(u.kind);
      }
    }
  },

  applyPickup: function (kind) {
    var p = this.player;
    if (kind === 'weapon') {
      if (p.weapon < 4) {
        p.weapon++;
        this.showHint('火力强化 Lv.' + p.weapon);
      } else {
        this.score += 500;
        this.showHint('火力已满 +500');
      }
    } else if (kind === 'bomb') {
      if (p.bombs < 5) {
        p.bombs++;
        this.showHint('炸弹 +1');
      } else {
        this.score += 300;
        this.showHint('炸弹已满 +300');
      }
    } else {
      if (p.hp < p.maxHp) {
        p.hp++;
        this.showHint('机体修复');
      } else {
        p.shield = Math.min(2, p.shield + 1);
        this.showHint('护盾 +1');
      }
    }
    this.spark(p.x, p.y - 20, '#9dffcf', 12);
    this.playSfx('power', 150);
  },

  // ---------------- 粒子 ----------------

  spark: function (x, y, color, count) {
    for (var i = 0; i < count; i++) {
      if (this.particles.length >= MAX_PARTICLES) return;
      this.particles.push({
        type: 'spark',
        x: x,
        y: y,
        vx: rand(-150, 150),
        vy: rand(-180, 60),
        life: rand(0.2, 0.45),
        max: 0.45,
        size: rand(2, 4),
        color: color
      });
    }
  },

  boom: function (x, y, size) {
    var count = Math.min(34, Math.floor(size * 0.7) + 8);
    for (var i = 0; i < count; i++) {
      if (this.particles.length >= MAX_PARTICLES) break;
      var ang = rand(0, Math.PI * 2);
      var spd = rand(40, 90) * (size / 30);
      this.particles.push({
        type: 'spark',
        x: x,
        y: y,
        vx: Math.cos(ang) * spd,
        vy: Math.sin(ang) * spd,
        life: rand(0.25, 0.6),
        max: 0.6,
        size: rand(2, 5),
        color: pick(['#ffd166', '#ff8f5c', '#ff5f6d', '#fff2c9'])
      });
    }
    this.ring(x, y, '#ffc46b', size);
  },

  ring: function (x, y, color, size) {
    if (this.particles.length >= MAX_PARTICLES) return;
    this.particles.push({
      type: 'ring',
      x: x,
      y: y,
      r: 6,
      grow: (size || 40) * 5.2,
      life: 0.34,
      max: 0.34,
      color: color
    });
  },

  updateParticles: function (dt) {
    for (var i = this.particles.length - 1; i >= 0; i--) {
      var pt = this.particles[i];
      pt.life -= dt;
      if (pt.life <= 0) {
        this.particles.splice(i, 1);
        continue;
      }
      if (pt.type === 'ring') {
        pt.r += pt.grow * dt;
      } else {
        pt.x += pt.vx * dt;
        pt.y += pt.vy * dt;
        pt.vx *= 1 - Math.min(1, dt * 3.2);
        pt.vy *= 1 - Math.min(1, dt * 3.2);
      }
    }
  },

  // ---------------- 绘制 ----------------

  draw: function () {
    var ctx = this.ctx;
    ctx.setGlobalAlpha(1);
    ctx.setFillStyle('#05070f');
    ctx.fillRect(0, 0, W, H);

    var sx = 0;
    var sy = 0;
    if (this.shake > 0.02) {
      sx = rand(-1, 1) * this.shake * 13;
      sy = rand(-1, 1) * this.shake * 10;
    }

    ctx.save();
    ctx.translate(sx, sy);
    this.drawBackdrop(ctx);
    this.drawPickups(ctx);
    if (this.state === 'ready') {
      this.drawPlayer(ctx, true);
    } else if (this.state !== 'over') {
      this.drawPlayer(ctx, false);
    }
    this.drawEnemies(ctx);
    this.drawBoss(ctx);
    this.drawBullets(ctx);
    this.drawParticles(ctx);
    ctx.restore();

    this.drawHud(ctx);
    this.drawBanner(ctx);
    this.drawHint(ctx);

    if (this.state === 'ready') this.drawReady(ctx);
    else if (this.state === 'paused') this.drawPaused(ctx);
    else if (this.state === 'over') this.drawOver(ctx);

    if (this.flash > 0.01) {
      ctx.setGlobalAlpha(Math.min(0.5, this.flash * 0.42));
      ctx.setFillStyle(this.flashColor);
      ctx.fillRect(0, 0, W, H);
      ctx.setGlobalAlpha(1);
    }

    ctx.draw();
  },

  drawBackdrop: function (ctx) {
    // 远景星云
    ctx.setFillStyle('rgba(46,86,150,0.055)');
    ctx.beginPath();
    ctx.arc(250, 560, 300, 0, Math.PI * 2);
    ctx.fill();
    ctx.setFillStyle('rgba(120,60,160,0.045)');
    ctx.beginPath();
    ctx.arc(1040, 240, 260, 0, Math.PI * 2);
    ctx.fill();

    // 左下角行星
    ctx.setFillStyle('#0a1424');
    ctx.beginPath();
    ctx.arc(110, 700, 210, 0, Math.PI * 2);
    ctx.fill();
    ctx.setStrokeStyle('rgba(90,160,230,0.22)');
    ctx.setLineWidth(2);
    ctx.beginPath();
    ctx.arc(110, 700, 210, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setFillStyle('rgba(70,130,200,0.10)');
    ctx.beginPath();
    ctx.arc(58, 646, 74, 0, Math.PI * 2);
    ctx.fill();

    // 星空：按层合批，减少绘制指令
    for (var i = 0; i < STAR_LAYERS.length; i++) {
      var layer = STAR_LAYERS[i];
      ctx.setFillStyle(layer.color);
      ctx.beginPath();
      for (var j = 0; j < this.stars.length; j++) {
        var st = this.stars[j];
        if (st.layer !== i) continue;
        ctx.rect(st.x, st.y, st.s, st.s);
      }
      ctx.fill();
    }
  },

  drawPlayer: function (ctx, idle) {
    var p = this.player;
    var t = this.anim;
    var x = p.x;
    var y = idle ? p.y + Math.sin(t * 2.2) * 5 : p.y;

    if (!idle && p.invuln > 0 && Math.floor(p.invuln * 14) % 2 === 0) return;

    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(p.tilt);

    // 尾焰
    var flame = 16 + Math.sin(p.flame) * 6;
    ctx.setFillStyle('rgba(80,190,255,0.42)');
    ctx.beginPath();
    ctx.moveTo(-9, 16);
    ctx.lineTo(0, 16 + flame + 10);
    ctx.lineTo(9, 16);
    ctx.closePath();
    ctx.fill();
    ctx.setFillStyle('rgba(200,245,255,0.95)');
    ctx.beginPath();
    ctx.moveTo(-4, 16);
    ctx.lineTo(0, 16 + flame);
    ctx.lineTo(4, 16);
    ctx.closePath();
    ctx.fill();

    // 机翼
    ctx.setFillStyle('#17527d');
    ctx.beginPath();
    ctx.moveTo(0, -20);
    ctx.lineTo(27, 12);
    ctx.lineTo(15, 16);
    ctx.lineTo(9, 22);
    ctx.lineTo(-9, 22);
    ctx.lineTo(-15, 16);
    ctx.lineTo(-27, 12);
    ctx.closePath();
    ctx.fill();

    // 机身
    ctx.setFillStyle('#49c6f5');
    ctx.beginPath();
    ctx.moveTo(0, -28);
    ctx.lineTo(9, 6);
    ctx.lineTo(6, 20);
    ctx.lineTo(-6, 20);
    ctx.lineTo(-9, 6);
    ctx.closePath();
    ctx.fill();

    // 高光与座舱
    ctx.setFillStyle('rgba(230,250,255,0.92)');
    ctx.fillRect(-3, -16, 6, 14);
    ctx.setFillStyle('#ffd166');
    ctx.fillRect(-19, 9, 5, 4);
    ctx.fillRect(14, 9, 5, 4);

    ctx.restore();

    if (p.shield > 0) {
      ctx.setStrokeStyle('rgba(120,225,255,' + (0.45 + Math.sin(t * 6) * 0.18) + ')');
      ctx.setLineWidth(2.5);
      ctx.beginPath();
      ctx.arc(x, y, 36, 0, Math.PI * 2);
      ctx.stroke();
    }
  },

  drawEnemies: function (ctx) {
    for (var i = 0; i < this.enemies.length; i++) {
      var e = this.enemies[i];
      var hot = e.flash > 0;
      ctx.save();
      ctx.translate(e.x, e.y);

      if (e.kind === 'scout') {
        ctx.setFillStyle(hot ? '#ffffff' : '#7c2d12');
        ctx.beginPath();
        ctx.moveTo(0, 20);
        ctx.lineTo(19, -14);
        ctx.lineTo(0, -6);
        ctx.lineTo(-19, -14);
        ctx.closePath();
        ctx.fill();
        ctx.setFillStyle(hot ? '#ffffff' : '#ff8a5c');
        ctx.beginPath();
        ctx.moveTo(0, 12);
        ctx.lineTo(11, -8);
        ctx.lineTo(0, -2);
        ctx.lineTo(-11, -8);
        ctx.closePath();
        ctx.fill();
        ctx.setFillStyle('#ffd166');
        ctx.fillRect(-2, -2, 4, 8);
      } else if (e.kind === 'darter') {
        ctx.setFillStyle(hot ? '#ffffff' : '#6d28d9');
        ctx.beginPath();
        ctx.moveTo(0, 22);
        ctx.lineTo(17, 0);
        ctx.lineTo(0, -20);
        ctx.lineTo(-17, 0);
        ctx.closePath();
        ctx.fill();
        ctx.setFillStyle(hot ? '#ffffff' : '#c084fc');
        ctx.beginPath();
        ctx.moveTo(0, 12);
        ctx.lineTo(8, 0);
        ctx.lineTo(0, -10);
        ctx.lineTo(-8, 0);
        ctx.closePath();
        ctx.fill();
      } else if (e.kind === 'gunship') {
        ctx.setFillStyle(hot ? '#ffffff' : '#7c3f06');
        ctx.beginPath();
        ctx.moveTo(-34, -12);
        ctx.lineTo(34, -12);
        ctx.lineTo(24, 16);
        ctx.lineTo(-24, 16);
        ctx.closePath();
        ctx.fill();
        ctx.setFillStyle(hot ? '#ffffff' : '#f59e0b');
        ctx.fillRect(-20, -6, 40, 12);
        ctx.setFillStyle('#3b1d05');
        ctx.fillRect(-12, 4, 8, 8);
        ctx.fillRect(4, 4, 8, 8);
        // 血条
        var ratio = Math.max(0, e.hp / e.maxHp);
        ctx.setFillStyle('rgba(0,0,0,0.55)');
        ctx.fillRect(-30, -26, 60, 5);
        ctx.setFillStyle('#ff9f43');
        ctx.fillRect(-30, -26, 60 * ratio, 5);
      } else if (e.kind === 'kamikaze') {
        var pulse = 0.55 + Math.sin(e.t * 10) * 0.35;
        ctx.setFillStyle('rgba(255,80,80,' + pulse * 0.5 + ')');
        ctx.beginPath();
        ctx.arc(0, 0, 24, 0, Math.PI * 2);
        ctx.fill();
        ctx.setFillStyle(hot ? '#ffffff' : '#ff5252');
        ctx.beginPath();
        ctx.moveTo(0, 18);
        ctx.lineTo(15, -12);
        ctx.lineTo(-15, -12);
        ctx.closePath();
        ctx.fill();
        ctx.setFillStyle('#2b0a0a');
        ctx.fillRect(-4, -4, 8, 8);
      }

      ctx.restore();
    }
  },

  drawBoss: function (ctx) {
    var boss = this.boss;
    if (!boss) return;
    var hot = boss.flash > 0;
    var t = boss.t;

    ctx.save();
    ctx.translate(boss.x, boss.y);

    // 外翼
    ctx.setFillStyle(hot ? '#ffffff' : '#4a1220');
    ctx.beginPath();
    ctx.moveTo(-96, -26);
    ctx.lineTo(96, -26);
    ctx.lineTo(74, 20);
    ctx.lineTo(30, 46);
    ctx.lineTo(-30, 46);
    ctx.lineTo(-74, 20);
    ctx.closePath();
    ctx.fill();

    // 主体
    ctx.setFillStyle(hot ? '#ffffff' : '#8b1f33');
    ctx.beginPath();
    ctx.moveTo(-60, -18);
    ctx.lineTo(60, -18);
    ctx.lineTo(44, 30);
    ctx.lineTo(-44, 30);
    ctx.closePath();
    ctx.fill();

    // 装甲条
    ctx.setFillStyle(hot ? '#ffffff' : '#d93a52');
    ctx.fillRect(-38, -8, 76, 14);

    // 核心
    var pulse = 0.6 + Math.sin(t * 4) * 0.3;
    ctx.setFillStyle('rgba(255,220,120,' + pulse + ')');
    ctx.beginPath();
    ctx.arc(0, -2, 13, 0, Math.PI * 2);
    ctx.fill();
    ctx.setFillStyle('#fff6d8');
    ctx.beginPath();
    ctx.arc(0, -2, 6, 0, Math.PI * 2);
    ctx.fill();

    // 两侧炮口
    ctx.setFillStyle('#2b0a12');
    ctx.fillRect(-72, 4, 16, 22);
    ctx.fillRect(56, 4, 16, 22);

    ctx.restore();

    // 顶部血条
    var ratio = Math.max(0, boss.hp / boss.maxHp);
    ctx.setFillStyle('rgba(0,0,0,0.55)');
    ctx.fillRect(360, 22, 560, 14);
    ctx.setFillStyle(ratio > 0.45 ? '#ff5c8a' : '#ff3b3b');
    ctx.fillRect(360, 22, 560 * ratio, 14);
    ctx.setStrokeStyle('rgba(255,180,200,0.55)');
    ctx.setLineWidth(1.5);
    ctx.strokeRect(360, 22, 560, 14);
    ctx.setTextAlign('center');
    ctx.setFontSize(15);
    ctx.setFillStyle('#ffdce6');
    ctx.fillText('BOSS', 640, 52);
    ctx.setTextAlign('left');
  },

  drawBullets: function (ctx) {
    var i;
    // 玩家子弹
    for (i = 0; i < this.pBullets.length; i++) {
      var pb = this.pBullets[i];
      ctx.setFillStyle('rgba(90,220,255,0.28)');
      ctx.fillRect(pb.x - 5, pb.y - 9, 10, 24);
      ctx.setFillStyle('#d7f8ff');
      ctx.fillRect(pb.x - 2, pb.y - 10, 4, 16);
    }
    // 敌方子弹
    var dense = this.eBullets.length > 80;
    for (i = 0; i < this.eBullets.length; i++) {
      var eb = this.eBullets[i];
      if (!dense) {
        ctx.setFillStyle('rgba(255,255,255,0.16)');
        ctx.beginPath();
        ctx.arc(eb.x, eb.y, eb.r + 4, 0, Math.PI * 2);
        ctx.fill();
      }
      ctx.setFillStyle(eb.color);
      ctx.beginPath();
      ctx.arc(eb.x, eb.y, eb.r, 0, Math.PI * 2);
      ctx.fill();
      ctx.setFillStyle('rgba(255,255,255,0.75)');
      ctx.beginPath();
      ctx.arc(eb.x - eb.r * 0.28, eb.y - eb.r * 0.28, eb.r * 0.34, 0, Math.PI * 2);
      ctx.fill();
    }
  },

  drawPickups: function (ctx) {
    for (var i = 0; i < this.pickups.length; i++) {
      var u = this.pickups[i];
      var color = u.kind === 'weapon' ? '#4ade80' : (u.kind === 'bomb' ? '#60a5fa' : '#f472b6');
      var label = u.kind === 'weapon' ? '火' : (u.kind === 'bomb' ? '弹' : '修');

      ctx.save();
      ctx.translate(u.x, u.y);
      ctx.rotate(u.t * 2.2);
      ctx.setFillStyle('rgba(255,255,255,0.12)');
      ctx.beginPath();
      ctx.arc(0, 0, 24, 0, Math.PI * 2);
      ctx.fill();
      ctx.setFillStyle(color);
      ctx.fillRect(-13, -13, 26, 26);
      ctx.setFillStyle('rgba(6,10,18,0.85)');
      ctx.fillRect(-8, -8, 16, 16);
      ctx.restore();

      ctx.setTextAlign('center');
      ctx.setFontSize(17);
      ctx.setFillStyle(color);
      ctx.fillText(label, u.x, u.y + 6);
      ctx.setTextAlign('left');
    }
  },

  drawParticles: function (ctx) {
    for (var i = 0; i < this.particles.length; i++) {
      var pt = this.particles[i];
      var a = Math.max(0, pt.life / pt.max);
      if (pt.type === 'ring') {
        ctx.setStrokeStyle(pt.color);
        ctx.setGlobalAlpha(a * 0.8);
        ctx.setLineWidth(3 * a + 1);
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, pt.r, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setGlobalAlpha(1);
      } else {
        ctx.setGlobalAlpha(a);
        ctx.setFillStyle(pt.color);
        ctx.fillRect(pt.x - pt.size / 2, pt.y - pt.size / 2, pt.size, pt.size);
      }
    }
    ctx.setGlobalAlpha(1);
  },

  drawHud: function (ctx) {
    var p = this.player;
    var i;

    // 得分
    ctx.setTextAlign('left');
    ctx.setFontSize(18);
    ctx.setFillStyle('#5c7695');
    ctx.fillText('得分', 36, 44);
    ctx.setFontSize(42);
    ctx.setFillStyle('#eaf6ff');
    ctx.fillText('' + this.score, 36, 88);
    ctx.setFontSize(17);
    ctx.setFillStyle('#4f647e');
    ctx.fillText('最高 ' + this.best, 36, 114);

    // 右侧状态
    ctx.setTextAlign('right');
    ctx.setFontSize(20);
    ctx.setFillStyle('#8fb4d8');
    ctx.fillText('第 ' + this.level + ' 波', 1244, 46);
    ctx.setFontSize(16);
    ctx.setFillStyle('#5c7695');
    ctx.fillText('火力 Lv.' + p.weapon, 1244, 72);

    // 炸弹图标
    ctx.setTextAlign('left');
    ctx.setFontSize(15);
    ctx.setFillStyle('#5c7695');
    ctx.fillText('炸弹', 1010, 100);
    for (i = 0; i < 5; i++) {
      var bx = 1052 + i * 26;
      ctx.setFillStyle(i < p.bombs ? '#60a5fa' : 'rgba(96,165,250,0.18)');
      ctx.beginPath();
      ctx.arc(bx, 95, 8, 0, Math.PI * 2);
      ctx.fill();
    }

    // 机体
    ctx.setFillStyle('#5c7695');
    ctx.setFontSize(15);
    ctx.fillText('机体', 1010, 132);
    for (i = 0; i < p.maxHp; i++) {
      var hx = 1052 + i * 26;
      var filled = i < p.hp;
      ctx.setFillStyle(filled ? '#ff6b81' : 'rgba(255,107,129,0.20)');
      ctx.beginPath();
      ctx.moveTo(hx, 120);
      ctx.lineTo(hx + 9, 136);
      ctx.lineTo(hx - 9, 136);
      ctx.closePath();
      ctx.fill();
    }
    if (p.shield > 0) {
      ctx.setFillStyle('#7ee8ff');
      ctx.fillText('护盾 x' + p.shield, 1150, 158);
    }

    // 连击
    if (this.streak >= 6) {
      var mult = 1 + Math.min(4, Math.floor(this.streak / 6));
      ctx.setTextAlign('center');
      ctx.setFontSize(26);
      ctx.setFillStyle('#ffd166');
      ctx.fillText('连击 x' + mult, 640, 108);
      ctx.setFontSize(15);
      ctx.setFillStyle('#a58a4a');
      ctx.fillText('已击破 ' + this.streak, 640, 130);
      ctx.setTextAlign('left');
    }

    // 触屏炸弹按钮
    if (this.state === 'play' || this.state === 'paused') {
      var active = p.bombs > 0;
      ctx.setFillStyle(active ? 'rgba(60,130,220,0.30)' : 'rgba(60,80,110,0.18)');
      ctx.beginPath();
      ctx.arc(BTN_BOMB.x, BTN_BOMB.y, BTN_BOMB.r, 0, Math.PI * 2);
      ctx.fill();
      ctx.setStrokeStyle(active ? 'rgba(140,200,255,0.55)' : 'rgba(120,150,180,0.25)');
      ctx.setLineWidth(2);
      ctx.beginPath();
      ctx.arc(BTN_BOMB.x, BTN_BOMB.y, BTN_BOMB.r, 0, Math.PI * 2);
      ctx.stroke();
      ctx.setTextAlign('center');
      ctx.setFontSize(20);
      ctx.setFillStyle(active ? '#d6ecff' : '#6b8099');
      ctx.fillText('炸弹', BTN_BOMB.x, BTN_BOMB.y - 2);
      ctx.setFontSize(26);
      ctx.setFillStyle(active ? '#ffffff' : '#6b8099');
      ctx.fillText('' + p.bombs, BTN_BOMB.x, BTN_BOMB.y + 30);
      ctx.setTextAlign('left');
    }

    // 底部提示
    ctx.setTextAlign('center');
    ctx.setFontSize(16);
    ctx.setFillStyle('#34455c');
    ctx.fillText('左摇杆 / 方向键 移动 · 自动开火 · A / X 炸弹 · START 暂停 · BACK 重开 · B 返回', 640, 706);
    ctx.setTextAlign('left');
  },

  drawBanner: function (ctx) {
    var b = this.banner;
    if (!b) return;
    var fadeIn = Math.min(1, b.t / 0.18);
    var fadeOut = Math.min(1, Math.max(0, (b.dur + 0.6 - b.t) / 0.5));
    var a = Math.min(fadeIn, fadeOut);
    if (a <= 0) return;

    ctx.setGlobalAlpha(a);
    ctx.setTextAlign('center');
    ctx.setFontSize(46);
    ctx.setFillStyle('#ffd166');
    ctx.fillText(b.title, 640, 250);
    ctx.setFontSize(20);
    ctx.setFillStyle('#b7c9dd');
    ctx.fillText(b.sub, 640, 286);
    ctx.setTextAlign('left');
    ctx.setGlobalAlpha(1);
  },

  drawHint: function (ctx) {
    if (this.hintT <= 0) return;
    ctx.setGlobalAlpha(Math.min(1, this.hintT * 2));
    ctx.setTextAlign('center');
    ctx.setFontSize(22);
    ctx.setFillStyle('#cfe6ff');
    ctx.fillText(this.hint, 640, 600);
    ctx.setTextAlign('left');
    ctx.setGlobalAlpha(1);
  },

  drawMask: function (ctx) {
    ctx.setFillStyle('rgba(4,7,14,0.80)');
    ctx.fillRect(0, 0, W, H);
  },

  drawReady: function (ctx) {
    this.drawMask(ctx);

    ctx.setTextAlign('center');
    ctx.setFontSize(74);
    ctx.setFillStyle('#eaf6ff');
    ctx.fillText('星 海 突 击', 640, 250);
    ctx.setFontSize(24);
    ctx.setFillStyle('#6f93b8');
    ctx.fillText('竖 版 飞 机 大 战', 640, 292);

    ctx.setFillStyle('#0d1726');
    ctx.fillRect(330, 340, 620, 190);
    ctx.setStrokeStyle('#1e334d');
    ctx.setLineWidth(2);
    ctx.strokeRect(330, 340, 620, 190);

    ctx.setFontSize(20);
    ctx.setFillStyle('#9db8d4');
    ctx.fillText('左摇杆 / 方向键  移动（自动开火）', 640, 382);
    ctx.fillText('A / X  清屏炸弹　·　START  暂停', 640, 418);
    ctx.fillText('击破敌机掉落火力 / 炸弹 / 维修补给', 640, 454);
    ctx.fillText('每 40 秒 BOSS 来袭，弹幕会随血量变密', 640, 490);

    ctx.setFontSize(28);
    ctx.setFillStyle('#ffd166');
    ctx.fillText('按 A 或点击屏幕 开始', 640, 592);
    ctx.setFontSize(17);
    ctx.setFillStyle('#4f647e');
    ctx.fillText('最高分 ' + this.best + ' · 触屏可直接拖拽飞船', 640, 630);
    ctx.setTextAlign('left');
  },

  drawPaused: function (ctx) {
    this.drawMask(ctx);
    ctx.setFillStyle('#0d1726');
    ctx.fillRect(400, 240, 480, 230);
    ctx.setStrokeStyle('#24405f');
    ctx.setLineWidth(3);
    ctx.strokeRect(400, 240, 480, 230);

    ctx.setTextAlign('center');
    ctx.setFontSize(48);
    ctx.setFillStyle('#8fd0ff');
    ctx.fillText('已暂停', 640, 322);
    ctx.setFontSize(22);
    ctx.setFillStyle('#9db8d4');
    ctx.fillText('按 START 或点击屏幕继续', 640, 376);
    ctx.setFontSize(19);
    ctx.setFillStyle('#5c7695');
    ctx.fillText('得分 ' + this.score + ' · 第 ' + this.level + ' 波', 640, 420);
    ctx.setTextAlign('left');
  },

  drawOver: function (ctx) {
    this.drawMask(ctx);
    ctx.setFillStyle('#170d14');
    ctx.fillRect(350, 200, 580, 320);
    ctx.setStrokeStyle('#7f2b45');
    ctx.setLineWidth(3);
    ctx.strokeRect(350, 200, 580, 320);

    ctx.setTextAlign('center');
    ctx.setFontSize(56);
    ctx.setFillStyle('#ff7a8a');
    ctx.fillText('机体损毁', 640, 286);
    ctx.setFontSize(26);
    ctx.setFillStyle('#eaf6ff');
    ctx.fillText('本局 ' + this.score + ' 分 · 最高 ' + this.best + ' 分', 640, 344);
    ctx.setFontSize(20);
    ctx.setFillStyle('#a08aa8');
    ctx.fillText('击破敌机 ' + this.kills + ' 架 · 抵达第 ' + this.level + ' 波', 640, 384);
    ctx.setFontSize(24);
    ctx.setFillStyle('#ffd166');
    ctx.fillText('按 A / 点击屏幕 再来一局', 640, 452);
    ctx.setFontSize(17);
    ctx.setFillStyle('#6b5a6b');
    ctx.fillText('BACK 重开 · B 返回', 640, 488);
    ctx.setTextAlign('left');
  }
});
