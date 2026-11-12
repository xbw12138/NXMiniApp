var ROWS = [
  [
    { label: 'DEG', act: 'deg', cls: 'key key-fn' },
    { label: 'sin', act: 'fn', val: 'sin(', cls: 'key key-fn' },
    { label: 'cos', act: 'fn', val: 'cos(', cls: 'key key-fn' },
    { label: 'tan', act: 'fn', val: 'tan(', cls: 'key key-fn' },
    { label: 'C', act: 'clear', cls: 'key key-clear' }
  ],
  [
    { label: '(', act: 'raw', val: '(', cls: 'key key-fn' },
    { label: ')', act: 'raw', val: ')', cls: 'key key-fn' },
    { label: 'x^y', act: 'raw', val: '^', cls: 'key key-fn' },
    { label: '√', act: 'fn', val: 'sqrt(', cls: 'key key-fn' },
    { label: '⌫', act: 'back', cls: 'key key-fn' }
  ],
  [
    { label: '7', act: 'raw', val: '7', cls: 'key' },
    { label: '8', act: 'raw', val: '8', cls: 'key' },
    { label: '9', act: 'raw', val: '9', cls: 'key' },
    { label: '÷', act: 'op', val: '÷', cls: 'key key-op' },
    { label: 'π', act: 'raw', val: 'π', cls: 'key key-fn' }
  ],
  [
    { label: '4', act: 'raw', val: '4', cls: 'key' },
    { label: '5', act: 'raw', val: '5', cls: 'key' },
    { label: '6', act: 'raw', val: '6', cls: 'key' },
    { label: '×', act: 'op', val: '×', cls: 'key key-op' },
    { label: 'ln', act: 'fn', val: 'ln(', cls: 'key key-fn' }
  ],
  [
    { label: '1', act: 'raw', val: '1', cls: 'key' },
    { label: '2', act: 'raw', val: '2', cls: 'key' },
    { label: '3', act: 'raw', val: '3', cls: 'key' },
    { label: '−', act: 'op', val: '−', cls: 'key key-op' },
    { label: 'log', act: 'fn', val: 'log(', cls: 'key key-fn' }
  ],
  [
    { label: '0', act: 'raw', val: '0', cls: 'key' },
    { label: '.', act: 'raw', val: '.', cls: 'key' },
    { label: '%', act: 'op', val: '%', cls: 'key key-op' },
    { label: '+', act: 'op', val: '+', cls: 'key key-op' },
    { label: '=', act: 'eq', cls: 'key key-eq' }
  ]
];

// 角度模式切换时键帽文字随之变化（DEG / RAD）。
function buildRows(deg) {
  var rows = [];
  for (var i = 0; i < ROWS.length; i++) {
    var src = ROWS[i];
    var row = [];
    for (var j = 0; j < src.length; j++) {
      var k = src[j];
      if (k.act === 'deg') {
        row.push({ label: deg ? 'DEG' : 'RAD', act: k.act, val: k.val, cls: k.cls });
      } else {
        row.push(k);
      }
    }
    rows.push(row);
  }
  return rows;
}

function normalize(expr) {
  var s = expr;
  s = s.split('÷').join('/');
  s = s.split('×').join('*');
  s = s.split('−').join('-');
  s = s.split('π').join('3.141592653589793');
  // 支持隐式乘法： 2(3+4) 、(1+2)3 、(1+2)(3+4)
  s = s.replace(/(\d)\(/g, '$1*(');
  s = s.replace(/\)(\d)/g, ')*$1');
  s = s.replace(/\)\(/g, ')*(');
  return s;
}

function tokenize(s) {
  var tokens = [];
  var i = 0;
  while (i < s.length) {
    var ch = s.charAt(i);
    if (ch === ' ') { i++; continue; }
    if ((ch >= '0' && ch <= '9') || ch === '.') {
      var j = i;
      while (j < s.length && ((s.charAt(j) >= '0' && s.charAt(j) <= '9') || s.charAt(j) === '.')) j++;
      tokens.push({ t: 'num', v: parseFloat(s.slice(i, j)) });
      i = j;
      continue;
    }
    if (ch >= 'a' && ch <= 'z') {
      var k = i;
      while (k < s.length && s.charAt(k) >= 'a' && s.charAt(k) <= 'z') k++;
      tokens.push({ t: 'id', v: s.slice(i, k) });
      i = k;
      continue;
    }
    tokens.push({ t: ch });
    i++;
  }
  return tokens;
}

