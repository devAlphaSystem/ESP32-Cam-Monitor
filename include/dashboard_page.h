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
[hidden]{display:none!important}
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
.top-tools{display:flex;flex-wrap:wrap;align-items:center;justify-content:flex-end;gap:10px}
.tabs{display:flex;gap:3px;padding:3px;background:var(--surface);border:1px solid var(--line);border-radius:10px}
.tab{padding:5px 12px;border:0;border-radius:7px;background:transparent;color:var(--muted);cursor:pointer;
font:inherit;font-size:12.5px;font-weight:600;line-height:1.5;transition:.16s ease}
.tab:hover{color:var(--text);background:var(--surface2)}
.tab[aria-selected="true"]{color:var(--text);background:var(--line2)}
.tab:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
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
.viewer{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);
box-shadow:var(--shadow);overflow:hidden}
.viewer-h{padding:15px 17px;border-bottom:1px solid var(--line)}
.viewer-h h2{font-size:14px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.viewer-h p{margin:3px 0 0;font-size:12.5px;color:var(--dim)}
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
.viewer-stage img.ready{cursor:zoom-in}
.feed-fs-btn{position:absolute;z-index:4;right:10px;top:10px;width:32px;height:32px;display:grid;place-items:center;
border:1px solid rgba(255,255,255,.14);border-radius:8px;background:rgba(5,7,10,.72);color:#dbe6f2;
cursor:pointer;backdrop-filter:blur(5px);transition:.16s ease}
.feed-fs-btn:hover{background:rgba(20,27,37,.92);color:#fff}
.feed-fs-btn:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.feed-fs-btn svg{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen,.viewer-stage.fs-fallback{aspect-ratio:auto;min-height:0;background:#000}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen{width:100%;height:100%}
.viewer-stage.fs-fallback{position:fixed;inset:0;z-index:999;width:100vw;height:100vh}
.viewer-stage:fullscreen::before,.viewer-stage:-webkit-full-screen::before,.viewer-stage.fs-fallback::before{display:none}
.viewer-stage:fullscreen img,.viewer-stage:-webkit-full-screen img,.viewer-stage.fs-fallback img{cursor:zoom-out}
body.fs-locked{overflow:hidden}

/* metricas */
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:14px 0 0}
.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);padding:15px 16px;box-shadow:var(--shadow)}
.card-h{display:flex;align-items:center;gap:7px;color:var(--dim);font-size:11.5px;
font-weight:650;letter-spacing:.07em;text-transform:uppercase}
.card-h .pip{width:7px;height:7px;border-radius:50%;background:var(--dim);margin-left:auto;flex:none}
.card-v{margin-top:9px;font-size:21px;font-weight:670;letter-spacing:-.02em;overflow-wrap:anywhere}
.card-m{margin-top:3px;font-size:12.5px;color:var(--muted);overflow-wrap:anywhere}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}
.pip.ok{background:var(--ok)}.pip.warn{background:var(--warn)}.pip.bad{background:var(--bad)}.pip.info{background:var(--info)}
.bars{display:inline-flex;align-items:flex-end;gap:3px;height:19px;vertical-align:-3px;margin-right:9px}
.bars i{width:5px;border-radius:1.5px;background:rgba(255,255,255,.13)}
.bars i:nth-child(1){height:6px}.bars i:nth-child(2){height:10px}
.bars i:nth-child(3){height:14px}.bars i:nth-child(4){height:19px}
.bars i.on{background:currentColor}
.meter{height:5px;margin-top:11px;border-radius:999px;background:rgba(255,255,255,.07);overflow:hidden}
.meter i{display:block;width:0;height:100%;border-radius:inherit;background:var(--ok);transition:width .4s ease}
.meter i.warn{background:var(--warn)}.meter i.bad{background:var(--bad)}

