// Run with node --test tests/window-geometry.test.cjs, or pass QML paths to node.
// Executes the production QML functions/handlers with synthetic screen data.
// This is a geometry/state regression suite, not a native drag or frame benchmark.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const defaults = fs.existsSync(path.join(root, 'apps/qt/qml/App.qml'))
  ? ['apps/qt/qml/App.qml', 'qt-version/qml/App.qml']
  : ['apps/camera_debugger/Main.qml'];
const sources = process.argv.length > 2 ? process.argv.slice(2) : defaults.map(p => path.join(root, p));

function block(source, start) {
  let depth = 0;
  for (let i = start; i < source.length; ++i) {
    if (source[i] === '{') ++depth;
    if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error('unclosed production QML block');
}

for (const file of sources) {
  const source = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
  const functions = [...source.matchAll(/^    function \w+\([^\n]*\) \{/gm)].map(m =>
    source.slice(m.index, source.indexOf('{', m.index)) + block(source, source.indexOf('{', m.index)));
  function handler(name, indent = 4) {
    const match = new RegExp(`^ {${indent}}${name.replace('.', '\\.')}:[ \\t]*`, 'm').exec(source);
    assert.ok(match, `${file}: missing ${name}`);
    const start = match.index + match[0].length;
    return source[start] === '{' ? block(source, start) : source.slice(start, source.indexOf('\n', start));
  }
  function setup() {
    const screens = [0, 1440].map(x => ({virtualX: x, virtualY: 0, width: 1440, height: 900,
      desktopAvailableWidth: 1440, desktopAvailableHeight: 900}));
    const context = {Qt: {application: {screens}}, Window: {Windowed: 2}, visibility: 2,
      screen: screens[0], x: 100, y: 100, width: 800, height: 600,
      startupGeometryReady: true, lastNormalGeometry: null,
      startupGeometry: {savedX: 100, savedY: 100, savedWidth: 800, savedHeight: 600},
      minimumWidth: 0, minimumHeight: 0, timerRestarts: 0};
    context.normalGeometrySaveTimer = {restart() { ++context.timerRestarts; }};
    vm.createContext(context);
    vm.runInContext(functions.join('\n'), context);
    context.runHandler = (name, indent) => vm.runInContext(handler(name, indent), context);
    return context;
  }
  const geometry = c => [c.x, c.y, c.width, c.height];
  const saved = c => ['savedX', 'savedY', 'savedWidth', 'savedHeight'].map(k => c.startupGeometry[k]);
  const changed = c => ['X', 'Y', 'Width', 'Height'].forEach(p => {
    if (new RegExp(`^    on${p}Changed:`, 'm').test(source)) c.runHandler(`on${p}Changed`);
  });
  const prefix = path.relative(root, file);

  test(`${prefix}: screen transition preserves a visible straddling drag`, () => {
    const c = setup();
    Object.assign(c, {x: 1050, y: 100, width: 800, height: 600, screen: c.Qt.application.screens[1]});
    c.runHandler('onScreenChanged');
    assert.deepEqual(geometry(c), [1050, 100, 800, 600]);
  });
  test(`${prefix}: a removed display's unreachable rectangle is recovered`, () => {
    const c = setup();
    c.Qt.application.screens.pop();
    c.x = 1700;
    c.runHandler('onScreenChanged');
    assert.ok(c.x >= 0 && c.x + c.width <= 1440);
    assert.deepEqual([c.width, c.height], [800, 600]);
  });
  test(`${prefix}: screen signals do not reshape hidden or maximized windows`, () => {
    for (const visibility of [0, 3, 4, 5]) {
      const c = setup();
      c.visibility = visibility;
      c.x = 1700;
      c.runHandler('onScreenChanged');
      assert.equal(c.x, 1700);
    }
  });
  test(`${prefix}: close after resize and state change retains the normal rectangle`, () => {
    for (const visibility of [0, 3, 4, 5]) {
      const c = setup();
      c.runHandler('Component.onCompleted');
      Object.assign(c, {x: 120, y: 110, width: 1100, height: 700});
      changed(c);
      // The state change overtakes WindowBase's 250 ms save timer.
      assert.deepEqual(saved(c), [100, 100, 800, 600], 'move/resize only updates memory');
      Object.assign(c, {visibility, x: 0, y: 0, width: 1440, height: 900});
      // UniVision's original source has no geometry-change handlers. Handle
      // those only when present so this test also reproduces its close bug.
      if (/^    onWidthChanged:/m.test(source)) changed(c);
      c.runHandler('onClosing');
      assert.deepEqual(saved(c), [120, 110, 1100, 700]);
    }
  });
  test(`${prefix}: a normal close saves the current rectangle`, () => {
    const c = setup();
    c.runHandler('Component.onCompleted');
    Object.assign(c, {x: 80, y: 70, width: 1200, height: 750});
    c.runHandler('onClosing');
    assert.deepEqual(saved(c), [80, 70, 1200, 750]);
  });
  test(`${prefix}: valid saved sizes and compact startup behavior remain intact`, () => {
    const c = setup();
    c.startupGeometry = {savedX: 0, savedY: 0, savedWidth: 1400, savedHeight: 850};
    c.runHandler('Component.onCompleted');
    assert.deepEqual(geometry(c), [0, 0, 1400, 850]);
    c.startupGeometry = {savedX: -100000, savedY: -100000, savedWidth: 0, savedHeight: 0};
    c.Qt.application.screens = [{virtualX: -800, virtualY: 0, width: 800, height: 550,
      desktopAvailableWidth: 800, desktopAvailableHeight: 550}];
    c.screen = c.Qt.application.screens[0];
    c.runHandler('Component.onCompleted');
    assert.ok(c.x >= -800 && c.x + c.width <= 0);
    assert.ok(c.y >= 0 && c.y + c.height <= 550);
  });
  test(`${prefix}: startup and empty display signals are harmless`, () => {
    const c = setup();
    c.startupGeometryReady = false;
    c.x = 1050;
    c.runHandler('onScreenChanged');
    assert.equal(c.x, 1050);
    c.startupGeometryReady = true;
    c.Qt.application.screens = [];
    c.runHandler('onScreenChanged');
    assert.equal(c.x, 1050);
  });
  if (/normalGeometrySaveTimer/.test(source)) {
    test(`${prefix}: debounce saves the cached normal size after maximize`, () => {
      const c = setup();
      c.runHandler('Component.onCompleted');
      c.width = 1000;
      c.runHandler('onWidthChanged');
      assert.equal(c.timerRestarts, 1);
      c.visibility = 4;
      c.width = 1440;
      c.runHandler('onWidthChanged');
      c.runHandler('onTriggered', 8);
      assert.deepEqual(saved(c), [100, 100, 1000, 600]);
    });
  }
}
