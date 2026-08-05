#pragma once

#include <Arduino.h>

// Painel web servido em GET /. Fica em PROGMEM e e transmitido com
// WebServer::send_P, portanto nao consome heap ao ser enviado.
static const char DASHBOARD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#0b0f14">
<title>Camera Watchdog</title>
<style>
*,*::before,*::after{box-sizing:border-box}
:root{
--bg:#0b0f14;--surface:#141b25;--surface2:#19212d;--line:#233043;--line2:#2d3c50;
--text:#e9eff7;--muted:#93a3b6;--dim:#657388;
--ok:#34d399;--warn:#fbbf24;--bad:#f87171;--info:#60a5fa;--accent:#38bdf8;
--r:14px;--shadow:0 1px 2px rgba(0,0,0,.35),0 14px 34px -20px rgba(0,0,0,.95);
--mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace}
html{-webkit-text-size-adjust:100%}
body{margin:0;color:var(--text);background:var(--bg);background-attachment:fixed;
background-image:radial-gradient(900px 460px at 8% -14%,rgba(56,189,248,.11),transparent 60%),
radial-gradient(760px 380px at 94% -8%,rgba(52,211,153,.07),transparent 55%);
font:15px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,system-ui,sans-serif;
-webkit-font-smoothing:antialiased}
main{max-width:1560px;margin:0 auto;padding:18px 24px 44px}
h1,h2{margin:0;font-weight:650;letter-spacing:-.015em}
.mono{font-family:var(--mono)}

/* cabecalho */
.top{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;padding:8px 0 18px}
.brand{min-width:0}
.brand h1{font-size:19px}
.brand p{margin:2px 0 0;font-size:12.5px;color:var(--dim)}
.live{display:flex;align-items:center;gap:8px;font-size:12.5px;color:var(--muted);
background:var(--surface);border:1px solid var(--line);border-radius:999px;padding:7px 14px;white-space:nowrap}
.beat{width:7px;height:7px;border-radius:50%;background:var(--ok);flex:none;animation:beat 2.4s infinite}
@keyframes beat{0%{box-shadow:0 0 0 0 rgba(52,211,153,.5)}70%{box-shadow:0 0 0 7px rgba(52,211,153,0)}100%{box-shadow:0 0 0 0 rgba(52,211,153,0)}}
.live.stale{color:var(--bad);border-color:rgba(248,113,113,.35)}
.live.stale .beat{background:var(--bad);animation:none}

/* destaque */
.hero{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);
padding:22px;display:flex;flex-wrap:wrap;gap:22px;align-items:center;justify-content:space-between;position:relative;overflow:hidden}
.hero::before{content:"";position:absolute;inset:0 auto 0 0;width:4px;background:var(--sc,var(--dim))}
.hero-main{min-width:0;flex:1 1 300px}
.badge{display:inline-flex;align-items:center;gap:8px;font-size:12.5px;font-weight:650;letter-spacing:.02em;
padding:6px 13px;border-radius:999px;background:var(--sbg,rgba(148,163,184,.12));color:var(--sc,var(--muted));
border:1px solid var(--sbd,rgba(148,163,184,.22))}
.badge i{width:7px;height:7px;border-radius:50%;background:currentColor}
.hero-name{margin:14px 0 0;font-size:27px;font-weight:680;letter-spacing:-.02em;line-height:1.2;overflow-wrap:anywhere}
.hero-sub{margin-top:7px;font-size:13.5px;color:var(--muted);overflow-wrap:anywhere}
.hero-sub b{color:var(--text);font-weight:600}
.gauge{position:relative;width:112px;height:112px;flex:none;margin:0 auto}
.arc{position:absolute;inset:0;border-radius:50%;transition:background .5s ease;
background:conic-gradient(var(--sc,var(--dim)) calc(var(--p,0)*3.6deg),rgba(255,255,255,.06) 0)}
.hole{position:absolute;inset:10px;border-radius:50%;background:var(--surface);display:grid;place-items:center;line-height:1.15}
.hole b{font-size:22px;font-weight:680;letter-spacing:-.02em}
.hole span{font-size:10.5px;color:var(--dim);letter-spacing:.05em;text-transform:uppercase}