/* historico de recursos */
.chart-card{margin-top:14px;background:var(--surface);border:1px solid var(--line);border-radius:var(--r);
padding:16px;box-shadow:var(--shadow)}
.chart-head{display:flex;flex-wrap:wrap;align-items:flex-start;justify-content:space-between;gap:10px}
.chart-head h2{font-size:14px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.chart-head p{margin:3px 0 0;font-size:12.5px;color:var(--dim)}
.legend{display:flex;flex-wrap:wrap;gap:13px;color:var(--muted);font-size:12px}
.legend span{display:flex;align-items:center;gap:6px}
.legend i{width:8px;height:8px;border-radius:50%;background:var(--accent)}
.legend .wifi-dot{background:var(--warn)}
.chart{width:100%;height:210px;margin-top:13px;display:block;overflow:visible}
.chart line{stroke:var(--line);stroke-width:1;vector-effect:non-scaling-stroke}
.chart polyline{fill:none;stroke:var(--accent);stroke-width:2.2;stroke-linecap:round;stroke-linejoin:round;
vector-effect:non-scaling-stroke}
.chart .wifi-line{stroke:var(--warn)}

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
.top-tools{width:100%;justify-content:space-between}
.tabs{flex:1}.tab{flex:1}
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

<header class="top">
  <div class="brand">
    <h1>Camera Watchdog</h1>
    <p>Monitor ONVIF/RTSP com alertas por e-mail</p>
  </div>
  <div class="top-tools">
    <nav class="tabs" role="tablist" aria-label="Seções do painel">
      <button class="tab" id="cameraTab" type="button" role="tab" data-view="camera" aria-controls="cameraView" aria-selected="true">Câmera</button>
      <button class="tab" id="feedTab" type="button" role="tab" data-view="feed" aria-controls="feedView" aria-selected="false">Prévia</button>
      <button class="tab" id="resourcesTab" type="button" role="tab" data-view="resources" aria-controls="resourcesView" aria-selected="false">Recursos</button>
    </nav>
    <div class="live" id="live"><span class="beat"></span><span id="liveText">conectando…</span></div>
  </div>
</header>

<div id="cameraView" role="tabpanel" aria-labelledby="cameraTab" data-view-panel="camera">

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
    <div class="card-h">Verificações</div>
    <div class="card-v" id="v-checks">—</div>
    <div class="card-m" id="m-checks">—</div>
  </article>
</div>

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
</div>

<div id="feedView" role="tabpanel" aria-labelledby="feedTab" data-view-panel="feed" hidden>
  <section class="viewer" aria-labelledby="viewerTitle">
    <div class="viewer-h">
      <h2 id="viewerTitle">Prévia ao vivo</h2>
      <p id="feedMeta">Quadros JPEG protegidos pelo ESP32, sem expor credenciais da câmera</p>
    </div>
    <div class="viewer-stage">
      <img id="cameraFeed" alt="Imagem ao vivo da câmera monitorada" decoding="async">
      <div class="feed-state" id="feedState">Carregando o primeiro quadro da câmera…</div>
      <div class="feed-stamp" id="feedStamp">aguardando imagem</div>
      <button type="button" class="feed-fs-btn" id="feedFsBtn" aria-label="Ver em tela cheia" aria-pressed="false" title="Tela cheia">
        <svg class="ic-expand" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M9 3H5a2 2 0 0 0-2 2v4M15 3h4a2 2 0 0 1 2 2v4M9 21H5a2 2 0 0 1-2-2v-4M15 21h4a2 2 0 0 0 2-2v-4"/></svg>
        <svg class="ic-collapse" viewBox="0 0 24 24" aria-hidden="true" focusable="false" hidden><path d="M4 9V5a2 2 0 0 1 2-2h4M20 9V5a2 2 0 0 0-2-2h-4M4 15v4a2 2 0 0 0 2 2h4M20 15v4a2 2 0 0 1-2 2h-4"/></svg>
      </button>
    </div>
  </section>
</div>

<div id="resourcesView" role="tabpanel" aria-labelledby="resourcesTab" data-view-panel="resources" hidden>
  <section class="hero" id="resourcesHero">
    <div class="hero-main">
      <span class="badge" id="resourcesBadge"><i></i><span id="resourcesState">carregando</span></span>
      <h2 class="hero-name" id="resourcesChip">ESP32</h2>
      <div class="hero-sub" id="resourcesMeta">aguardando dados do dispositivo…</div>
    </div>
    <div>
      <div class="gauge">
        <div class="arc" id="resourcesArc"></div>
        <div class="hole"><div><b id="resourcesHeapPercent">—</b><br><span>heap livre</span></div></div>
      </div>
    </div>
  </section>

  <div class="grid">
    <article class="card">
      <div class="card-h">Heap livre<i class="pip" id="p-rheap"></i></div>
      <div class="card-v" id="r-heap">—</div>
      <div class="card-m" id="r-heap-meta">—</div>
      <div class="meter"><i id="r-heap-meter"></i></div>
    </article>
    <article class="card">
      <div class="card-h">Maior bloco alocável<i class="pip" id="p-rblock"></i></div>
      <div class="card-v" id="r-block">—</div>
      <div class="card-m" id="r-block-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Menor heap desde o boot<i class="pip" id="p-rmin"></i></div>
      <div class="card-v" id="r-min">—</div>
      <div class="card-m" id="r-min-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Firmware e flash</div>
      <div class="card-v info" id="r-flash">—</div>
      <div class="card-m" id="r-flash-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">PSRAM<i class="pip" id="p-rpsram"></i></div>
      <div class="card-v" id="r-psram">—</div>
      <div class="card-m" id="r-psram-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Wi-Fi do ESP32<i class="pip" id="p-wifi"></i></div>
      <div class="card-v" id="v-wifi"><span class="bars" id="bars"><i></i><i></i><i></i><i></i></span><span id="rssi">—</span></div>
      <div class="card-m" id="m-wifi">—</div>
    </article>
  </div>

  <section class="chart-card" aria-labelledby="historyTitle">
    <div class="chart-head">
      <div>
        <h2 id="historyTitle">Histórico recente</h2>
        <p>Até três minutos desta sessão do navegador</p>
      </div>
      <div class="legend">
        <span><i></i>Heap <b id="historyHeap">—</b></span>
        <span><i class="wifi-dot"></i>Wi-Fi <b id="historyRssi">—</b></span>
      </div>
    </div>
    <svg class="chart" viewBox="0 0 600 180" preserveAspectRatio="none" role="img" aria-label="Histórico de heap livre e sinal Wi-Fi">
      <line x1="0" y1="20" x2="600" y2="20"></line>
      <line x1="0" y1="90" x2="600" y2="90"></line>
      <line x1="0" y1="160" x2="600" y2="160"></line>
      <polyline id="heapHistory" points=""></polyline>
      <polyline class="wifi-line" id="rssiHistory" points=""></polyline>
    </svg>
  </section>
</div>

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
const HISTORY_LIMIT = 60;
const resourceHistory = { heap: [], rssi: [] };
let activeView = location.hash === '#resources'
  ? 'resources'
  : location.hash === '#feed' ? 'feed' : 'camera';

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

function formatBytes(value) {
  if (value >= 1048576) return (value / 1048576).toFixed(value >= 10485760 ? 1 : 2) + ' MB';
  if (value >= 1024) return Math.round(value / 1024) + ' KB';
  return value + ' B';
}

function resourceTone(percent) {
  return percent >= 35 ? 'ok' : percent >= 20 ? 'warn' : 'bad';
}

function historyPoints(values, minimum, maximum) {
  const width = 600, top = 20, height = 140;
  const span = Math.max(1, values.length - 1);
  return values.map((value, index) => {
    const normalized = Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)));
    return (index / span * width).toFixed(1) + ',' + (top + (1 - normalized) * height).toFixed(1);
  }).join(' ');
}

