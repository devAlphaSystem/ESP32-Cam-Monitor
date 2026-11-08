#include <Arduino.h>

namespace CameraWatchdog {

extern const char WIFI_SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#24262a">
<link rel="icon" href="data:,">
<title>Wi-Fi · Camera Watchdog</title>
<style>
*,*::before,*::after{box-sizing:border-box}
:root{
  --bg:#18191b;--surface:#202225;--surface-alt:#24262a;--surface-hover:#2d3035;
  --line:#34373d;--line-strong:#626973;--text:#e2e4e7;--muted:#b4bac3;--dim:#969ea9;
  --ok:#80b99a;--warn:#d4b078;--bad:#dc949b;--info:#91b4d5;--accent:#8ab0dc;
  --button-primary:#355b85;--button-primary-hover:#406b9c;--on-primary:#f0f3f7;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  --sans:"Segoe UI",system-ui,-apple-system,BlinkMacSystemFont,sans-serif;
}
html{-webkit-text-size-adjust:100%;color-scheme:dark}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 var(--sans);-webkit-font-smoothing:antialiased}
button,input{font:inherit}
button,a,input{-webkit-tap-highlight-color:transparent}
button{touch-action:manipulation}
h1,h2,p{margin:0}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
main{max-width:520px;margin:0 auto;padding:max(20px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(32px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
.brand{margin-bottom:20px}
.brand h1{font-size:20px;font-weight:600;letter-spacing:-.02em;line-height:1.25}
.brand p{color:var(--muted);font-size:13px}
.card{margin-top:16px;padding:18px;border:1px solid var(--line);border-radius:6px;background:var(--surface)}
.card h2{font-size:15px;font-weight:600;margin-bottom:4px}
.hint{color:var(--muted);font-size:13px;line-height:1.55}
.status{display:flex;align-items:center;gap:12px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);flex:none}
.status.on .dot{background:var(--ok)}
.status b{display:block;font-weight:600;overflow-wrap:anywhere}
.status>div{flex:1;min-width:0}
.status small{display:block;color:var(--muted);font-size:12px}
.notice{margin-top:12px;padding:10px 12px;border-left:2px solid var(--info);background:var(--surface-alt);color:var(--muted);font-size:13px;line-height:1.55}
.notice.warn{border-left-color:var(--warn)}
.notice.bad{border-left-color:var(--bad);color:var(--text)}
.head-row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:44px;padding:0 16px;border:1px solid var(--line-strong);border-radius:4px;background:var(--surface);color:var(--text);font-weight:500;cursor:pointer;text-decoration:none}
.btn:hover{background:var(--surface-hover)}
.btn:disabled{opacity:.5;cursor:not-allowed}
.btn.primary{width:100%;background:var(--button-primary);border-color:#6f98c7;color:var(--on-primary)}
.btn.primary:hover:not(:disabled){background:var(--button-primary-hover)}
.btn.small{min-height:36px;padding:0 12px;font-size:13px}
.btn.link{border:0;background:none;color:var(--accent);padding:0 4px;min-height:36px}
.spin{width:14px;height:14px;border:2px solid var(--line-strong);border-top-color:var(--accent);border-radius:50%;animation:turn .8s linear infinite}
@keyframes turn{to{transform:rotate(360deg)}}
.networks{display:flex;flex-direction:column;gap:6px;margin:0;padding:0;list-style:none}
.net{display:flex;align-items:center;gap:12px;width:100%;min-height:52px;padding:8px 12px;border:1px solid var(--line);border-radius:4px;background:var(--surface-alt);color:var(--text);text-align:left;cursor:pointer}
.net:hover{background:var(--surface-hover)}
.net[aria-pressed="true"]{border-color:var(--accent);background:#25303d}
.net-name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500}
.net-meta{display:flex;align-items:center;gap:8px;flex:none;color:var(--dim);font-size:12px}
.tag{padding:1px 6px;border-radius:3px;background:#26323e;color:var(--info);font-size:11px;font-weight:500}
.tag.ok{background:#24352c;color:var(--ok)}
.lock{width:14px;height:14px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:16px;flex:none}
.bars i{width:4px;background:var(--line-strong);border-radius:1px}
.bars i:nth-child(1){height:5px}.bars i:nth-child(2){height:8px}.bars i:nth-child(3){height:12px}.bars i:nth-child(4){height:16px}
.bars.l1 i:nth-child(-n+1){background:var(--bad)}
.bars.l2 i:nth-child(-n+2){background:var(--warn)}
.bars.l3 i:nth-child(-n+3),.bars.l4 i:nth-child(-n+4){background:var(--ok)}
.empty{padding:20px 8px;text-align:center;color:var(--dim);font-size:14px}
.field{margin-top:14px}
.field label{display:block;margin-bottom:6px;font-size:13px;font-weight:500}
.field input{width:100%;height:46px;padding:0 12px;border:1px solid var(--line-strong);border-radius:4px;background:var(--surface);color:var(--text);font-size:16px}
.field input:focus{border-color:var(--accent)}
.pw{position:relative}
.pw input{padding-right:84px}
.pw button{position:absolute;right:4px;top:4px;height:38px;padding:0 12px;border:0;border-radius:3px;background:var(--surface-alt);color:var(--accent);font-size:13px;font-weight:500;cursor:pointer}
.sel-name{display:flex;align-items:center;gap:8px;margin-bottom:2px;font-weight:600;overflow-wrap:anywhere}
.actions{margin-top:16px;display:flex;flex-direction:column;gap:10px}
.progress{height:4px;margin:16px 0 12px;background:var(--line);border-radius:2px;overflow:hidden}
.progress i{display:block;width:40%;height:100%;background:var(--accent);animation:slide 1.3s ease-in-out infinite}
@keyframes slide{0%{margin-left:-40%}100%{margin-left:100%}}
.result-icon{display:flex;align-items:center;gap:10px;font-size:17px;font-weight:600}
.result-icon svg{width:26px;height:26px;fill:none;stroke-width:2.4;stroke-linecap:round;stroke-linejoin:round;flex:none}
.result-icon.ok{color:var(--ok)}.result-icon.ok svg{stroke:var(--ok)}
.result-icon.bad{color:var(--bad)}.result-icon.bad svg{stroke:var(--bad)}
.meter{height:6px;margin:6px 0 4px;background:var(--line);border-radius:3px;overflow:hidden}
.meter i{display:block;height:100%;background:var(--ok)}
.meter i.weak{background:var(--warn)}.meter i.very_weak{background:var(--bad)}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:14px 0 0;font-size:14px}
dt{color:var(--muted)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
.mono{font-family:var(--mono);font-size:13px}
.foot{margin-top:20px;text-align:center;color:var(--dim);font-size:12px}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important}}
</style>
</head>
<body>
<main>
  <div class="brand">
    <div><h1>Camera Watchdog</h1><p>Wi-Fi setup</p></div>
  </div>

  <section class="card" id="statusCard" aria-live="polite">
    <div class="status" id="statusRow"><span class="dot"></span><div><b id="statusTitle">Checking…</b><small id="statusSub"></small></div><span class="bars" id="statusBars" hidden><i></i><i></i><i></i><i></i></span></div>
    <div class="notice" id="viaNotice" hidden></div>
    <div class="notice warn" id="savedRow" hidden><span id="savedText"></span> <button class="btn link" type="button" id="forgetBtn">Forget saved network</button></div>
  </section>

  <section class="card" id="pickCard">
    <div class="head-row"><h2>1. Choose the network</h2><button class="btn small" type="button" id="rescanBtn"><span class="spin" id="scanSpin" hidden></span><span id="rescanText">Scan again</span></button></div>
    <ul class="networks" id="netList" aria-label="Wi-Fi networks found"></ul>
    <div class="empty" id="netEmpty">Looking for nearby networks…</div>
    <button class="btn link" type="button" id="manualBtn">Enter the network name</button>
  </section>

  <section class="card" id="formCard" hidden>
    <h2>2. Network password</h2>
    <div class="sel-name" id="selName"></div>
    <p class="hint" id="selHint"></p>
    <form id="connectForm" autocomplete="off">
      <div class="field" id="ssidField" hidden><label for="ssidInput">Network name</label><input id="ssidInput" type="text" maxlength="32" autocapitalize="none" autocorrect="off" spellcheck="false"></div>
      <div class="field" id="pwField"><label for="pwInput">Wi-Fi password</label><div class="pw"><input id="pwInput" type="password" maxlength="63" autocapitalize="none" autocorrect="off" spellcheck="false" autocomplete="new-password"><button type="button" id="pwToggle" aria-pressed="false">Show</button></div></div>
      <div class="field" id="adminField" hidden><label for="adminInput">Dashboard admin password</label><input id="adminInput" type="password" maxlength="64" autocomplete="off"></div>
      <div class="notice bad" id="formError" role="alert" hidden></div>
      <div class="actions"><button class="btn primary" type="submit" id="connectBtn">Test and connect</button></div>
      <p class="hint" style="margin-top:10px">The ESP32 tests the connection and signal before saving, and it only stores a network that works.</p>
    </form>
  </section>

  <section class="card" id="progressCard" hidden aria-live="polite">
    <h2 id="progressTitle">Testing the connection…</h2>
    <div class="progress" aria-hidden="true"><i></i></div>
    <p class="hint" id="progressText"></p>
  </section>

  <section class="card" id="resultCard" hidden aria-live="polite">
    <div class="result-icon ok" id="resultHead"></div>
    <p class="hint" id="resultText" style="margin-top:8px"></p>
    <div id="resultDetails" hidden>
      <div class="meter" id="resultMeter"><i id="resultMeterFill"></i></div>
      <dl>
        <dt>Signal</dt><dd id="rSignal"></dd>
        <dt>IP address</dt><dd class="mono" id="rIp"></dd>
        <dt>Channel</dt><dd id="rChannel"></dd>
      </dl>
    </div>
    <div class="notice warn" id="resultWarn" hidden></div>
    <div class="notice" id="resultNext" hidden></div>
    <div class="actions"><button class="btn primary" type="button" id="resultAgain" hidden>Try again</button><a class="btn" id="resultOpen" href="/" hidden>Open the dashboard</a></div>
  </section>

  <p class="foot" id="foot"></p>
</main>
<script>
'use strict';
const $ = id => document.getElementById(id);
const FLAG = 'wd_wifi_test';
const QUALITY = { excellent: 'Excellent', good: 'Good', fair: 'Fair', weak: 'Weak', very_weak: 'Very weak' };
const MESSAGES = {
  network_not_found: 'We could not find that network. Check the name and move the ESP32 closer to the router.',
  wrong_password: 'The password is incorrect. Check it and try again.',
  association_refused: 'The router refused the connection. It may have reached its device limit or use a MAC filter.',
  timeout: 'The connection took too long. Check the password and the signal, and try again.',
  dhcp_failed: 'Connected to the router, but it did not assign an IP address.',
  connect_failed: 'Could not connect to that network.',
  save_failed: 'The ESP32 connected, but it could not save the network to memory.',
  invalid_ssid: 'The network name must be 1 to 32 characters long.',
  invalid_password: 'The password must be 8 to 63 characters long.',
  busy: 'The ESP32 is busy with another test. Wait a moment.'
};
const app = { token: '', passwordRequired: false, networks: [], selected: null, manual: false, status: null, scanTimer: 0, pollTimer: 0, testing: false, failures: 0 };

async function api(path, options, timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs || 6000);
  try {
    const response = await fetch(path, Object.assign({ cache: 'no-store', signal: controller.signal }, options || {}));
    const data = await response.json().catch(() => ({}));
    return { ok: response.ok, status: response.status, data };
  } finally { clearTimeout(timer); }
}

function barsLevel(percent) { return percent >= 75 ? 4 : percent >= 50 ? 3 : percent >= 25 ? 2 : 1; }
function setBars(el, percent) { el.className = 'bars l' + barsLevel(percent); }
function show(id, on) { $(id).hidden = !on; }

function renderStatus(status) {
  app.status = status;
  app.token = status.security.writeToken;
  app.passwordRequired = status.security.passwordRequired;
  show('adminField', app.passwordRequired);
  const row = $('statusRow');
  row.classList.toggle('on', status.connected);
  if (status.connected) {
    $('statusTitle').textContent = 'Connected to ' + status.ssid;
    $('statusSub').textContent = 'IP ' + status.ip + ' · ' + status.rssi + ' dBm · channel ' + status.channel;
    setBars($('statusBars'), Math.max(0, Math.min(100, 2 * (status.rssi + 100))));
    show('statusBars', true);
  } else {
    $('statusTitle').textContent = 'No Wi-Fi connection';
    $('statusSub').textContent = status.saved.stored || status.saved.builtin ? 'The ESP32 keeps trying the known networks.' : 'No network configured yet.';
    show('statusBars', false);
  }
  const via = $('viaNotice');
  if (status.viaAccessPoint) {
    via.hidden = false;
    via.textContent = 'You are on the ESP32 setup network, ' + status.accessPoint.ssid + '. It closes by itself after you set up the Wi-Fi.';
  } else if (status.connected) {
    via.hidden = false;
    via.textContent = 'You are connected through the current network. When you switch networks, this page stops responding. Then open http://' + status.hostname + '.local/ on the new network.';
  } else { via.hidden = true; }
  const saved = $('savedRow');
  saved.hidden = !status.saved.stored;
  if (status.saved.stored) $('savedText').textContent = 'This ESP32 has the network ' + status.saved.ssid + ' saved.';
  $('foot').textContent = 'Firmware ' + status.firmware;
}

async function loadStatus() {
  try {
    const result = await api('/api/wifi/status');
    if (result.ok) { app.failures = 0; renderStatus(result.data); return result.data; }
  } catch (error) { app.failures++; }
  return null;
}

function renderNetworks() {
  const list = $('netList');
  list.replaceChildren();
  app.networks.forEach(network => {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'net';
    button.setAttribute('aria-pressed', app.selected && !app.manual && app.selected.ssid === network.ssid ? 'true' : 'false');
    const bars = document.createElement('span');
    bars.innerHTML = '<i></i><i></i><i></i><i></i>';
    setBars(bars, network.percent);
    const name = document.createElement('span');
    name.className = 'net-name';
    name.textContent = network.ssid;
    const meta = document.createElement('span');
    meta.className = 'net-meta';
    if (network.current) { const tag = document.createElement('span'); tag.className = 'tag ok'; tag.textContent = 'connected'; meta.appendChild(tag); }
    else if (network.saved) { const tag = document.createElement('span'); tag.className = 'tag'; tag.textContent = 'saved'; meta.appendChild(tag); }
    if (network.secure) meta.insertAdjacentHTML('beforeend', '<svg class="lock" viewBox="0 0 24 24" aria-label="password protected"><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/></svg>');
    button.append(bars, name, meta);
    button.addEventListener('click', () => selectNetwork(network));
    item.appendChild(button);
    list.appendChild(item);
  });
  $('netEmpty').hidden = app.networks.length > 0;
}

function setScanning(on) {
  show('scanSpin', on);
  $('rescanBtn').disabled = on;
  $('rescanText').textContent = on ? 'Scanning…' : 'Scan again';
}

async function scan(refresh) {
  clearTimeout(app.scanTimer);
  if (app.testing) return;
  setScanning(true);
  if (!app.networks.length) $('netEmpty').textContent = 'Looking for nearby networks…';
  try {
    const result = await api('/api/wifi/scan' + (refresh ? '?refresh=1' : ''), null, 8000);
    if (result.ok) {
      const data = result.data;
      if (data.state === 'scanning') { app.scanTimer = setTimeout(() => scan(false), 1200); return; }
      if (data.state === 'done') { app.networks = data.networks; renderNetworks(); }
      else if (data.state === 'failed') { $('netEmpty').textContent = 'Could not scan for networks right now. Tap "Scan again".'; }
      if (!app.networks.length && data.state === 'done') $('netEmpty').textContent = 'No networks found. Tap "Scan again".';
    }
  } catch (error) {
    app.scanTimer = setTimeout(() => scan(false), 2500);
    return;
  }
  setScanning(false);
}

function openForm(ssid, secure, manual) {
  app.selected = { ssid, secure };
  app.manual = manual;
  show('formCard', true);
  show('ssidField', manual);
  show('pwField', manual || secure);
  $('selName').textContent = manual ? 'Network entered manually' : ssid;
  $('selHint').textContent = manual ? 'Enter the exact network name, respecting uppercase and lowercase, and the password. Leave the password blank if the network is open.'
    : secure ? 'Enter the password for this network.' : 'Open network, no password. Tap Test and connect.';
  $('formError').hidden = true;
  $('pwInput').value = '';
  renderNetworks();
  $('formCard').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  setTimeout(() => (manual ? $('ssidInput') : secure ? $('pwInput') : $('connectBtn')).focus({ preventScroll: true }), 250);
}
function selectNetwork(network) { openForm(network.ssid, network.secure, false); }
$('manualBtn').addEventListener('click', () => { $('ssidInput').value = ''; openForm('', false, true); });
$('rescanBtn').addEventListener('click', () => scan(true));
$('pwToggle').addEventListener('click', () => {
  const input = $('pwInput');
  const reveal = input.type === 'password';
  input.type = reveal ? 'text' : 'password';
  $('pwToggle').textContent = reveal ? 'Hide' : 'Show';
  $('pwToggle').setAttribute('aria-pressed', reveal ? 'true' : 'false');
});

function describe(code, detail) { return MESSAGES[code] || detail || 'Could not complete. Try again.'; }

function showProgress(ssid) {
  show('pickCard', false); show('formCard', false); show('resultCard', false); show('progressCard', true);
  $('progressTitle').textContent = 'Testing "' + ssid + '"…';
  $('progressText').textContent = 'Looking for the network…';
}

function showResult(test) {
  clearTimeout(app.pollTimer);
  app.testing = false;
  sessionStorage.removeItem(FLAG);
  show('progressCard', false); show('resultCard', true);
  const head = $('resultHead');
  const ok = test.state === 'success' && test.saved;
  head.className = 'result-icon ' + (ok ? 'ok' : 'bad');
  head.innerHTML = ok ? '<svg viewBox="0 0 24 24"><path d="m5 12 5 5 9-10"/></svg>' : '<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6 6 18"/></svg>';
  head.appendChild(document.createTextNode(ok ? 'Connected and saved!' : 'Could not connect'));
  show('resultDetails', test.state === 'success');
  show('resultAgain', !ok);
  show('resultWarn', false);
  show('resultNext', false);
  const via = app.status && app.status.viaAccessPoint;
  const host = app.status ? app.status.hostname : 'camera-watchdog';
  if (test.state === 'success') {
    $('resultText').textContent = ok ? 'The ESP32 tested and saved the network "' + test.ssid + '". It keeps using that network after a restart or a power loss.' : describe(test.code);
    $('rSignal').textContent = (QUALITY[test.quality] || '') + ' (' + test.rssi + ' dBm)';
    $('rIp').textContent = test.ip;
    $('rChannel').textContent = test.channel;
    const fill = $('resultMeterFill');
    fill.style.width = test.percent + '%';
    fill.className = test.quality;
    if (test.quality === 'weak' || test.quality === 'very_weak') {
      $('resultWarn').textContent = 'The signal is ' + (test.quality === 'weak' ? 'weak' : 'very weak') + '. Move the ESP32 closer to the router or use a repeater to keep the monitoring stable.';
      show('resultWarn', true);
    }
    if (ok) {
      $('resultNext').innerHTML = '';
      $('resultNext').append(document.createTextNode((via ? 'The setup network will close. Reconnect your device to your normal Wi-Fi and open ' : 'Open ') + 'http://' + host + '.local/ or http://' + test.ip + '/ to reach the dashboard.'));
      show('resultNext', true);
      $('resultOpen').href = 'http://' + test.ip + '/';
      show('resultOpen', !via);
    }
  } else {
    $('resultText').textContent = describe(test.code);
    show('resultOpen', false);
  }
}

async function poll() {
  clearTimeout(app.pollTimer);
  const started = Date.now();
  const step = async () => {
    const status = await loadStatus();
    if (status) {
      const test = status.test;
      if (test.state === 'running') {
        const seconds = (test.elapsedMs || 0) / 1000;
        $('progressText').textContent = test.phase === 'measuring' ? 'Connected! Measuring the signal quality…' : seconds < 4 ? 'Looking for the network…' : seconds < 9 ? 'Authenticating with the router…' : 'Waiting for the router to assign an IP address…';
      } else if (test.state === 'success' || test.state === 'failed') {
        showResult(test);
        return;
      }
    } else if (Date.now() - started > 3000) {
      $('progressText').textContent = 'The ESP32 may have changed channel. If your device left the setup network, reconnect to it. This page picks up where it stopped.';
    }
    if (Date.now() - started > 150000) {
      showResult({ state: 'failed', code: 'timeout' });
      return;
    }
    app.pollTimer = setTimeout(step, 1000);
  };
  app.pollTimer = setTimeout(step, 800);
}

$('connectForm').addEventListener('submit', async event => {
  event.preventDefault();
  const ssid = app.manual ? $('ssidInput').value : app.selected.ssid;
  const password = $('pwInput').value;
  const error = $('formError');
  error.hidden = true;
  if (!ssid) { error.textContent = describe('invalid_ssid'); error.hidden = false; return; }
  if (password && password.length < 8) { error.textContent = describe('invalid_password'); error.hidden = false; return; }
  if (!app.manual && app.selected.secure && !password) { error.textContent = 'Enter the network password.'; error.hidden = false; return; }
  const body = new URLSearchParams({ ssid, password, writeToken: app.token, adminPassword: $('adminInput').value });
  $('connectBtn').disabled = true;
  try {
    const result = await api('/api/wifi/connect', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' }, body: body.toString() }, 10000);
    if (!result.ok) { error.textContent = describe(result.data.error, result.data.detail); error.hidden = false; return; }
    app.testing = true;
    sessionStorage.setItem(FLAG, ssid);
    showProgress(ssid);
    poll();
  } catch (failure) {
    error.textContent = 'No response from the ESP32. Check that it is still connected and try again.';
    error.hidden = false;
  } finally { $('connectBtn').disabled = false; }
});

$('resultAgain').addEventListener('click', () => {
  show('resultCard', false); show('pickCard', true); show('formCard', !!app.selected);
  loadStatus(); scan(true);
});

$('forgetBtn').addEventListener('click', async () => {
  if (!confirm('Forget the network saved on this ESP32? It will go back to the network stored in the firmware, if there is one.')) return;
  const body = new URLSearchParams({ writeToken: app.token, adminPassword: $('adminInput').value });
  try {
    const result = await api('/api/wifi/forget', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' }, body: body.toString() });
    if (!result.ok) { alert(describe(result.data.error, result.data.detail)); return; }
  } catch (error) { alert('No response from the ESP32.'); return; }
  loadStatus(); scan(true);
});

(async function init() {
  const status = await loadStatus();
  const pending = sessionStorage.getItem(FLAG);
  if (status && pending && status.test.ssid === pending && (status.test.state === 'running' || status.test.state === 'success' || status.test.state === 'failed')) {
    app.testing = true;
    if (status.test.state === 'running') { showProgress(pending); poll(); } else { showResult(status.test); }
    return;
  }
  sessionStorage.removeItem(FLAG);
  if (status && status.test.state === 'running') { app.testing = true; showProgress(status.test.ssid); poll(); return; }
  scan(false);
  setInterval(() => { if (!app.testing && document.visibilityState === 'visible') loadStatus(); }, 5000);
})();
</script>
</body>
</html>
)HTML";

}
