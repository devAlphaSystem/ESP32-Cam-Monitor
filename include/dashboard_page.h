#pragma once

#include <Arduino.h>

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
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:14px 0 0}
.grid.grid-3{grid-template-columns:repeat(3,minmax(0,1fr))}
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

/* configuracoes */
.settings-kpi{min-width:170px;padding:13px 16px;border:1px solid var(--line);border-radius:11px;
background:rgba(255,255,255,.025);text-align:right}
.settings-kpi span{display:block;color:var(--dim);font-size:10.5px;letter-spacing:.07em;text-transform:uppercase}
.settings-kpi b{display:block;margin-top:4px;color:var(--info);font-size:21px;letter-spacing:-.02em}
.settings-layout{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:14px}
.settings-card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);padding:18px}
.settings-card-h{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:15px}
.settings-card-h h2{font-size:15px;color:var(--text)}
.settings-card-h p{margin:3px 0 0;color:var(--dim);font-size:12.5px}
.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:13px}
.field{min-width:0}.field.wide{grid-column:1/-1}
.field label{display:flex;align-items:baseline;justify-content:space-between;gap:8px;margin-bottom:6px;
color:var(--muted);font-size:12.5px;font-weight:620}
.field label span{color:var(--dim);font-size:10.5px;font-weight:500;white-space:nowrap}
.field input[type="text"],.field input[type="password"],.field input[type="number"]{width:100%;height:40px;
border:1px solid var(--line2);border-radius:9px;background:#0e141d;color:var(--text);padding:0 11px;
font:13px/1.2 var(--mono);outline:none;transition:.16s ease}
.field input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(56,189,248,.11)}
.field input:disabled{opacity:.55;cursor:not-allowed}
.field small{display:block;margin-top:5px;color:var(--dim);font-size:10.8px;line-height:1.45}
.checkline{display:flex;align-items:flex-start;gap:10px;padding:10px 11px;border:1px solid var(--line);
border-radius:9px;background:rgba(255,255,255,.018)}
.checkline input{width:17px;height:17px;margin:2px 0 0;accent-color:var(--ok);flex:none}
.checkline label{margin:0;color:var(--muted);font-size:12.5px;cursor:pointer}
.checkline label b{display:block;color:var(--text);font-size:12.5px;font-weight:620}
.checkline label span{display:block;margin-top:2px;color:var(--dim);font-size:10.8px;line-height:1.4}
.estimate-box{grid-column:1/-1;padding:12px 13px;border:1px solid rgba(96,165,250,.24);border-radius:9px;
background:rgba(96,165,250,.065);color:var(--muted);font-size:12px}
.estimate-box b{color:var(--info)}
.settings-advanced{margin-top:14px}
.settings-advanced>summary{padding:15px 18px}
.settings-advanced-body{padding:0 14px 14px}
.settings-savebar{display:flex;flex-wrap:wrap;align-items:end;justify-content:space-between;gap:14px;margin-top:14px;
background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);padding:16px 18px}
.settings-savebar .field{flex:1 1 260px;max-width:460px}
.button-row{display:flex;flex-wrap:wrap;gap:9px;align-items:center}
.btn{min-height:39px;border:1px solid var(--line2);border-radius:9px;background:var(--surface2);color:var(--text);
padding:8px 14px;cursor:pointer;font:inherit;font-size:12.5px;font-weight:650;transition:.16s ease}
.btn:hover{border-color:#456078;background:#202b3a}.btn:disabled{opacity:.48;cursor:wait}
.btn.primary{background:#166c91;border-color:#2388b3;color:#fff}.btn.primary:hover{background:#197da7}
.btn.warn{border-color:rgba(251,191,36,.35);color:var(--warn);background:rgba(251,191,36,.07)}
.btn.bad{border-color:rgba(248,113,113,.35);color:var(--bad);background:rgba(248,113,113,.07)}
.form-result{flex:1 1 100%;min-height:20px;color:var(--dim);font-size:12px}
.form-result.ok{color:var(--ok)}.form-result.warn{color:var(--warn)}.form-result.bad{color:var(--bad)}
.action-grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:10px;margin-top:12px}
.action-item{display:flex;flex-direction:column;align-items:flex-start;gap:8px;padding:13px;border:1px solid var(--line);
border-radius:10px;background:rgba(255,255,255,.018)}
.action-item b{font-size:12.5px}.action-item span{color:var(--dim);font-size:10.8px;line-height:1.4;min-height:31px}
.action-item .btn{width:100%;margin-top:auto}

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

@media(max-width:1180px){
.grid{grid-template-columns:repeat(2,minmax(0,1fr))}
.grid.grid-3{grid-template-columns:repeat(2,minmax(0,1fr))}
.action-grid{grid-template-columns:repeat(3,minmax(0,1fr))}
}
@media(max-width:560px){
main{padding:14px 12px 36px}
.top-tools{width:100%;justify-content:space-between}
.tabs{flex:1}.tab{flex:1}
.hero{padding:18px;gap:16px}
.hero-name{font-size:23px}
.gauge{width:96px;height:96px;margin:0}
.grid{grid-template-columns:1fr;gap:10px}
.grid.grid-3{grid-template-columns:1fr}
.settings-layout,.form-grid,.action-grid{grid-template-columns:1fr}
.field.wide{grid-column:auto}
.settings-savebar{align-items:stretch}.settings-savebar .field{max-width:none}
.button-row{width:100%}.button-row .btn{flex:1 1 auto}
.settings-kpi{width:100%;text-align:left}
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
      <button class="tab" id="settingsTab" type="button" role="tab" data-view="settings" aria-controls="settingsView" aria-selected="false">Configurações</button>
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
    <div class="card-h">Imagem JPEG<i class="pip" id="p-image"></i></div>
    <div class="card-v" id="v-image">—</div>
    <div class="card-m" id="m-image">—</div>
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
    <div class="row"><dt>Último JPEG</dt><dd id="l-image">—</dd></div>
    <div class="row"><dt>Recuperação automática</dt><dd id="l-reboot">—</dd></div>
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

  <div class="grid grid-3">
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

<div id="settingsView" role="tabpanel" aria-labelledby="settingsTab" data-view-panel="settings" hidden>
  <section class="hero" id="settingsHero" style="--sc:var(--info);--sbg:rgba(96,165,250,.13);--sbd:rgba(96,165,250,.28)">
    <div class="hero-main">
      <span class="badge"><i></i><span id="settingsSource">carregando configurações</span></span>
      <h2 class="hero-name">Configurações do watchdog</h2>
      <div class="hero-sub" id="settingsMeta">Valores persistentes na NVS, validados também pelo firmware.</div>
    </div>
    <div class="settings-kpi">
      <span>alerta de imagem estimado</span>
      <b id="settingsAlertEstimate">—</b>
    </div>
  </section>

  <form id="settingsForm" autocomplete="off">
    <div class="settings-layout">
      <section class="settings-card">
        <div class="settings-card-h"><div><h2>Identificação e interface</h2><p>Nome, perfil JPEG e ritmo do painel.</p></div></div>
        <div class="form-grid">
          <div class="field wide"><label for="cfgCameraName">Nome da câmera <span>1–48 caracteres</span></label><input id="cfgCameraName" type="text" minlength="1" maxlength="48" required><small>Exibido no painel e nos assuntos dos e-mails.</small></div>
          <div class="field"><label for="cfgSnapshotProfileMatch">Perfil ONVIF preferido <span>até 32 caracteres</span></label><input id="cfgSnapshotProfileMatch" type="text" maxlength="32" placeholder="sub"><small>Vazio usa o primeiro perfil; “sub” normalmente reduz a carga.</small></div>
          <div class="field"><label for="cfgCameraRtspPort">Porta RTSP <span>1–65535</span></label><input id="cfgCameraRtspPort" type="number" min="1" max="65535" step="1" required><small>Padrão: 554.</small></div>
          <div class="field"><label for="cfgFeedMinimum">Intervalo mínimo da prévia <span>segundos</span></label><input id="cfgFeedMinimum" type="number" min="0.5" max="10" step="0.1" required><small>Padrão: 1,5 s.</small></div>
          <div class="field"><label for="cfgFeedTarget">Ciclo-alvo da prévia <span>segundos</span></label><input id="cfgFeedTarget" type="number" min="1" max="30" step="0.1" required><small>Padrão: 2,5 s; nunca pode ser menor que o intervalo mínimo.</small></div>
          <div class="field"><label for="cfgStatusRefresh">Atualização do painel <span>segundos</span></label><input id="cfgStatusRefresh" type="number" min="1" max="30" step="0.5" required><small>Padrão: 3 s.</small></div>
        </div>
      </section>

      <section class="settings-card">
        <div class="settings-card-h"><div><h2>Monitoramento</h2><p>Confirmação de falhas e recuperações.</p></div></div>
        <div class="form-grid">
          <div class="field"><label for="cfgHealthCheckInterval">Intervalo entre verificações <span>10–3600 s</span></label><input id="cfgHealthCheckInterval" type="number" min="10" max="3600" step="1" required><small>Padrão: 30 s.</small></div>
          <div class="field"><label for="cfgStartupGrace">Tolerância após ligar <span>0–600 s</span></label><input id="cfgStartupGrace" type="number" min="0" max="600" step="1" required><small>Padrão: 45 s.</small></div>
          <div class="field"><label for="cfgFailuresBeforeRediscovery">Falhas para redescobrir <span>1–20</span></label><input id="cfgFailuresBeforeRediscovery" type="number" min="1" max="20" step="1" required><small>Padrão: 2.</small></div>
          <div class="field"><label for="cfgFailuresBeforeOffline">Falhas para declarar offline <span>1–20</span></label><input id="cfgFailuresBeforeOffline" type="number" min="1" max="20" step="1" required><small>Padrão: 3; deve ser igual ou maior que a redescoberta.</small></div>
          <div class="field"><label for="cfgSuccessesBeforeRecovery">Sucessos para confirmar recuperação <span>1–10</span></label><input id="cfgSuccessesBeforeRecovery" type="number" min="1" max="10" step="1" required><small>Padrão: 2.</small></div>
          <div class="field"><label for="cfgVideoFailuresBeforeAlert">Falhas JPEG para alerta <span>1–20</span></label><input id="cfgVideoFailuresBeforeAlert" type="number" min="1" max="20" step="1" required><small>Padrão: 3.</small></div>
          <div class="field"><label for="cfgVideoFailuresBeforeReboot">Falhas JPEG para reboot <span>1–20</span></label><input id="cfgVideoFailuresBeforeReboot" type="number" min="1" max="20" step="1" required><small>Padrão: 3; não pode anteceder o alerta.</small></div>
          <div class="estimate-box">Com estes valores: alerta de imagem em aproximadamente <b id="estimateAlert">—</b>, reboot em <b id="estimateReboot">—</b> e offline em <b id="estimateOffline">—</b>.</div>
        </div>
      </section>

      <section class="settings-card">
        <div class="settings-card-h"><div><h2>Recuperação automática</h2><p>Limites contra ciclos de reinicialização.</p></div></div>
        <div class="form-grid">
          <div class="field wide"><div class="checkline"><input id="cfgAutoReboot" type="checkbox"><label for="cfgAutoReboot"><b>Permitir ONVIF SystemReboot automático</b><span>O reboot manual continua respeitando cooldown e limite da janela.</span></label></div></div>
          <div class="field"><label for="cfgRebootGrace">Tolerância após reboot <span>30–600 s</span></label><input id="cfgRebootGrace" type="number" min="30" max="600" step="1" required><small>Padrão: 90 s.</small></div>
          <div class="field"><label for="cfgRebootCooldown">Cooldown entre reboots <span>5–1440 min</span></label><input id="cfgRebootCooldown" type="number" min="5" max="1440" step="1" required><small>Padrão: 15 min.</small></div>
          <div class="field"><label for="cfgRebootWindow">Janela de contabilização <span>1–168 h</span></label><input id="cfgRebootWindow" type="number" min="1" max="168" step="1" required><small>Padrão: 24 h; deve ser maior ou igual ao cooldown.</small></div>
          <div class="field"><label for="cfgMaxReboots">Máximo de reboots na janela <span>1–20</span></label><input id="cfgMaxReboots" type="number" min="1" max="20" step="1" required><small>Padrão: 3.</small></div>
        </div>
      </section>

      <section class="settings-card">
        <div class="settings-card-h"><div><h2>Rede e notificações</h2><p>Descoberta da câmera e repetição dos alertas.</p></div></div>
        <div class="form-grid">
          <div class="field wide"><div class="checkline"><input id="cfgAutoScan" type="checkbox"><label for="cfgAutoScan"><b>Permitir varredura automática da sub-rede</b><span>Usada apenas após falhas confirmadas e limitada a 254 hosts.</span></label></div></div>
          <div class="field wide"><div class="checkline"><input id="cfgEmailAlerts" type="checkbox"><label for="cfgEmailAlerts"><b>Ativar alertas por e-mail</b><span>Desativar cancela notificações pendentes, mas não apaga as credenciais do Resend.</span></label></div></div>
          <div class="field wide"><div class="checkline"><input id="cfgEmailIpChange" type="checkbox"><label for="cfgEmailIpChange"><b>Avisar quando a câmera mudar de IP</b><span>Aplicado somente quando os alertas por e-mail estiverem ativos.</span></label></div></div>
          <div class="field"><label for="cfgWifiTimeout">Timeout inicial do Wi-Fi <span>5–120 s</span></label><input id="cfgWifiTimeout" type="number" min="5" max="120" step="1" required><small>Usado no próximo boot.</small></div>
          <div class="field"><label for="cfgDiscoveryTimeout">Tempo do WS-Discovery <span>1–15 s</span></label><input id="cfgDiscoveryTimeout" type="number" min="1" max="15" step="0.1" required><small>Padrão: 3,5 s.</small></div>
          <div class="field"><label for="cfgRediscoveryInterval">Intervalo entre redescobertas <span>30–3600 s</span></label><input id="cfgRediscoveryInterval" type="number" min="30" max="3600" step="1" required><small>Padrão: 60 s.</small></div>
          <div class="field"><label for="cfgSubnetScanInterval">Intervalo entre varreduras <span>1–1440 min</span></label><input id="cfgSubnetScanInterval" type="number" min="1" max="1440" step="1" required><small>Padrão: 5 min.</small></div>
          <div class="field"><label for="cfgEmailRetry">Repetição de tentativa de e-mail <span>30–3600 s</span></label><input id="cfgEmailRetry" type="number" min="30" max="3600" step="1" required><small>Padrão: 60 s.</small></div>
        </div>
      </section>
    </div>

    <details class="settings-advanced">
      <summary>Parâmetros avançados de comunicação e hardware</summary>
      <div class="settings-advanced-body">
        <div class="settings-layout">
          <section class="settings-card">
            <div class="settings-card-h"><div><h2>Timeouts</h2><p>Aumente somente para câmeras ou redes lentas.</p></div></div>
            <div class="form-grid">
              <div class="field"><label for="cfgTcpTimeout">Conexão TCP <span>200–5000 ms</span></label><input id="cfgTcpTimeout" type="number" min="200" max="5000" step="50" required><small>Padrão: 900 ms.</small></div>
              <div class="field"><label for="cfgServiceTimeout">Resposta ONVIF/RTSP <span>500–10000 ms</span></label><input id="cfgServiceTimeout" type="number" min="500" max="10000" step="100" required><small>Padrão: 1500 ms.</small></div>
              <div class="field"><label for="cfgScanTimeout">Conexão durante varredura <span>20–1000 ms</span></label><input id="cfgScanTimeout" type="number" min="20" max="1000" step="10" required><small>Padrão: 90 ms.</small></div>
              <div class="field"><label for="cfgSnapshotTimeout">Timeout do JPEG <span>1–30 s</span></label><input id="cfgSnapshotTimeout" type="number" min="1" max="30" step="0.5" required><small>Padrão: 7 s.</small></div>
              <div class="field"><label for="cfgSnapshotResolveRetry">Nova resolução do URI <span>5–600 s</span></label><input id="cfgSnapshotResolveRetry" type="number" min="5" max="600" step="1" required><small>Padrão: 30 s.</small></div>
              <div class="field"><label for="cfgSnapshotFailuresReresolve">Falhas antes de resolver o URI <span>1–10</span></label><input id="cfgSnapshotFailuresReresolve" type="number" min="1" max="10" step="1" required><small>Padrão: 2.</small></div>
            </div>
          </section>
          <section class="settings-card">
            <div class="settings-card-h"><div><h2>Alerta local</h2><p>LED integrado do ESP32.</p></div></div>
            <div class="form-grid">
              <div class="field wide"><div class="checkline"><input id="cfgLedEnabled" type="checkbox"><label for="cfgLedEnabled"><b>Piscar o LED quando não estiver online</b><span>O GPIO permanece fixo no firmware para evitar conflito de hardware.</span></label></div></div>
              <div class="field"><label for="cfgLedBlink">Intervalo da piscada <span>200–10000 ms</span></label><input id="cfgLedBlink" type="number" min="200" max="10000" step="100" required><small>Padrão: 1000 ms.</small></div>
            </div>
          </section>
        </div>
      </div>
    </details>

    <div class="settings-savebar">
      <div class="field" id="adminPasswordField" hidden><label for="cfgAdminPassword">Senha administrativa <span>não é armazenada no navegador</span></label><input id="cfgAdminPassword" type="password" maxlength="96" autocomplete="current-password"><small>Definida por DASHBOARD_ADMIN_PASSWORD_VALUE em secrets.h.</small></div>
      <div class="button-row">
        <button class="btn warn" id="settingsReset" type="button" disabled>Restaurar padrões</button>
        <button class="btn primary" id="settingsSave" type="submit" disabled>Salvar configurações</button>
      </div>
      <div class="form-result" id="settingsFormStatus">Carregue a aba para consultar os valores atuais.</div>
    </div>
  </form>

  <section class="settings-card sec">
    <div class="settings-card-h"><div><h2>Ações administrativas</h2><p>Executadas pelo loop principal; ações sensíveis pedem confirmação.</p></div></div>
    <div class="action-grid">
      <div class="action-item"><b>Verificar agora</b><span>Executa ONVIF, RTSP e validação JPEG.</span><button class="btn" type="button" data-admin-action="health-check" disabled>Executar</button></div>
      <div class="action-item"><b>Redescobrir câmera</b><span>Repete WS-Discovery e confirma o endpoint.</span><button class="btn" type="button" data-admin-action="rediscover" disabled>Redescobrir</button></div>
      <div class="action-item"><b>Reiniciar câmera</b><span>Envia ONVIF SystemReboot respeitando os limites.</span><button class="btn warn" type="button" data-admin-action="reboot-camera" disabled>Reiniciar câmera</button></div>
      <div class="action-item"><b>Limpar limitador</b><span>Zera cooldown e tentativas da janela atual.</span><button class="btn warn" type="button" data-admin-action="reset-reboot-limiter" disabled>Limpar limite</button></div>
      <div class="action-item"><b>Reiniciar ESP32</b><span>Reinicia somente o watchdog, sem cortar a câmera.</span><button class="btn bad" type="button" data-admin-action="reboot-esp32" disabled>Reiniciar ESP32</button></div>
    </div>
    <div class="form-result" id="adminActionStatus">Nenhuma ação solicitada nesta sessão.</div>
  </section>
</div>

<footer id="foot">ESP32 Camera Watchdog</footer>
</main>

<script>
const $ = id => document.getElementById(id);
const PALETTE = {
  online:    ['Online',      'var(--ok)',   'rgba(52,211,153,.13)', 'rgba(52,211,153,.28)'],
  degraded:  ['Instável',    'var(--warn)', 'rgba(251,191,36,.13)', 'rgba(251,191,36,.28)'],
  recovering:['Recuperando', 'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)'],
  offline:   ['Offline',     'var(--bad)',  'rgba(248,113,113,.13)','rgba(248,113,113,.28)'],
  discovering:['Procurando', 'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)'],
  starting:  ['Iniciando',   'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)']
};
const HISTORY_LIMIT = 60;
const resourceHistory = { heap: [], rssi: [] };
let activeView = location.hash === '#settings'
  ? 'settings'
  : location.hash === '#resources'
    ? 'resources'
    : location.hash === '#feed' ? 'feed' : 'camera';
let feedMinimumIntervalMs = 1500;
let feedTargetCycleMs = 2500;
let statusRefreshIntervalMs = 3000;
let statusRefreshTimer;
let settingsLoaded = false;
let settingsLoading = false;
let settingsBusy = false;
let settingsWriteToken = '';
let settingsPasswordRequired = false;

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

function setImageProbe(snapshot, recovery) {
  const value = $('v-image'), meta = $('m-image');

  if (recovery && recovery.graceActive) {
    value.textContent = 'reiniciando';
    value.className = 'card-v warn';
    pip('p-image', 'warn');
    meta.textContent = 'aguardando a câmera por mais ' + duration(recovery.graceRemainingSeconds);
    return;
  }

  if (!snapshot || !snapshot.attempted) {
    value.textContent = 'aguardando';
    value.className = 'card-v info';
    pip('p-image', 'info');
    meta.textContent = 'o primeiro JPEG ainda não foi testado';
    return;
  }

  if (snapshot.healthy) {
    value.textContent = 'JPEG válido';
    value.className = 'card-v ok';
    pip('p-image', 'ok');
    meta.textContent = formatBytes(snapshot.bytesRead) + ' em ' + snapshot.latencyMs
      + ' ms' + (snapshot.profile ? ' · perfil ' + snapshot.profile : '');
    return;
  }

  value.textContent = snapshot.httpResponded ? 'sem imagem' : 'indisponível';
  value.className = 'card-v bad';
  pip('p-image', 'bad');
  meta.textContent = snapshot.detail || 'nenhum JPEG completo foi recebido';
}

let lastOk = 0;
let cameraKnown = false;
let cameraRecovering = false;
let feedLoading = false;
let feedFailures = 0;
let feedTimer;
let feedRequestedAtMs = 0;
// Os intervalos da previa sao atualizados pelo JSON do firmware.

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
  if (cameraRecovering) {
    $('cameraFeed').classList.remove('ready');
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'A câmera está reiniciando; a prévia será retomada automaticamente…';
    $('feedStamp').textContent = 'recuperação automática em andamento';
    scheduleFeed(5000);
    return;
  }
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
    scheduleFeed(Math.max(feedMinimumIntervalMs, feedTargetCycleMs - elapsed));
  };
  frame.onerror = () => {
    feedLoading = false;
    feedFailures++;
    frame.classList.remove('ready');
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

const SETTINGS_FIELDS = {
  cameraName: { id: 'cfgCameraName', kind: 'text' },
  snapshotProfileMatch: { id: 'cfgSnapshotProfileMatch', kind: 'text' },
  cameraRtspPort: { id: 'cfgCameraRtspPort' },
  healthCheckIntervalMs: { id: 'cfgHealthCheckInterval', scale: 1000 },
  startupGraceMs: { id: 'cfgStartupGrace', scale: 1000 },
  failuresBeforeRediscovery: { id: 'cfgFailuresBeforeRediscovery' },
  failuresBeforeOffline: { id: 'cfgFailuresBeforeOffline' },
  successesBeforeRecovery: { id: 'cfgSuccessesBeforeRecovery' },
  videoFailuresBeforeAlert: { id: 'cfgVideoFailuresBeforeAlert' },
  videoFailuresBeforeReboot: { id: 'cfgVideoFailuresBeforeReboot' },
  enableOnvifAutoReboot: { id: 'cfgAutoReboot', kind: 'bool' },
  onvifRebootGraceMs: { id: 'cfgRebootGrace', scale: 1000 },
  onvifRebootCooldownMs: { id: 'cfgRebootCooldown', scale: 60000 },
  onvifRebootWindowMs: { id: 'cfgRebootWindow', scale: 3600000 },
  maxOnvifRebootAttemptsPerWindow: { id: 'cfgMaxReboots' },
  wifiConnectTimeoutMs: { id: 'cfgWifiTimeout', scale: 1000 },
  enableAutomaticSubnetScan: { id: 'cfgAutoScan', kind: 'bool' },
  discoveryTimeoutMs: { id: 'cfgDiscoveryTimeout', scale: 1000 },
  rediscoveryIntervalMs: { id: 'cfgRediscoveryInterval', scale: 1000 },
  subnetScanIntervalMs: { id: 'cfgSubnetScanInterval', scale: 60000 },
  enableEmailAlerts: { id: 'cfgEmailAlerts', kind: 'bool' },
  emailOnIpChange: { id: 'cfgEmailIpChange', kind: 'bool' },
  emailRetryIntervalMs: { id: 'cfgEmailRetry', scale: 1000 },
  tcpConnectTimeoutMs: { id: 'cfgTcpTimeout' },
  serviceResponseTimeoutMs: { id: 'cfgServiceTimeout' },
  scanConnectTimeoutMs: { id: 'cfgScanTimeout' },
  snapshotHttpTimeoutMs: { id: 'cfgSnapshotTimeout', scale: 1000 },
  snapshotResolveRetryIntervalMs: { id: 'cfgSnapshotResolveRetry', scale: 1000 },
  snapshotFailuresBeforeReresolve: { id: 'cfgSnapshotFailuresReresolve' },
  enableCameraAlertLed: { id: 'cfgLedEnabled', kind: 'bool' },
  cameraAlertLedBlinkIntervalMs: { id: 'cfgLedBlink' },
  feedMinIntervalMs: { id: 'cfgFeedMinimum', scale: 1000 },
  feedTargetCycleMs: { id: 'cfgFeedTarget', scale: 1000 },
  statusRefreshIntervalMs: { id: 'cfgStatusRefresh', scale: 1000 }
};

function formatSettingNumber(value) {
  const rounded = Math.round(value * 1000) / 1000;
  return String(rounded);
}

function setSettingsStatus(message, tone) {
  const target = $('settingsFormStatus');
  target.textContent = message;
  target.className = 'form-result' + (tone ? ' ' + tone : '');
}

function setAdminActionStatus(message, tone) {
  const target = $('adminActionStatus');
  target.textContent = message;
  target.className = 'form-result' + (tone ? ' ' + tone : '');
}

function setMutationControlsEnabled(enabled) {
  $('settingsSave').disabled = !enabled;
  $('settingsReset').disabled = !enabled;
  document.querySelectorAll('[data-admin-action]').forEach(button => {
    button.disabled = !enabled;
  });
}

function settingsAuthorizationReady() {
  if (settingsWriteToken) return true;
  setSettingsStatus('As configurações ainda não foram carregadas. Aguarde e tente novamente.', 'bad');
  setAdminActionStatus('As ações permanecem bloqueadas até a leitura das configurações.', 'bad');
  loadSettings(true);
  return false;
}

function updateSettingsEstimates() {
  const interval = Number($('cfgHealthCheckInterval').value || 0);
  const alertFailures = Number($('cfgVideoFailuresBeforeAlert').value || 0);
  const rebootFailures = Number($('cfgVideoFailuresBeforeReboot').value || 0);
  const offlineFailures = Number($('cfgFailuresBeforeOffline').value || 0);
  const alertSeconds = interval * alertFailures;
  $('estimateAlert').textContent = duration(alertSeconds);
  $('estimateReboot').textContent = duration(interval * rebootFailures);
  $('estimateOffline').textContent = duration(interval * offlineFailures);
  $('settingsAlertEstimate').textContent = alertSeconds ? duration(alertSeconds) : '—';
}

function populateSettings(payload) {
  const values = payload.values || {};
  Object.entries(SETTINGS_FIELDS).forEach(([key, descriptor]) => {
    const input = $(descriptor.id);
    if (!input || !(key in values)) return;
    if (descriptor.kind === 'bool') {
      input.checked = Boolean(values[key]);
    } else if (descriptor.kind === 'text') {
      input.value = values[key] == null ? '' : values[key];
    } else {
      input.value = formatSettingNumber(Number(values[key]) / (descriptor.scale || 1));
    }
  });

  settingsWriteToken = payload.security ? payload.security.writeToken || '' : '';
  settingsPasswordRequired = Boolean(payload.security && payload.security.passwordRequired);
  $('adminPasswordField').hidden = !settingsPasswordRequired;
  $('cfgAdminPassword').required = settingsPasswordRequired;
  $('settingsSource').textContent = payload.source === 'nvs'
    ? 'configurações salvas na NVS'
    : 'valores padrão de fábrica';
  $('settingsMeta').textContent = 'esquema ' + payload.schemaVersion + ' · revisão ' + payload.revision
    + ' · ' + (payload.lastResult || 'configurações carregadas');

  feedMinimumIntervalMs = Number(values.feedMinIntervalMs) || feedMinimumIntervalMs;
  feedTargetCycleMs = Number(values.feedTargetCycleMs) || feedTargetCycleMs;
  statusRefreshIntervalMs = Number(values.statusRefreshIntervalMs) || statusRefreshIntervalMs;
  settingsLoaded = true;
  updateSettingsEstimates();
  setMutationControlsEnabled(true);
  setSettingsStatus('Configurações carregadas. Alterações só são gravadas ao clicar em Salvar.', 'ok');
}

async function loadSettings(force) {
  if (settingsLoading || (settingsLoaded && !force)) return;
  settingsLoading = true;
  setSettingsStatus('Lendo configurações do ESP32…', 'warn');
  try {
    const response = await fetch('/api/settings', { cache: 'no-store' });
    const payload = await response.json();
    if (!response.ok || !payload.ok) throw new Error(payload.detail || 'HTTP ' + response.status);
    populateSettings(payload);
  } catch (error) {
    setMutationControlsEnabled(false);
    setSettingsStatus('Falha ao carregar: ' + error.message, 'bad');
  } finally {
    settingsLoading = false;
  }
}

function collectMutationParams(includeSettings) {
  const params = new URLSearchParams();
  params.set('writeToken', settingsWriteToken);
  if (settingsPasswordRequired) params.set('adminPassword', $('cfgAdminPassword').value);

  if (includeSettings) {
    Object.entries(SETTINGS_FIELDS).forEach(([key, descriptor]) => {
      const input = $(descriptor.id);
      if (descriptor.kind === 'bool') {
        params.set(key, input.checked ? '1' : '0');
      } else if (descriptor.kind === 'text') {
        params.set(key, input.value.trim());
      } else {
        const numeric = Number(input.value);
        params.set(key, String(Math.round(numeric * (descriptor.scale || 1))));
      }
    });
  }
  return params;
}

async function postForm(url, params) {
  const response = await fetch(url, {
    method: 'POST',
    cache: 'no-store',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: params.toString()
  });
  let payload = {};
  try { payload = await response.json(); } catch (_) {}
  if (!response.ok || payload.ok === false) {
    throw new Error(payload.detail || payload.message || 'HTTP ' + response.status);
  }
  return payload;
}

$('settingsForm').addEventListener('input', updateSettingsEstimates);
$('settingsForm').addEventListener('submit', async event => {
  event.preventDefault();
  if (settingsBusy || !$('settingsForm').reportValidity() || !settingsAuthorizationReady()) return;
  settingsBusy = true;
  $('settingsSave').disabled = true;
  $('settingsReset').disabled = true;
  setSettingsStatus('Validando e gravando na NVS…', 'warn');
  try {
    const payload = await postForm('/api/settings', collectMutationParams(true));
    populateSettings(payload);
    setSettingsStatus('Configurações salvas e aplicadas.', 'ok');
    refresh();
  } catch (error) {
    setSettingsStatus('Não foi possível salvar: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
});

$('settingsReset').addEventListener('click', async () => {
  if (settingsBusy || !settingsAuthorizationReady() || !confirm('Restaurar todos os valores ajustáveis para os padrões de fábrica?')) return;
  settingsBusy = true;
  $('settingsSave').disabled = true;
  $('settingsReset').disabled = true;
  setSettingsStatus('Restaurando os valores de fábrica…', 'warn');
  try {
    const payload = await postForm('/api/settings/reset', collectMutationParams(false));
    populateSettings(payload);
    setSettingsStatus('Padrões restaurados e aplicados.', 'ok');
    refresh();
  } catch (error) {
    setSettingsStatus('Não foi possível restaurar: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
});

const ADMIN_ACTIONS = {
  'health-check': ['/api/actions/health-check', 'Verificação manual solicitada.', false],
  rediscover: ['/api/actions/rediscover', 'Redescoberta solicitada.', false],
  'reboot-camera': ['/api/actions/reboot-camera', 'Reboot ONVIF solicitado.', true],
  'reset-reboot-limiter': ['/api/actions/reset-reboot-limiter', 'Limitador de reboots será limpo.', true],
  'reboot-esp32': ['/api/actions/reboot-esp32', 'O ESP32 será reiniciado.', true]
};

document.querySelectorAll('[data-admin-action]').forEach(button => {
  button.addEventListener('click', async () => {
    const action = ADMIN_ACTIONS[button.dataset.adminAction];
    if (!action || settingsBusy || !settingsAuthorizationReady()) return;
    if (action[2] && !confirm(action[1] + ' Continuar?')) return;
    settingsBusy = true;
    document.querySelectorAll('[data-admin-action]').forEach(item => { item.disabled = true; });
    setAdminActionStatus('Enviando ação ao ESP32…', 'warn');
    try {
      const payload = await postForm(action[0], collectMutationParams(false));
      setAdminActionStatus(payload.message || action[1], 'ok');
      setTimeout(refresh, 700);
    } catch (error) {
      setAdminActionStatus('Ação recusada: ' + error.message, 'bad');
    } finally {
      settingsBusy = false;
      setMutationControlsEnabled(Boolean(settingsWriteToken));
    }
  });
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
  cameraRecovering = Boolean(d.recovery && d.recovery.graceActive);

  if (d.ui) {
    feedMinimumIntervalMs = Number(d.ui.feedMinimumIntervalMs) || feedMinimumIntervalMs;
    feedTargetCycleMs = Number(d.ui.feedTargetCycleMs) || feedTargetCycleMs;
    statusRefreshIntervalMs = Number(d.ui.statusRefreshIntervalMs) || statusRefreshIntervalMs;
  }

  if (d.adminAction && d.adminAction.lastResult && activeView === 'settings') {
    setAdminActionStatus(d.adminAction.lastResult, d.adminAction.pending === 'none' ? '' : 'warn');
  }

  if (d.snapshot) {
    $('feedMeta').textContent = d.snapshot.healthy
      ? 'Perfil ' + (d.snapshot.profile || 'ONVIF') + ' · último teste com '
        + formatBytes(d.snapshot.bytesRead) + ' · ' + d.snapshot.served + ' quadros entregues'
      : d.recovery && d.recovery.graceActive
        ? 'A câmera está reiniciando; a prévia será retomada automaticamente'
        : 'Imagem indisponível · ' + (d.snapshot.detail || 'aguardando novo teste');
  }

  if (activeView === 'feed' && !feedLoading && !feedTimer) scheduleFeed(0);

  const c = d.counters;
  const rate = c.totalChecks ? c.successfulChecks / c.totalChecks * 100 : 0;
  $('arc').style.setProperty('--p', c.totalChecks ? rate : 0);
  $('availValue').textContent = c.totalChecks ? rate.toFixed(rate >= 99.95 ? 0 : 1) + '%' : '—';

  const p = d.probes;
  setProbe('onvif', p.onvifTcp, p.onvifHttp, p.onvifStatus, p.onvifLatencyMs, 'HTTP');
  setProbe('rtsp', p.rtspTcp, p.rtspProtocol, p.rtspStatus, p.rtspLatencyMs, 'RTSP');
  setImageProbe(d.snapshot, d.recovery);

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
    + currentSequence;

  $('l-event').textContent = d.lastEvent;
  $('l-url').textContent = d.camera.onvifUrl || 'desconhecido';
  $('l-image').textContent = d.snapshot.lastResult || d.snapshot.detail || 'ainda não testado';
  $('l-reboot').textContent = d.recovery.lastResult
    + (d.recovery.graceActive ? ' · aguardando ' + duration(d.recovery.graceRemainingSeconds) : '');
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

function scheduleStatusRefresh(delay) {
  clearTimeout(statusRefreshTimer);
  if (document.hidden) return;
  statusRefreshTimer = setTimeout(refresh, delay == null ? statusRefreshIntervalMs : delay);
}

async function refresh() {
  clearTimeout(statusRefreshTimer);
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    render(await response.json());
    $('live').classList.remove('stale');
  } catch (error) {
    $('live').classList.add('stale');
    $('liveText').textContent = 'sem resposta do dispositivo';
  } finally {
    scheduleStatusRefresh(statusRefreshIntervalMs);
  }
}

setInterval(() => {
  if (!lastOk || $('live').classList.contains('stale')) return;
  const ago = Math.round((Date.now() - lastOk) / 1000);
  $('liveText').textContent = ago < 2 ? 'atualizado agora' : 'atualizado há ' + ago + 's';
}, 1000);

function activateView(name, updateHash) {
  const nextView = name === 'settings' || name === 'resources' || name === 'feed'
    ? name
    : 'camera';
  activeView = nextView;
  document.querySelectorAll('[data-view-panel]').forEach(panel => {
    panel.hidden = panel.dataset.viewPanel !== nextView;
  });
  document.querySelectorAll('.tab[data-view]').forEach(tab => {
    tab.setAttribute('aria-selected', tab.dataset.view === nextView ? 'true' : 'false');
  });
  const viewTitle = nextView === 'settings'
    ? 'Configurações'
    : nextView === 'resources'
      ? 'Recursos'
      : nextView === 'feed' ? 'Prévia' : 'Câmera';
  document.title = viewTitle + ' · Camera Watchdog';

  if (nextView === 'settings') loadSettings(false);

  if (nextView === 'feed') {
    $('feedState').textContent = cameraRecovering
      ? 'A câmera está reiniciando…'
      : cameraKnown ? 'Atualizando a imagem…' : 'Aguardando a câmera ser localizada…';
    $('feedState').classList.remove('hidden');
    $('feedStamp').textContent = cameraRecovering
      ? 'recuperação automática em andamento'
      : 'aguardando imagem';
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
  const hashView = location.hash === '#settings'
    ? 'settings'
    : location.hash === '#resources'
      ? 'resources'
      : location.hash === '#feed' ? 'feed' : 'camera';
  activateView(hashView, false);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearTimeout(feedTimer);
    clearTimeout(statusRefreshTimer);
    feedTimer = null;
    return;
  }

  refresh();
  if (activeView === 'feed') scheduleFeed(0);
  if (activeView === 'settings') loadSettings(true);
});

activateView(activeView, false);
refresh();
</script>
</body>
</html>
)HTML";