function renderResources(d) {
  const resources = d.resources;
  if (!resources) return;

  const heap = resources.heap;
  const heapPercent = heap.totalBytes ? heap.freeBytes / heap.totalBytes * 100 : 0;
  const heapTone = resourceTone(heapPercent);
  const toneValues = heapTone === 'ok'
    ? ['Recursos estáveis', 'var(--ok)', 'rgba(52,211,153,.13)', 'rgba(52,211,153,.28)']
    : heapTone === 'warn'
      ? ['Heap em atenção', 'var(--warn)', 'rgba(251,191,36,.13)', 'rgba(251,191,36,.28)']
      : ['Heap baixo', 'var(--bad)', 'rgba(248,113,113,.13)', 'rgba(248,113,113,.28)'];
  const resourceHero = $('resourcesHero');
  resourceHero.style.setProperty('--sc', toneValues[1]);
  resourceHero.style.setProperty('--sbg', toneValues[2]);
  resourceHero.style.setProperty('--sbd', toneValues[3]);
  $('resourcesState').textContent = toneValues[0];
  $('resourcesChip').textContent = resources.chip.model + ' rev. ' + resources.chip.revision;
  $('resourcesMeta').textContent = resources.chip.cores + ' núcleos · '
    + resources.chip.cpuFrequencyMHz + ' MHz · ligado há ' + duration(d.uptimeSeconds);
  $('resourcesArc').style.setProperty('--p', heapPercent);
  $('resourcesHeapPercent').textContent = Math.round(heapPercent) + '%';

  $('r-heap').textContent = formatBytes(heap.freeBytes);
  $('r-heap').className = 'card-v ' + heapTone;
  $('r-heap-meta').textContent = formatBytes(heap.totalBytes - heap.freeBytes)
    + ' usados de ' + formatBytes(heap.totalBytes);
  $('r-heap-meter').style.width = Math.max(0, Math.min(100, heapPercent)) + '%';
  $('r-heap-meter').className = heapTone;
  pip('p-rheap', heapTone);

  const fragmentation = heap.freeBytes
    ? Math.max(0, 100 - heap.largestBlockBytes / heap.freeBytes * 100)
    : 100;
  const blockTone = fragmentation <= 25 ? 'ok' : fragmentation <= 50 ? 'warn' : 'bad';
  $('r-block').textContent = formatBytes(heap.largestBlockBytes);
  $('r-block').className = 'card-v ' + blockTone;
  $('r-block-meta').textContent = 'fragmentação estimada em ' + Math.round(fragmentation) + '%';
  pip('p-rblock', blockTone);

  const minimumPercent = heap.totalBytes ? heap.minimumFreeBytes / heap.totalBytes * 100 : 0;
  const minimumTone = resourceTone(minimumPercent);
  $('r-min').textContent = formatBytes(heap.minimumFreeBytes);
  $('r-min').className = 'card-v ' + minimumTone;
  $('r-min-meta').textContent = Math.round(minimumPercent) + '% do heap total';
  pip('p-rmin', minimumTone);

  const flash = resources.flash;
  $('r-flash').textContent = formatBytes(flash.sketchBytes);
  $('r-flash-meta').textContent = formatBytes(flash.freeSketchBytes)
    + ' livres na partição · flash ' + formatBytes(flash.chipBytes);

  const psram = resources.psram;
  if (psram.totalBytes) {
    const psramPercent = psram.freeBytes / psram.totalBytes * 100;
    const psramTone = resourceTone(psramPercent);
    $('r-psram').textContent = formatBytes(psram.freeBytes);
    $('r-psram').className = 'card-v ' + psramTone;
    $('r-psram-meta').textContent = 'livres de ' + formatBytes(psram.totalBytes)
      + ' · maior bloco ' + formatBytes(psram.largestBlockBytes);
    pip('p-rpsram', psramTone);
  } else {
    $('r-psram').textContent = 'não disponível';
    $('r-psram').className = 'card-v info';
    $('r-psram-meta').textContent = 'esta placa não expõe PSRAM';
    pip('p-rpsram', 'info');
  }

  resourceHistory.heap.push(heapPercent);
  resourceHistory.rssi.push(d.wifi.connected ? d.wifi.rssi : -100);
  if (resourceHistory.heap.length > HISTORY_LIMIT) resourceHistory.heap.shift();
  if (resourceHistory.rssi.length > HISTORY_LIMIT) resourceHistory.rssi.shift();
  $('heapHistory').setAttribute('points', historyPoints(resourceHistory.heap, 0, 100));
  $('rssiHistory').setAttribute('points', historyPoints(resourceHistory.rssi, -100, -30));
  $('historyHeap').textContent = Math.round(heapPercent) + '%';
  $('historyRssi').textContent = d.wifi.connected ? d.wifi.rssi + ' dBm' : 'desconectado';
}

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
let feedLoading = false;
let feedFailures = 0;
let feedTimer;
let feedRequestedAtMs = 0;
const FEED_MIN_INTERVAL_MS = 300;   // piso de seguranca para nao sobrecarregar o loop do ESP32
const FEED_TARGET_CYCLE_MS = 600;   // meta de intervalo total (busca + espera) entre quadros

