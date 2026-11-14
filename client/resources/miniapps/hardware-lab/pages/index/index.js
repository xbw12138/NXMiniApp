const DEG = 180 / Math.PI;
const IR_W = 320;
const IR_H = 240;

function num(value, fallback) {
  return typeof value === 'number' && isFinite(value) ? value : fallback;
}

function fmt(value) {
  const v = num(value, 0);
  return (v >= 0 ? '+' : '') + v.toFixed(2);
}

function safeState(api, fallback) {
  try {
    return api && api.state ? api.state() : fallback;
  } catch (err) {
    return fallback;
  }
}

Page({
  data: {
    motionText: '等待六轴数据…',
    gyroText: '',
    accelText: '',
    angleText: '',
    irStatus: '未启动',
    gestureText: '把右 Joy-Con 拆下来，对准手部',
    rumbleText: '自定义频率 / 振幅 / 时长',
    nfcStatus: '未启动',
    nfcInfo: '',
    nfcArea: ''
  },

  onLoad() {
    this.ctxIr = wx.createCanvasContext('ir');
    this.ctxLevel = wx.createCanvasContext('level');
    this.motion = { gyro: [0, 0, 0], accel: [0, 0, 0], angle: [0, 0, 0] };
    this.irMode = '';
    this.irHand = null;
    this.nfcMounted = false;
    this.morseTimers = [];

    this.dataTimer = setInterval(() => this.poll(), 120);
    this.irTimer = setInterval(() => this.drawIr(), 40);
    this.levelTimer = setInterval(() => this.drawLevel(), 50);
    this.poll();
    this.drawLevel();
  },

  onUnload() {
    if (this.dataTimer) clearInterval(this.dataTimer);
    if (this.irTimer) clearInterval(this.irTimer);
    if (this.levelTimer) clearInterval(this.levelTimer);
    this.clearMorse();
    wx.nx.ir.stop();
    wx.nx.nfc.stop();
    wx.nx.rumble.stop();
  },

  poll() {
    const nx = wx.nx;
    const motion = safeState(nx.motion, {});
    const ir = safeState(nx.ir, {});
    const nfc = safeState(nx.nfc, {});

    this.updateMotion(motion);
    this.updateIr(ir);
    this.updateNfc(nfc);
  },

  updateMotion(motion) {
    const gyro = motion.gyro || [0, 0, 0];
    const accel = motion.accel || [0, 0, 0];
    const angle = motion.angle || [0, 0, 0];
    this.motion = { gyro, accel, angle };

    let text = '设备不可用';
    if (motion.supported === false) text = '桌面构建无传感器';
    else if (motion.connected) text = '已连接 · ' + num(motion.count, 0) + ' 个传感器';
    else text = '等待 Joy-Con 连接';

    this.setData({
      motionText: text,
      gyroText: '陀螺仪 ' + fmt(gyro[0]) + '  ' + fmt(gyro[1]) + '  ' + fmt(gyro[2]) + ' rad/s',
      accelText: '加速度 ' + fmt(accel[0]) + '  ' + fmt(accel[1]) + '  ' + fmt(accel[2]) + ' g',
      angleText: '姿态 ' + (num(angle[0], 0) * DEG).toFixed(1) + '°  ' +
        (num(angle[1], 0) * DEG).toFixed(1) + '°  ' +
        (num(angle[2], 0) * DEG).toFixed(1) + '°'
    });
  },

  updateIr(ir) {
    let status = '未启动';
    if (ir.error) status = '不可用：' + ir.error;
    else if (ir.running && ir.mode === 'image') status = '画面 ' + num(ir.width, 0) + '×' + num(ir.height, 0);
    else if (ir.running && ir.mode === 'gesture') status = '手势检测中';
    else if (ir.running) status = '运行中';

    this.irRunning = !!ir.running;
    this.irMode = ir.running ? (ir.mode || 'image') : '';

    let gestureText = '把右 Joy-Con 拆下来，对准手部';
    if (ir.mode === 'gesture' && ir.running) {
      const hand = ir.hand || {};
      if (!hand.pixels) {
        gestureText = '未检测到物体';
        this.irHand = null;
      } else {
        const density = num(hand.density, 0);
        const pixels = num(hand.pixels, 0);
        const gesture = pixels < 900 ? '距离较远' : (density > 0.62 ? '握拳' : '张开手掌');
        gestureText = gesture + ' · ' + pixels + ' px · 密度 ' + density.toFixed(2);
        this.irHand = hand;
      }
    } else {
      this.irHand = null;
    }

    this.setData({ irStatus: status, gestureText });
  },

  updateNfc(nfc) {
    let status = '未启动';
    if (nfc.error) status = '不可用：' + nfc.error;
    else if (nfc.state === 'unavailable') status = '未找到 NFC 读卡器';
    else if (nfc.state === 'searching') status = '请把 Amiibo 贴到右 Joy-Con';
    else if (nfc.state === 'found') status = '已发现卡片，正在读取…';
    else if (nfc.state === 'mounted') status = '已读取 Amiibo';
    else if (nfc.running) status = '扫描中';

    if (nfc.mounted && nfc.writeOk === true) status = '存档已写回';
    if (nfc.mounted && nfc.writeOk === false && nfc.error) status = '写回失败：' + nfc.error;

    if (nfc.state === 'found' && !this.nfcMounted) {
      this.nfcMounted = true;
      wx.nx.nfc.mount();
    }
    if (!nfc.running) this.nfcMounted = false;

    const amiibo = nfc.amiibo || {};
    let info = '';
    if (nfc.state === 'mounted') {
      const name = amiibo.name ? amiibo.name : '(未命名)';
      info = name + ' · 角色 ' + num(amiibo.characterId, 0) +
        ' · 系列 ' + num(amiibo.seriesId, 0) +
        ' · 编号 ' + num(amiibo.numberingId, 0);
    } else if (nfc.uid) {
      info = 'UID ' + nfc.uid;
    }

    const area = amiibo.appArea || '';
    this.lastAreaHex = area;
    let areaText = '';
    if (area) {
      areaText = '存档区 ' + num(amiibo.appAreaSize, 0) + ' 字节 · ' + area.slice(0, 48) +
        (area.length > 48 ? '…' : '');
    }

    this.setData({ nfcStatus: status, nfcInfo: info, nfcArea: areaText });
  },

  drawLevel() {
    const ctx = this.ctxLevel;
    const ax = num(this.motion.accel[0], 0);
    const ay = num(this.motion.accel[1], 0);
    const cx = 130;
    const cy = 75;
    const radius = 58;

    ctx.setFillStyle('#0b0e12');
    ctx.fillRect(0, 0, 260, 150);

    ctx.setStrokeStyle('#2b333d');
    ctx.setLineWidth(2);
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(cx - radius, cy);
    ctx.lineTo(cx + radius, cy);
    ctx.moveTo(cx, cy - radius);
    ctx.lineTo(cx, cy + radius);
    ctx.stroke();

    const maxTilt = 0.6;
    const bx = cx + Math.max(-1, Math.min(1, ay / maxTilt)) * radius;
    const by = cy - Math.max(-1, Math.min(1, ax / maxTilt)) * radius;

    ctx.setFillStyle('#4cc38a');
    ctx.beginPath();
    ctx.arc(bx, by, 9, 0, Math.PI * 2);
    ctx.fill();
    ctx.draw();
  },

  drawIr() {
    if (!this.irRunning) return;
    const ctx = this.ctxIr;
    if (this.irMode === 'image') {
      ctx.drawIrCamera(0, 0, IR_W, IR_H);
      ctx.draw();
      return;
    }
    if (this.irMode === 'gesture') {
      ctx.setFillStyle('#000000');
      ctx.fillRect(0, 0, IR_W, IR_H);
      const hand = this.irHand;
      if (hand && hand.width) {
        ctx.setStrokeStyle('#4cc38a');
        ctx.setLineWidth(3);
        ctx.strokeRect(num(hand.x, 0), num(hand.y, 0), num(hand.width, 0), num(hand.height, 0));
        ctx.setFillStyle('#4cc38a');
        ctx.fillRect(num(hand.centroidX, 0) - 3, num(hand.centroidY, 0) - 3, 6, 6);
      }
      ctx.draw();
    }
  },

  onIrImage() {
    this.irMode = 'image';
    this.irRunning = true;
    wx.nx.ir.start('image');
    this.setData({ irStatus: '启动红外画面…' });
  },

  onIrGesture() {
    this.irMode = 'gesture';
    this.irRunning = true;
    wx.nx.ir.start('gesture');
    this.setData({ irStatus: '启动手势检测…' });
  },

  onIrStop() {
    this.irRunning = false;
    this.irMode = '';
    this.irHand = null;
    wx.nx.ir.stop();
    this.setData({ irStatus: '已停止', gestureText: '把右 Joy-Con 拆下来，对准手部' });
    this.drawIr();
  },

  onRumbleLow() {
    wx.nx.rumble.play({ lowFreq: 90, highFreq: 90, lowAmp: 1, highAmp: 0.2, duration: 700 });
    this.setData({ rumbleText: '低频 90Hz · 振幅 1.0 · 700ms' });
  },

  onRumbleHigh() {
    wx.nx.rumble.play({ lowFreq: 900, highFreq: 700, lowAmp: 0.2, highAmp: 1, duration: 700 });
    this.setData({ rumbleText: '高频 700~900Hz · 振幅 1.0 · 700ms' });
  },

  onRumbleStop() {
    this.clearMorse();
    wx.nx.rumble.stop();
    this.setData({ rumbleText: '已停止' });
  },

  onRumbleMorse() {
    this.clearMorse();
    // SOS：三短、三长、三短；用连续 play 拼出节奏。
    const pattern = [1, 1, 1, 3, 3, 3, 1, 1, 1];
    const unit = 110;
    let delay = 0;
    this.setData({ rumbleText: '摩尔斯 SOS 播放中…' });
    pattern.forEach((length) => {
      const duration = length * unit;
      const timer = setTimeout(() => {
        wx.nx.rumble.play({
          lowFreq: 240,
          highFreq: 520,
          lowAmp: 1,
          highAmp: 1,
          duration: duration - 30
        });
      }, delay);
      this.morseTimers.push(timer);
      delay += duration + unit;
    });
    const endTimer = setTimeout(() => {
      this.setData({ rumbleText: '摩尔斯 SOS 完成' });
    }, delay);
    this.morseTimers.push(endTimer);
  },

  clearMorse() {
    this.morseTimers.forEach((timer) => clearTimeout(timer));
    this.morseTimers = [];
  },

  onNfcStart() {
    this.nfcMounted = false;
    wx.nx.nfc.start();
    this.setData({ nfcStatus: '启动扫描…', nfcInfo: '', nfcArea: '' });
  },

  onNfcStop() {
    this.nfcMounted = false;
    wx.nx.nfc.stop();
    this.setData({ nfcStatus: '已停止', nfcInfo: '', nfcArea: '' });
  },

  onNfcWrite() {
    if (!this.lastAreaHex) {
      wx.showToast({ title: '请先读取 Amiibo', icon: 'none' });
      return;
    }
    wx.nx.nfc.writeAppArea(this.lastAreaHex);
    this.setData({ nfcStatus: '正在写回存档…' });
  }
});