function evaluate(expr, deg) {
  var tokens = tokenize(normalize(expr));
  var pos = 0;

  function peek() { return tokens[pos]; }
  function eat(t) {
    if (pos < tokens.length && tokens[pos].t === t) {
      pos++;
      return true;
    }
    return false;
  }

  function parseExpr() {
    var v = parseTerm();
    while (pos < tokens.length && (peek().t === '+' || peek().t === '-')) {
      var op = tokens[pos].t;
      pos++;
      var r = parseTerm();
      v = op === '+' ? v + r : v - r;
    }
    return v;
  }

  function parseTerm() {
    var v = parseUnary();
    while (pos < tokens.length && (peek().t === '*' || peek().t === '/' || peek().t === '%')) {
      var op = tokens[pos].t;
      pos++;
      var r = parseUnary();
      if (op === '*') v = v * r;
      else if (op === '/') {
        if (r === 0) throw new Error('除数不能为 0');
        v = v / r;
      } else v = v % r;
    }
    return v;
  }

  function parseUnary() {
    if (peek() && peek().t === '-') {
      pos++;
      return -parseUnary();
    }
    if (peek() && peek().t === '+') {
      pos++;
      return parseUnary();
    }
    return parsePower();
  }

  function parsePower() {
    var base = parseAtom();
    if (peek() && peek().t === '^') {
      pos++;
      return Math.pow(base, parseUnary());
    }
    return base;
  }

  function parseAtom() {
    var tk = peek();
    if (!tk) throw new Error('表达式不完整');
    if (tk.t === 'num') {
      pos++;
      return tk.v;
    }
    if (tk.t === 'id') {
      var name = tk.v;
      pos++;
      if (name === 'pi') return Math.PI;
      if (!eat('(')) throw new Error('缺少括号');
      var arg = parseExpr();
      if (!eat(')')) throw new Error('括号不匹配');
      if (name === 'sin') return Math.sin(deg ? arg * Math.PI / 180 : arg);
      if (name === 'cos') return Math.cos(deg ? arg * Math.PI / 180 : arg);
      if (name === 'tan') return Math.tan(deg ? arg * Math.PI / 180 : arg);
      if (name === 'sqrt') {
        if (arg < 0) throw new Error('负数不能开方');
        return Math.sqrt(arg);
      }
      if (name === 'ln') {
        if (arg <= 0) throw new Error('对数参数需大于 0');
        return Math.log(arg);
      }
      if (name === 'log') {
        if (arg <= 0) throw new Error('对数参数需大于 0');
        return Math.log(arg) / Math.LN10;
      }
      throw new Error('未知函数 ' + name);
    }
    if (tk.t === '(') {
      pos++;
      var v = parseExpr();
      if (!eat(')')) throw new Error('括号不匹配');
      return v;
    }
    throw new Error('无法解析的符号');
  }

  var value = parseExpr();
  if (pos < tokens.length) throw new Error('表达式有误');
  if (!isFinite(value)) throw new Error('结果超出范围');
  return value;
}

function formatValue(v) {
  if (Math.abs(v) >= 1e12 || (Math.abs(v) < 1e-9 && v !== 0)) {
    return v.toExponential(6);
  }
  var r = Math.round(v * 1e10) / 1e10;
  var s = '' + r;
  if (s.indexOf('e') >= 0) return r.toExponential(6);
  return s;
}

Page({
  data: {
    rows: buildRows(true),
    expr: '',
    result: '0',
    isError: false,
    deg: true,
    prev: ''
  },

  onLoad() {
    this.justEvaluated = false;
    this.setData({ rows: buildRows(this.data.deg) });
  },

  onKey(e) {
    var ds = e.currentTarget.dataset;
    var act = ds.act;
    var val = ds.val;
    if (act === 'deg') {
      var deg = !this.data.deg;
      this.setData({ deg: deg, rows: buildRows(deg) });
      return;
    }
    if (act === 'clear') {
      this.setData({ expr: '', result: '0', isError: false, prev: '' });
      this.justEvaluated = false;
      return;
    }
    if (act === 'back') {
      var cur = this.data.expr;
      if (!cur.length) return;
      if (cur.slice(-4) === 'sqrt(') cur = cur.slice(0, -5);
      else if (cur.slice(-4) === 'sin(' || cur.slice(-4) === 'cos(' || cur.slice(-4) === 'tan(' || cur.slice(-4) === 'log(') cur = cur.slice(0, -4);
      else if (cur.slice(-3) === 'ln(') cur = cur.slice(0, -3);
      else cur = cur.slice(0, -1);
      this.setData({ expr: cur, isError: false });
      this.justEvaluated = false;
      return;
    }
    if (act === 'eq') {
      this.doEval();
      return;
    }
    var text = this.data.expr;
    if (this.justEvaluated) {
      if (act === 'raw' && (val === ')' || (val >= '0' && val <= '9') || val === '.' || val === 'π')) {
        text = '';
      } else {
        text = this.data.result;
        if (text === '错误') text = '';
      }
      this.justEvaluated = false;
    }
    var ops = ['÷', '×', '−', '+', '%', '^'];
    if (act === 'op' && ops.indexOf(val) >= 0) {
      if (!text.length) {
        if (val === '−') {
          text = '−';
        } else {
          wx.showToast({ title: '先输入数字', icon: 'none' });
          return;
        }
      } else {
        var last = text.slice(-1);
        if (ops.indexOf(last) >= 0) {
          text = text.slice(0, -1);
        }
      }
    }
    this.setData({ expr: text + val, isError: false });
    this.autoPreview();
  },

  autoPreview() {
    var expr = this.data.expr;
    if (!expr || expr.slice(-1) === '(' || '+-−×÷%^'.indexOf(expr.slice(-1)) >= 0) return;
    var open = 0;
    for (var i = 0; i < expr.length; i++) {
      if (expr.charAt(i) === '(') open++;
      if (expr.charAt(i) === ')') open--;
    }
    var probe = expr;
    while (open > 0) {
      probe += ')';
      open--;
    }
    try {
      var v = evaluate(probe, this.data.deg);
      this.setData({ result: formatValue(v), isError: false });
    } catch (err) {
      this.setData({ result: '…', isError: false });
    }
  },

  doEval() {
    var expr = this.data.expr;
    if (!expr) {
      wx.showToast({ title: '请输入表达式', icon: 'none' });
      return;
    }
    var open = 0;
    for (var i = 0; i < expr.length; i++) {
      if (expr.charAt(i) === '(') open++;
      if (expr.charAt(i) === ')') open--;
    }
    var full = expr;
    while (open > 0) {
      full += ')';
      open--;
    }
    try {
      var v = evaluate(full, this.data.deg);
      var text = formatValue(v);
      this.setData({
        expr: full,
        result: text,
        isError: false,
        prev: full + ' ='
      });
      this.justEvaluated = true;
    } catch (err) {
      this.setData({
        result: err.message || '表达式有误',
        isError: true
      });
    }
  }
});