/* previa da camera */
.viewer{margin-top:14px;background:var(--surface);border:1px solid var(--line);border-radius:var(--r);
box-shadow:var(--shadow);overflow:hidden}
.viewer-h{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;padding:15px 17px;
border-bottom:1px solid var(--line)}
.viewer-h h2{font-size:14px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.viewer-h p{margin:3px 0 0;font-size:12.5px;color:var(--dim)}
.feed-toggle{flex:none;width:auto;padding:8px 13px;border-radius:9px;font:inherit;font-size:12.5px;font-weight:600;
color:var(--text);cursor:pointer;background:var(--surface);border:1px solid var(--line2);transition:.16s ease}
.feed-toggle:hover{background:var(--surface2);border-color:var(--accent);transform:translateY(-1px)}
.feed-toggle:active{transform:none}
.feed-toggle:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.viewer-stage{position:relative;display:grid;place-items:center;aspect-ratio:16/9;min-height:220px;
background:#05070a;overflow:hidden}
.viewer-stage::before{content:"";position:absolute;inset:0;background:radial-gradient(circle at center,#17202c 0,#05070a 66%)}
.viewer-stage img{position:relative;z-index:1;width:100%;height:100%;object-fit:contain;opacity:0;transition:opacity .2s ease}
.viewer-stage img.ready{opacity:1}
.feed-state{position:absolute;z-index:2;inset:0;display:grid;place-items:center;text-align:center;padding:24px;
color:var(--muted);font-size:13px;background:rgba(5,7,10,.7);transition:opacity .2s ease}
.feed-state.hidden{opacity:0;pointer-events:none}
.feed-stamp{position:absolute;z-index:3;right:10px;bottom:10px;padding:5px 9px;border-radius:7px;
background:rgba(5,7,10,.72);color:#b7c3d2;font:11px/1.3 var(--mono);backdrop-filter:blur(5px)}
.viewer.paused .viewer-h{border-bottom:0}
.viewer.paused .viewer-stage{display:none}

/* metricas */
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:14px 0 0}
.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);padding:15px 16px;box-shadow:var(--shadow)}
.card-h{display:flex;align-items:center;gap:7px;color:var(--dim);font-size:11.5px;
font-weight:650;letter-spacing:.07em;text-transform:uppercase}
.card-h .pip{width:7px;height:7px;border-radius:50%;background:var(--dim);margin-left:auto;flex:none}
.card-v{margin-top:9px;font-size:21px;font-weight:670;letter-spacing:-.02em;overflow-wrap:anywhere}
.card-m{margin-top:3px;font-size:12.5px;color:var(--muted);overflow-wrap:anywhere}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}
.pip.ok{background:var(--ok)}.pip.warn{background:var(--warn)}.pip.bad{background:var(--bad)}
.bars{display:inline-flex;align-items:flex-end;gap:3px;height:19px;vertical-align:-3px;margin-right:9px}
.bars i{width:5px;border-radius:1.5px;background:rgba(255,255,255,.13)}
.bars i:nth-child(1){height:6px}.bars i:nth-child(2){height:10px}
.bars i:nth-child(3){height:14px}.bars i:nth-child(4){height:19px}
.bars i.on{background:currentColor}

.sec{margin-top:24px}
.sec-t{display:flex;align-items:baseline;gap:10px;margin-bottom:11px}
.sec-t h2{font-size:14px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.sec-t span{font-size:12.5px;color:var(--dim)}

/* registro */
.log{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);overflow:hidden}
.row{display:flex;flex-wrap:wrap;gap:4px 16px;padding:13px 16px;border-top:1px solid var(--line);font-size:13.5px}
.row:first-child{border-top:0}
.row dt{flex:0 0 156px;color:var(--dim);font-weight:600;margin:0}
.row dd{flex:1 1 240px;margin:0;color:var(--muted);overflow-wrap:anywhere;font-family:var(--mono);font-size:12.5px}
.tag{display:inline-block;padding:2px 9px;border-radius:999px;font-size:11px;font-weight:700;
letter-spacing:.03em;font-family:inherit}
.tag.ok{background:rgba(52,211,153,.14);color:var(--ok)}
.tag.bad{background:rgba(248,113,113,.14);color:var(--bad)}

details{margin-top:24px;background:var(--surface);border:1px solid var(--line);border-radius:var(--r);overflow:hidden}
summary{padding:14px 16px;cursor:pointer;font-size:13px;font-weight:650;color:var(--muted);
letter-spacing:.05em;text-transform:uppercase;list-style:none;display:flex;align-items:center;gap:9px}
summary::-webkit-details-marker{display:none}
summary::before{content:"";width:6px;height:6px;border-right:1.6px solid currentColor;
border-bottom:1.6px solid currentColor;transform:rotate(-45deg);transition:transform .2s}
details[open] summary::before{transform:rotate(45deg)}
pre{margin:0;padding:0 16px 16px;white-space:pre-wrap;overflow-wrap:anywhere;
font-family:var(--mono);font-size:12px;line-height:1.6;color:var(--muted)}
footer{margin-top:26px;text-align:center;font-size:12px;color:var(--dim)}

@media(max-width:900px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(max-width:560px){
main{padding:14px 12px 36px}
.hero{padding:18px;gap:16px}
.hero-name{font-size:23px}
.gauge{width:96px;height:96px;margin:0}
.grid{grid-template-columns:1fr;gap:10px}
.row dt{flex:1 1 100%}
}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<main>

<div class="top">
  <div class="brand">
    <h1>Camera Watchdog</h1>
    <p>Monitor ONVIF/RTSP com alertas por e-mail</p>
  </div>
  <div class="live" id="live"><span class="beat"></span><span id="liveText">conectando…</span></div>
</div>

<section class="hero" id="hero">
  <div class="hero-main">
    <span class="badge" id="badge"><i></i><span id="stateText">carregando</span></span>
    <h2 class="hero-name" id="camName">—</h2>
    <div class="hero-sub" id="camSub">aguardando o primeiro ciclo de verificação…</div>
  </div>
  <div>
    <div class="gauge">
      <div class="arc" id="arc"></div>
      <div class="hole"><div><b id="availValue">—</b><br><span>uptime</span></div></div>
    </div>
  </div>
</section>

<div class="grid">
  <article class="card">
    <div class="card-h">ONVIF<i class="pip" id="p-onvif"></i></div>
    <div class="card-v" id="v-onvif">—</div>
    <div class="card-m" id="m-onvif">—</div>
  </article>
  <article class="card">
    <div class="card-h">RTSP<i class="pip" id="p-rtsp"></i></div>
    <div class="card-v" id="v-rtsp">—</div>
    <div class="card-m" id="m-rtsp">—</div>
  </article>
  <article class="card">
    <div class="card-h">Wi-Fi do ESP32<i class="pip" id="p-wifi"></i></div>
    <div class="card-v" id="v-wifi"><span class="bars" id="bars"><i></i><i></i><i></i><i></i></span><span id="rssi">—</span></div>
    <div class="card-m" id="m-wifi">—</div>
  </article>
  <article class="card">
    <div class="card-h">Falhas consecutivas<i class="pip" id="p-fail"></i></div>
    <div class="card-v" id="v-fail">—</div>
    <div class="card-m" id="m-fail">—</div>
  </article>
  <article class="card">
    <div class="card-h">Verificações</div>
    <div class="card-v" id="v-checks">—</div>
    <div class="card-m" id="m-checks">—</div>
  </article>
  <article class="card">
    <div class="card-h">Memória livre<i class="pip" id="p-heap"></i></div>
    <div class="card-v" id="v-heap">—</div>
    <div class="card-m" id="m-heap">—</div>
  </article>
</div>

<section class="viewer" id="viewer" aria-labelledby="viewerTitle">
  <div class="viewer-h">
    <div>
      <h2 id="viewerTitle">Prévia ao vivo</h2>
      <p id="feedMeta">Quadros JPEG protegidos pelo ESP32, sem expor credenciais da câmera</p>
    </div>
    <button class="feed-toggle" id="feedToggle" type="button" aria-controls="viewerStage" aria-expanded="true">Pausar prévia</button>
  </div>
  <div class="viewer-stage" id="viewerStage">
    <img id="cameraFeed" alt="Imagem ao vivo da câmera monitorada" decoding="async">
    <div class="feed-state" id="feedState">Carregando o primeiro quadro da câmera…</div>
    <div class="feed-stamp" id="feedStamp">aguardando imagem</div>
  </div>
</section>

<section class="sec">
  <div class="sec-t"><h2>Registro</h2><span>última atividade conhecida</span></div>
  <dl class="log">
    <div class="row"><dt>Último evento</dt><dd id="l-event">—</dd></div>
    <div class="row"><dt>Endpoint ONVIF</dt><dd id="l-url">—</dd></div>
    <div class="row"><dt>Descoberta ONVIF</dt><dd id="l-disc">—</dd></div>
    <div class="row"><dt>Varredura de rede</dt><dd id="l-scan">—</dd></div>
    <div class="row"><dt>Envio de e-mail</dt><dd id="l-mail">—</dd></div>
  </dl>
</section>

<details>
  <summary>Diagnóstico completo (JSON)</summary>
  <pre id="raw">Carregando…</pre>
</details>

<footer id="foot">ESP32 Camera Watchdog</footer>
</main>

<script>
const $ = id => document.getElementById(id);
const PALETTE = {
  online:    ['Online',      'var(--ok)',   'rgba(52,211,153,.13)', 'rgba(52,211,153,.28)'],
  degraded:  ['Instável',    'var(--warn)', 'rgba(251,191,36,.13)', 'rgba(251,191,36,.28)'],
  offline:   ['Offline',     'var(--bad)',  'rgba(248,113,113,.13)','rgba(248,113,113,.28)'],
  discovering:['Procurando', 'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)'],
  starting:  ['Iniciando',   'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)']
};

function duration(totalSeconds) {
  const s = Math.max(0, Math.floor(totalSeconds));
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
  const m = Math.floor(s % 3600 / 60);
  if (d) return d + 'd ' + h + 'h';
  if (h) return h + 'h ' + m + 'min';
  if (m) return m + 'min ' + (s % 60) + 's';
  return s + 's';
}

function pip(id, tone) { $(id).className = 'pip ' + tone; }

function plural(count, one, many) { return count + ' ' + (count === 1 ? one : many); }

function setProbe(prefix, tcp, spoke, code, latency, protocol) {
  const value = $('v-' + prefix), meta = $('m-' + prefix);
  if (spoke) {
    value.textContent = protocol + ' ' + (code || '—');
    value.className = 'card-v ok';
    pip('p-' + prefix, 'ok');
    meta.textContent = 'resposta em ' + latency + ' ms';
  } else if (tcp) {
    value.textContent = 'porta aberta';
    value.className = 'card-v warn';
    pip('p-' + prefix, 'warn');
    meta.textContent = 'conectou em ' + latency + ' ms, sem resposta do protocolo';
  } else {
    value.textContent = 'indisponível';
    value.className = 'card-v bad';
    pip('p-' + prefix, 'bad');
    meta.textContent = 'sem conexão TCP';
  }
}

let lastOk = 0;
let cameraKnown = false;
let feedEnabled = true;
let feedLoading = false;
let feedFailures = 0;
let feedTimer;

function scheduleFeed(delay) {
  clearTimeout(feedTimer);
  if (!feedEnabled || document.hidden) return;
  feedTimer = setTimeout(() => {
    feedTimer = null;
    loadFeed();
  }, delay);
}

function loadFeed() {
  if (!feedEnabled || document.hidden || feedLoading) return;
  if (!cameraKnown) {
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'Aguardando a câmera ser localizada…';
    scheduleFeed(3000);
    return;
  }

  const frame = $('cameraFeed');
  feedLoading = true;
  frame.onload = () => {
    feedLoading = false;
    feedFailures = 0;
    frame.classList.add('ready');
    $('feedState').classList.add('hidden');
    $('feedStamp').textContent = 'atualizado ' + new Date().toLocaleTimeString('pt-BR');
    scheduleFeed(1000);
  };
  frame.onerror = () => {
    feedLoading = false;
    feedFailures++;
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'Não foi possível atualizar a imagem; tentando novamente…';
    $('feedStamp').textContent = 'prévia temporariamente indisponível';
    scheduleFeed(Math.min(8000, 1000 * Math.pow(2, Math.min(feedFailures, 3))));
  };
  frame.src = '/api/camera/snapshot?t=' + Date.now();
}

function render(d) {
  const [label, color, bg, border] = PALETTE[d.state] || ['Desconhecido', 'var(--dim)', '', ''];
  const hero = $('hero');
  hero.style.setProperty('--sc', color);
  hero.style.setProperty('--sbg', bg);
  hero.style.setProperty('--sbd', border);
  $('stateText').textContent = label + ' · há ' + duration(d.stateSinceSeconds);

  $('camName').textContent = d.camera.name;
  $('camSub').innerHTML = d.camera.known
    ? 'IP <b>' + d.camera.ip + '</b> · RTSP <b>' + d.camera.rtspPort + '</b> · ESP32 em <b>' + d.wifi.ip + '</b>'
    : 'Câmera ainda não localizada na rede · ESP32 em <b>' + d.wifi.ip + '</b>';
  cameraKnown = d.camera.known;

  if (d.snapshot) {
    $('feedMeta').textContent = d.snapshot.ready
      ? 'Perfil ' + (d.snapshot.profile || 'ONVIF') + ' · ' + d.snapshot.served + ' quadros entregues'
      : 'Preparando o perfil JPEG protegido pelo ESP32';
  }

  if (feedEnabled && !feedLoading && !feedTimer) scheduleFeed(0);

  const c = d.counters;
  const rate = c.totalChecks ? c.successfulChecks / c.totalChecks * 100 : 0;
  $('arc').style.setProperty('--p', c.totalChecks ? rate : 0);
  $('availValue').textContent = c.totalChecks ? rate.toFixed(rate >= 99.95 ? 0 : 1) + '%' : '—';

  const p = d.probes;
  setProbe('onvif', p.onvifTcp, p.onvifHttp, p.onvifStatus, p.onvifLatencyMs, 'HTTP');
  setProbe('rtsp', p.rtspTcp, p.rtspProtocol, p.rtspStatus, p.rtspLatencyMs, 'RTSP');

  const rssi = d.wifi.rssi;
  const level = !d.wifi.connected ? 0 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
  const wifi = $('v-wifi');
  wifi.className = 'card-v ' + (level >= 3 ? 'ok' : level === 2 ? 'warn' : 'bad');
  [...$('bars').children].forEach((bar, i) => bar.classList.toggle('on', i < level));
  $('rssi').textContent = d.wifi.connected ? rssi + ' dBm' : 'desconectado';
  $('m-wifi').textContent = d.wifi.connected ? d.wifi.ssid + ' · gateway ' + d.wifi.gateway : 'tentando reconectar';
  pip('p-wifi', level >= 3 ? 'ok' : level === 2 ? 'warn' : 'bad');

  const fails = c.consecutiveFailures;
  $('v-fail').textContent = fails;
  $('v-fail').className = 'card-v ' + (fails ? (fails >= 3 ? 'bad' : 'warn') : 'ok');
  $('m-fail').textContent = !fails
    ? plural(c.consecutiveSuccesses, 'sucesso seguido', 'sucessos seguidos')
    : d.state === 'offline'
      ? 'falha confirmada, alerta disparado'
      : 'confirmando a falha antes de alertar';
  pip('p-fail', fails ? (fails >= 3 ? 'bad' : 'warn') : 'ok');

  $('v-checks').textContent = c.totalChecks;
  $('m-checks').textContent = c.failedChecks + ' com falha · '
    + plural(c.discoveryRuns, 'descoberta', 'descobertas') + ' · '
    + plural(c.subnetScanRuns, 'varredura', 'varreduras');

  const heapKb = Math.round(d.freeHeapBytes / 1024);
  $('v-heap').textContent = heapKb + ' KB';
  $('v-heap').className = 'card-v ' + (heapKb >= 100 ? 'ok' : heapKb >= 50 ? 'warn' : 'bad');
  $('m-heap').textContent = 'mínimo já observado: ' + Math.round(d.minimumFreeHeapBytes / 1024) + ' KB';
  pip('p-heap', heapKb >= 100 ? 'ok' : heapKb >= 50 ? 'warn' : 'bad');

  $('l-event').textContent = d.lastEvent;
  $('l-url').textContent = d.camera.onvifUrl || 'desconhecido';
  $('l-disc').textContent = d.lastDiscovery;
  $('l-scan').textContent = d.lastSubnetScan;
  $('l-mail').innerHTML = (d.resendConfigured
    ? '<span class="tag ok">Resend ativo</span> '
    : '<span class="tag bad">Resend não configurado</span> ')
    + (c.emailsSent + ' enviados, ' + c.emailErrors + ' erros — ') + d.lastEmailResult;

  $('raw').textContent = JSON.stringify(d, null, 2);
  $('foot').textContent = 'ESP32 Camera Watchdog · firmware ' + d.firmwareVersion
    + ' · ligado há ' + duration(d.uptimeSeconds);

  lastOk = Date.now();
}

async function refresh() {
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    render(await response.json());
  } catch (error) {
    $('live').classList.add('stale');
    $('liveText').textContent = 'sem resposta do dispositivo';
    return;
  }
  $('live').classList.remove('stale');
}

setInterval(() => {
  if (!lastOk || $('live').classList.contains('stale')) return;
  const ago = Math.round((Date.now() - lastOk) / 1000);
  $('liveText').textContent = ago < 2 ? 'atualizado agora' : 'atualizado há ' + ago + 's';
}, 1000);

const feedToggleButton = $('feedToggle');

feedToggleButton.addEventListener('click', () => {
  feedEnabled = !feedEnabled;
  feedToggleButton.textContent = feedEnabled ? 'Pausar prévia' : 'Retomar prévia';
  feedToggleButton.setAttribute('aria-expanded', feedEnabled ? 'true' : 'false');
  $('viewer').classList.toggle('paused', !feedEnabled);
  $('viewerStage').setAttribute('aria-hidden', feedEnabled ? 'false' : 'true');

  if (feedEnabled) {
    $('feedState').textContent = 'Atualizando a imagem…';
    $('feedState').classList.remove('hidden');
    scheduleFeed(0);
  } else {
    clearTimeout(feedTimer);
    feedTimer = null;
    feedLoading = false;
    $('cameraFeed').removeAttribute('src');
    $('cameraFeed').classList.remove('ready');
    $('feedState').textContent = 'Prévia pausada';
    $('feedState').classList.remove('hidden');
    $('feedStamp').textContent = 'atualização pausada';
  }
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearTimeout(feedTimer);
    feedTimer = null;
    return;
  }

  if (feedEnabled) scheduleFeed(0);
});

refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)HTML";