function scheduleFeed(delay) {
  clearTimeout(feedTimer);
  if (document.hidden || activeView !== 'feed') return;
  feedTimer = setTimeout(() => {
    feedTimer = null;
    loadFeed();
  }, delay);
}

function loadFeed() {
  if (document.hidden || activeView !== 'feed' || feedLoading) return;
  if (!cameraKnown) {
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'Aguardando a câmera ser localizada…';
    scheduleFeed(3000);
    return;
  }

  const frame = $('cameraFeed');
  feedLoading = true;
  feedRequestedAtMs = Date.now();
  frame.onload = () => {
    feedLoading = false;
    feedFailures = 0;
    frame.classList.add('ready');
    $('feedState').classList.add('hidden');
    $('feedStamp').textContent = 'atualizado ' + new Date().toLocaleTimeString('pt-BR');
    const elapsed = Date.now() - feedRequestedAtMs;
    scheduleFeed(Math.max(FEED_MIN_INTERVAL_MS, FEED_TARGET_CYCLE_MS - elapsed));
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

const feedStage = document.querySelector('.viewer-stage');
const feedFsBtn = $('feedFsBtn');

function isNativeFullscreen() {
  return document.fullscreenElement === feedStage || document.webkitFullscreenElement === feedStage;
}

function updateFsButton() {
  const active = isNativeFullscreen() || feedStage.classList.contains('fs-fallback');
  feedFsBtn.setAttribute('aria-pressed', active ? 'true' : 'false');
  feedFsBtn.setAttribute('aria-label', active ? 'Sair da tela cheia' : 'Ver em tela cheia');
  feedFsBtn.title = active ? 'Sair da tela cheia' : 'Tela cheia';
  feedFsBtn.querySelector('.ic-expand').hidden = active;
  feedFsBtn.querySelector('.ic-collapse').hidden = !active;
}

function exitFallbackFullscreen() {
  feedStage.classList.remove('fs-fallback');
  document.body.classList.remove('fs-locked');
  updateFsButton();
}

function toggleFullscreen() {
  if (isNativeFullscreen()) {
    (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    return;
  }
  if (feedStage.classList.contains('fs-fallback')) {
    exitFallbackFullscreen();
    return;
  }

  const request = feedStage.requestFullscreen || feedStage.webkitRequestFullscreen;
  if (!request) {
    feedStage.classList.add('fs-fallback');
    document.body.classList.add('fs-locked');
    updateFsButton();
    return;
  }
  Promise.resolve(request.call(feedStage)).catch(() => {
    feedStage.classList.add('fs-fallback');
    document.body.classList.add('fs-locked');
    updateFsButton();
  });
}

feedFsBtn.addEventListener('click', toggleFullscreen);
$('cameraFeed').addEventListener('click', () => { if (cameraKnown) toggleFullscreen(); });
document.addEventListener('fullscreenchange', updateFsButton);
document.addEventListener('webkitfullscreenchange', updateFsButton);
document.addEventListener('keydown', e => {
  if (e.key === 'Escape' && feedStage.classList.contains('fs-fallback')) exitFallbackFullscreen();
});

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

  if (activeView === 'feed' && !feedLoading && !feedTimer) scheduleFeed(0);

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

  const currentSequence = c.consecutiveFailures
    ? plural(c.consecutiveFailures, 'falha consecutiva', 'falhas consecutivas')
    : plural(c.consecutiveSuccesses, 'sucesso seguido', 'sucessos seguidos');
  $('v-checks').textContent = c.totalChecks;
  $('m-checks').textContent = plural(c.failedChecks, 'falha no total', 'falhas no total') + ' · '
    + currentSequence + ' · '
    + plural(c.discoveryRuns, 'descoberta', 'descobertas') + ' · '
    + plural(c.subnetScanRuns, 'varredura', 'varreduras');

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
  renderResources(d);

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

function activateView(name, updateHash) {
  const nextView = name === 'resources' || name === 'feed' ? name : 'camera';
  activeView = nextView;
  document.querySelectorAll('[data-view-panel]').forEach(panel => {
    panel.hidden = panel.dataset.viewPanel !== nextView;
  });
  document.querySelectorAll('.tab[data-view]').forEach(tab => {
    tab.setAttribute('aria-selected', tab.dataset.view === nextView ? 'true' : 'false');
  });
  const viewTitle = nextView === 'resources'
    ? 'Recursos'
    : nextView === 'feed' ? 'Prévia' : 'Câmera';
  document.title = viewTitle + ' · Camera Watchdog';

  if (nextView === 'feed') {
    $('feedState').textContent = cameraKnown
      ? 'Atualizando a imagem…'
      : 'Aguardando a câmera ser localizada…';
    $('feedState').classList.remove('hidden');
    $('feedStamp').textContent = 'aguardando imagem';
    scheduleFeed(0);
  } else {
    if (isNativeFullscreen()) (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    if (feedStage.classList.contains('fs-fallback')) exitFallbackFullscreen();
    clearTimeout(feedTimer);
    feedTimer = null;
    const frame = $('cameraFeed');
    frame.onload = null;
    frame.onerror = null;
    frame.removeAttribute('src');
    frame.classList.remove('ready');
    feedLoading = false;
    feedFailures = 0;
  }

  const nextHash = '#' + nextView;
  if (updateHash && location.hash !== nextHash) location.hash = nextHash;
}

document.querySelectorAll('.tab[data-view]').forEach(tab => {
  tab.addEventListener('click', () => activateView(tab.dataset.view, true));
});

window.addEventListener('hashchange', () => {
  const hashView = location.hash === '#resources'
    ? 'resources'
    : location.hash === '#feed' ? 'feed' : 'camera';
  activateView(hashView, false);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearTimeout(feedTimer);
    feedTimer = null;
    return;
  }

  if (activeView === 'feed') scheduleFeed(0);
});

activateView(activeView, false);
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)HTML";
