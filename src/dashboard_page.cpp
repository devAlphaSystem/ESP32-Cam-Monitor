#include <Arduino.h>

namespace CameraWatchdog {

extern const char DASHBOARD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#24262a">
<link rel="icon" type="image/svg+xml" sizes="any" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='16' fill='%23242a31'/%3E%3Cpath d='m32 12 19 8v13c0 8-10 15-19 19-9-4-19-11-19-19V20Z' fill='none' stroke='%23e9edf2' stroke-width='3' stroke-linejoin='round'/%3E%3Ccircle cx='32' cy='29' r='7' fill='none' stroke='%23e9edf2' stroke-width='3'/%3E%3Cpath d='M32 36v7' stroke='%23e9edf2' stroke-width='3'/%3E%3C/svg%3E">
<title>Camera Watchdog</title>
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
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 var(--sans);-webkit-font-smoothing:antialiased}
button,input,select{font:inherit}
button,a,input,select{-webkit-tap-highlight-color:transparent}
button,a{touch-action:manipulation}
h1,h2,h3,p{margin:0}
h1{font-size:24px;font-weight:600;line-height:1.3;letter-spacing:-.025em}
h2,h3{font-size:16px;font-weight:600;line-height:1.4}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
.skip-link{position:fixed;left:12px;top:-80px;z-index:1001;padding:12px 16px;border:1px solid var(--accent);border-radius:3px;background:var(--surface);color:var(--text)}
.skip-link:focus{top:12px}

/* Navegacao */
.navigation{border-bottom:1px solid var(--line);background:var(--surface-alt)}
.navigation-inner{display:flex;align-items:center;justify-content:space-between;gap:24px;max-width:1440px;margin:auto;padding:0 32px}
.tabs{display:flex;flex:1 1 auto;min-width:0;gap:22px;overflow-x:auto;scrollbar-width:thin}
.tab{flex:none;min-height:56px;padding:0 2px;border:0;border-bottom:2px solid transparent;background:transparent;color:var(--muted);font-size:14px;white-space:nowrap;cursor:pointer}
.tab:hover{color:var(--text);border-bottom-color:var(--line-strong)}
.tab[aria-selected="true"]{color:var(--text);border-bottom-color:var(--accent);font-weight:600}
.tab-admin-start{margin-left:auto}
.connection{display:flex;align-items:center;gap:12px;flex:none;padding-left:24px;border-left:1px solid var(--line);font-size:12px}
.connection>span{color:var(--muted)}
.status-pill{display:flex;align-items:center;gap:7px;color:var(--muted);font-size:12px}
.beat{width:6px;height:6px;background:var(--ok);border-radius:50%;flex:none}
.status-pill.stale{color:var(--bad)}.status-pill.stale .beat{background:var(--bad)}
main{max-width:1440px;margin:0 auto;padding:30px 32px 24px;min-width:0}
[data-view-panel]{min-width:0}
footer{margin-top:32px;padding-top:16px;color:var(--dim);font-size:12px}

/* Identificacao e disponibilidade */
.device-header{display:flex;align-items:center;justify-content:space-between;gap:32px;padding:0 0 24px;border-bottom:1px solid var(--line)}
.device-copy{min-width:0;flex:1}
.device-name{margin:10px 0 8px;overflow-wrap:anywhere}
.device-meta{color:var(--muted);font-size:13px;line-height:1.7;overflow-wrap:anywhere}
.device-meta b{color:var(--text);font:500 12px/1.7 var(--mono)}
.badge{display:inline-flex;align-items:center;gap:7px;padding:3px 7px;border:1px solid var(--sbd,var(--line));border-radius:3px;background:var(--sbg,var(--surface-alt));color:var(--sc,var(--muted));font-size:12px;line-height:1.4}
.badge i{width:6px;height:6px;border-radius:50%;background:currentColor;flex:none}
.rate{width:220px;flex:none}
.rate-label{display:flex;align-items:baseline;justify-content:space-between;gap:16px;color:var(--muted);font-size:13px}
.rate-label b{font-size:24px;font-weight:600;color:var(--text);font-variant-numeric:tabular-nums}
.rate-track{height:4px;margin-top:8px;background:var(--line);overflow:hidden}
.rate-track::before{content:"";display:block;width:clamp(0%,calc(var(--p,0)*1%),100%);height:100%;background:var(--sc,var(--dim))}
.rate small{display:block;margin-top:7px;color:var(--dim);font-size:11px}

/* Sondas e ultimo registro */
.probe-panel{margin-top:24px}
.probe-table{width:100%;border-collapse:collapse;text-align:left;table-layout:fixed}
.probe-table caption{text-align:left;padding-bottom:12px;font-size:16px;font-weight:600}
.probe-table th,.probe-table td{padding:12px 16px;border-bottom:1px solid var(--line);vertical-align:top}
.probe-table thead th{background:var(--surface-alt);color:var(--muted);font-size:12px;font-weight:500}
.probe-table thead th:nth-child(1){width:18%}.probe-table thead th:nth-child(2){width:24%}
.probe-table tbody th{font-size:13px;font-weight:600}
.probe-table .card-v{margin:0;font-size:14px;font-weight:500}
.probe-table .card-m{margin:0;font-size:13px}
.pip{display:inline-block;width:6px;height:6px;border-radius:50%;background:var(--dim);flex:none}
.probe-table .pip{margin-left:8px;vertical-align:2px}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}
.pip.ok{background:var(--ok)}.pip.warn{background:var(--warn)}.pip.bad{background:var(--bad)}.pip.info{background:var(--info)}
.sec{margin-top:28px}
.sec-t{display:flex;align-items:baseline;justify-content:space-between;gap:16px;margin-bottom:12px}
.sec-t span{color:var(--dim);font-size:12px}
.log{margin:0;border-top:1px solid var(--line)}
.row{display:grid;grid-template-columns:190px minmax(0,1fr);gap:24px;padding:12px 16px;border-bottom:1px solid var(--line)}
.row:first-child{background:var(--surface-alt)}
.row dt{margin:0;color:var(--muted);font-size:13px}
.row dd{margin:0;overflow-wrap:anywhere;font-size:13px;line-height:1.6}
#l-url{font:12px/1.7 var(--mono)}
.tag{display:inline-block;padding:1px 5px;border-radius:3px;font-size:11px;font-weight:500}
.tag.ok{background:#24352c;color:var(--ok)}.tag.bad{background:#35282c;color:var(--bad)}
.diagnostic-output{position:relative;margin-top:20px}
.diagnostic-copy{position:absolute;top:12px;right:12px;z-index:1;min-width:144px}
#raw{margin:0;padding:16px;border:1px solid var(--line);border-radius:3px;background:var(--surface);white-space:pre-wrap;overflow-wrap:anywhere;color:var(--muted);font:12px/1.6 var(--mono)}

/* Recursos e historico */
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));margin-top:24px;border-top:1px solid var(--line);border-left:1px solid var(--line)}
.card{min-width:0;padding:18px 20px;border-right:1px solid var(--line);border-bottom:1px solid var(--line)}
.card-h{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:13px}
.card-h .pip{margin-left:auto}
.card-v{margin-top:8px;font-size:21px;font-weight:600;line-height:1.35;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.card-m{margin-top:6px;color:var(--muted);font-size:12px;line-height:1.65;overflow-wrap:anywhere}
.bars{display:inline-flex;align-items:flex-end;gap:3px;height:17px;margin-right:9px}
.bars i{width:4px;background:var(--line)}
.bars i:nth-child(1){height:5px}.bars i:nth-child(2){height:9px}.bars i:nth-child(3){height:13px}.bars i:nth-child(4){height:17px}
.bars i.on{background:currentColor}
.meter{height:3px;margin-top:14px;background:var(--line);overflow:hidden}
.meter i{display:block;width:0;height:100%;background:var(--ok)}
.meter i.warn{background:var(--warn)}.meter i.bad{background:var(--bad)}
.chart-card{margin-top:28px}
.chart-head{display:flex;flex-wrap:wrap;align-items:flex-start;justify-content:space-between;gap:16px}
.chart-head p{margin-top:4px;color:var(--dim);font-size:12px}
.legend{display:flex;flex-wrap:wrap;gap:18px;color:var(--muted);font-size:12px}
.legend span{display:flex;align-items:center;gap:7px}.legend b{color:var(--text);font-weight:500;font-variant-numeric:tabular-nums}
.legend i{width:12px;height:3px;background:var(--info)}.legend .wifi-dot{background:var(--warn)}
.chart{display:block;width:100%;height:210px;margin-top:16px}
.chart line{stroke:var(--line);stroke-width:1;vector-effect:non-scaling-stroke}
.chart polyline{fill:none;stroke:var(--info);stroke-width:2;stroke-linecap:round;stroke-linejoin:round;vector-effect:non-scaling-stroke}
.chart .wifi-line{stroke:var(--warn);stroke-dasharray:5 3}

/* Previa */
.viewer-h{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px 24px;padding-bottom:20px}
.viewer-copy{min-width:0;flex:1 1 320px}
.viewer-h p{margin-top:8px;color:var(--muted);font-size:13px;line-height:1.7}
.viewer-tools{display:flex;align-items:center;gap:10px}
.ptz-open-btn{gap:9px;white-space:nowrap}
.ptz-count{display:inline-grid;min-width:21px;height:20px;place-items:center;padding:0 5px;border-radius:3px;background:#ffffff26;font-size:11px;font-variant-numeric:tabular-nums}
.viewer-h .form-result{flex-basis:100%}
.viewer-stage{position:relative;display:grid;place-items:center;aspect-ratio:16/9;background:#171a1e;overflow:hidden}
.viewer-stage img{position:relative;z-index:1;width:100%;height:100%;object-fit:contain;opacity:0}
.viewer-stage img.ready{opacity:1;cursor:zoom-in}
.feed-state{position:absolute;z-index:3;inset:0;display:grid;place-items:center;text-align:center;padding:24px;background:#171a1edd;color:#d9dee4;font-size:14px}
.feed-state.hidden{opacity:0;pointer-events:none}
.feed-stamp{position:absolute;z-index:4;left:12px;bottom:12px;max-width:calc(100% - 24px);padding:6px 9px;background:#11161dde;color:#e0e5eb;font:11px/1.5 var(--mono)}
.feed-fs-btn{position:absolute;z-index:5;right:12px;top:12px;display:grid;place-items:center;width:40px;height:40px;border:1px solid #ffffff40;border-radius:3px;background:#11161dde;color:#fff;cursor:pointer}
.feed-fs-btn:hover{background:#384352}
.feed-fs-btn svg{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen,.viewer-stage.fs-fallback{aspect-ratio:auto;min-height:0;background:#000}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen{width:100%;height:100%}
.viewer-stage.fs-fallback{position:fixed;inset:0;z-index:999;width:100vw;height:100vh}
.viewer-stage:fullscreen img,.viewer-stage:-webkit-full-screen img,.viewer-stage.fs-fallback img{cursor:zoom-out}
body.fs-locked{overflow:hidden}

/* Configuracoes */
.settings-kpi{min-width:200px;padding-left:24px;border-left:1px solid var(--line)}
.settings-kpi span{display:block;color:var(--muted);font-size:12px}
.settings-kpi b{display:block;margin-top:4px;font-size:24px;font-weight:600;font-variant-numeric:tabular-nums}
#settingsForm{display:grid;grid-template-columns:180px minmax(0,1fr);gap:28px 36px;margin-top:28px;align-items:start}
.settings-tabbar{position:sticky;top:24px}
.settings-tablist{display:flex;flex-direction:column;gap:2px}
.settings-tab{min-height:40px;padding:10px 12px;border:0;border-left:2px solid transparent;background:transparent;color:var(--muted);font-size:13px;text-align:left;cursor:pointer}
.settings-tab:hover{color:var(--text);background:var(--surface-alt)}
.settings-tab[aria-selected="true"]{color:var(--accent);border-left-color:var(--accent);background:var(--surface-alt);font-weight:600}
.settings-panels,.settings-card{min-width:0}
.settings-card-h{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding-bottom:20px}
.settings-card-h p{margin-top:5px;color:var(--muted);font-size:13px}
.form-grid{display:grid;grid-template-columns:minmax(0,1fr);gap:24px}
.field{min-width:0}.field.wide,.estimate-box{grid-column:1/-1}
.field label{display:flex;align-items:baseline;justify-content:space-between;flex-wrap:wrap;gap:4px 8px;margin-bottom:7px;font-size:13px;font-weight:500}
.field label span{color:var(--dim);font-size:11px;font-weight:400;white-space:nowrap}
.field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select{width:100%;height:38px;padding:0 10px;border:1px solid var(--line-strong);border-radius:3px;background:var(--surface);color:var(--text);font:14px/1.3 var(--sans)}
.field input:focus,.field select:focus{border-color:var(--accent)}
.field input:disabled,.field select:disabled{opacity:.55;cursor:not-allowed}
.field small{display:block;margin-top:6px;color:var(--muted);font-size:12px;line-height:1.6}
.checkline{display:flex;align-items:flex-start;gap:10px}
.checkline input{width:17px;height:17px;flex:none;margin:2px 0 0;accent-color:var(--accent);cursor:pointer}
.checkline label{display:block;margin:0;cursor:pointer}
.checkline label b{display:block;font-size:13px;font-weight:500}
.checkline label span{display:block;margin-top:5px;color:var(--muted);font-size:12px;line-height:1.6;white-space:normal}
.estimate-box{padding:14px 16px;border-left:2px solid var(--accent);background:var(--surface-alt);color:var(--muted);font-size:13px;line-height:1.7}
.estimate-box b{color:var(--text);font-weight:600;font-variant-numeric:tabular-nums}
.settings-savebar{grid-column:2;display:flex;flex-direction:column;gap:12px;padding-top:20px;border-top:1px solid var(--line)}
.settings-savebar .button-row{justify-content:flex-end}.settings-savebar .form-result{text-align:right}
.button-row{display:flex;flex-wrap:wrap;align-items:center;gap:10px}
.btn{display:inline-flex;align-items:center;justify-content:center;min-height:36px;padding:8px 14px;border:1px solid var(--line-strong);border-radius:3px;background:var(--surface);color:var(--text);font:500 13px/1.4 var(--sans);text-decoration:none;cursor:pointer;transition:background-color .12s,border-color .12s}
.btn:hover{background:var(--surface-hover);border-color:var(--dim)}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn.primary{background:var(--button-primary);border-color:#6f98c7;color:var(--on-primary)}.btn.primary:hover{background:var(--button-primary-hover);border-color:var(--accent)}
.btn.warn{border-color:#806b49;color:var(--warn)}.btn.warn:hover{background:#393126;border-color:var(--warn)}
.btn.bad{border-color:#85555b;color:var(--bad)}.btn.bad:hover{background:#3c2b30;border-color:var(--bad)}
.form-result{min-height:18px;color:var(--dim);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.form-result.ok{color:var(--ok)}.form-result.warn{color:var(--warn)}.form-result.bad{color:var(--bad)}
.actions-card .form-result{margin-top:22px}
.action-grid{border-top:1px solid var(--line)}
.action-item{display:grid;grid-template-columns:180px minmax(0,1fr) 164px;align-items:center;gap:28px;padding:20px 0;border-bottom:1px solid var(--line)}
.action-item b{font-size:14px;font-weight:600}.action-item span{color:var(--muted);font-size:13px;line-height:1.65}
.action-item .btn{width:100%}

/* Logs */
.logs-hero-tools{display:flex;flex-wrap:wrap;gap:8px}
.logs-card{margin-top:24px;border:1px solid var(--line);border-radius:3px;overflow:hidden}
.logs-toolbar{display:grid;grid-template-columns:minmax(220px,2fr) repeat(3,minmax(0,1fr));gap:16px;padding:16px;border-bottom:1px solid var(--line)}
.logs-table-wrap{overflow:auto;max-height:620px;overscroll-behavior:contain}
.logs-table{width:100%;border-collapse:collapse;min-width:1050px;font-size:13px}
.logs-table th{position:sticky;top:0;z-index:2;padding:12px;background:var(--surface-alt);color:var(--muted);text-align:left;font-size:12px;font-weight:500;border-bottom:1px solid var(--line)}
.logs-table td{padding:12px;border-top:1px solid var(--line);vertical-align:top}
.logs-table tbody tr:first-child td{border-top:0}.logs-table tbody tr:hover td{background:var(--surface-hover)}
.logs-table .mono{font-size:12px;white-space:nowrap}
.logs-level{display:inline-block;padding:2px 6px;border-radius:3px;font-size:11px;font-weight:500}
.logs-level.info{color:var(--info);background:#26323e}.logs-level.warning{color:var(--warn);background:#393126}.logs-level.error{color:var(--bad);background:#35282c}
.logs-event{color:var(--text);font-size:13px;white-space:nowrap}
.logs-detail{max-width:340px;color:var(--muted);overflow-wrap:anywhere;line-height:1.6}
.logs-flags{display:block;margin-top:4px;color:var(--dim);font-size:11px;line-height:1.6}
.logs-empty{padding:36px 16px;text-align:center;color:var(--dim);font-size:13px}
.logs-foot{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;padding:14px 16px;border-top:1px solid var(--line)}
.logs-foot .form-result{flex:1 1 260px}

/* Dialogos */
.modal-overlay{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#08090bcc}
.modal-box{width:100%;max-width:460px;max-height:calc(100dvh - 40px);overflow-y:auto;padding:24px;background:var(--surface);border:1px solid var(--line-strong);border-radius:5px;box-shadow:0 12px 40px #0006}
.modal-box h3{font-size:18px}
.modal-desc{margin:10px 0 20px;color:var(--muted);font-size:13px;line-height:1.7}
.modal-actions{justify-content:flex-end;margin-top:22px}
.ptz-modal-box{max-width:600px;display:flex;flex-direction:column;overflow:hidden}
.ptz-modal-head{display:flex;align-items:flex-start;justify-content:space-between;gap:18px;flex:none}
.ptz-filter{flex:none;margin-bottom:16px}
.ptz-list{min-height:80px;max-height:min(50vh,480px);overflow-y:auto;overscroll-behavior:contain;border:1px solid var(--line);border-radius:3px}
.ptz-list-item{display:flex;align-items:center;gap:14px;padding:12px 14px;border-top:1px solid var(--line)}
.ptz-list-item:first-child{border:0}.ptz-list-item:hover{background:var(--surface-alt)}
.ptz-list-copy{min-width:0;flex:1}.ptz-list-copy b{display:block;font-size:14px;font-weight:600;overflow-wrap:anywhere}
.ptz-list-copy span{display:block;margin-top:4px;color:var(--dim);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.ptz-list-item .btn{min-width:60px;flex:none}
.ptz-empty{padding:28px 16px;text-align:center;color:var(--dim);font-size:13px;line-height:1.7}
.ptz-modal-foot{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;margin-top:16px;color:var(--dim);font-size:12px;flex:none}

@media(max-width:1000px){
  .navigation-inner{flex-wrap:wrap;gap:0;padding:0 24px}
  .tabs{width:100%;gap:24px}.connection{width:100%;justify-content:flex-end;padding:8px 0;border-top:1px solid var(--line);border-left:0}
  main{padding:24px}
  .grid{grid-template-columns:repeat(2,minmax(0,1fr))}
  #settingsForm{grid-template-columns:160px minmax(0,1fr);gap:24px}
  .logs-toolbar{grid-template-columns:repeat(3,minmax(0,1fr))}.logs-toolbar .field:first-child{grid-column:1/-1}
  .device-header{flex-wrap:wrap;gap:24px}
  .action-item{grid-template-columns:minmax(0,1fr) 164px;gap:6px 24px}.action-item span{grid-column:1}.action-item .btn{grid-column:2;grid-row:1/3}
}
@media(max-width:680px){
  .navigation-inner{padding:0 max(16px,env(safe-area-inset-left)) 0 max(16px,env(safe-area-inset-right))}
  .tabs{gap:22px}.tab{min-height:48px;font-size:13px}
  .tab-admin-start{position:relative;margin-left:0;padding-left:22px}
  .tab-admin-start::before{content:"";position:absolute;left:0;top:50%;height:20px;border-left:1px solid var(--line);transform:translateY(-50%)}
  .connection,.status-pill{font-size:11px}.connection{justify-content:flex-start}
  main{padding:24px max(16px,env(safe-area-inset-right)) 20px max(16px,env(safe-area-inset-left))}
  h1{font-size:22px}
  .device-header{display:block;padding-bottom:20px}
  .rate{width:100%;margin-top:20px}.rate-label b{font-size:22px}
  .probe-table,.probe-table tbody{display:block}
  .probe-table caption{display:block}
  .probe-table thead{display:none}
  .probe-table tr{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.25fr);gap:6px 16px;padding:14px 0;border-bottom:1px solid var(--line)}
  .probe-table tbody th,.probe-table td{display:block;width:auto;padding:0;border:0}
  .probe-table .card-v{text-align:right}.probe-table .card-m{grid-column:1/-1;font-size:13px}
  .sec-t{flex-wrap:wrap;gap:4px}
  .row{grid-template-columns:1fr;gap:6px;padding:12px 0}
  .row:first-child{padding:12px}
  .grid{grid-template-columns:1fr}.card{padding:16px}
  .chart{height:180px}
  .viewer-copy{flex-basis:100%}.viewer-tools,.ptz-open-btn{width:100%}
  .feed-state{padding:16px;font-size:13px}.feed-stamp{font-size:10px}
  .settings-kpi{margin-top:16px;padding:16px 0 0;border-left:0;border-top:1px solid var(--line)}
  #settingsForm{grid-template-columns:1fr;gap:20px}
  .settings-tabbar{position:static;min-width:0}
  .settings-tablist{flex-direction:row;overflow-x:auto;border-bottom:1px solid var(--line)}
  .settings-tab{min-height:44px;flex:none;padding:10px 12px;border-left:0;border-bottom:2px solid transparent;white-space:nowrap}
  .settings-tab[aria-selected="true"]{border-left:0;border-bottom-color:var(--accent)}
  .settings-savebar{grid-column:1}.settings-savebar .form-result{text-align:left}
  .settings-savebar .button-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  .btn{min-height:44px;padding-inline:12px}
  .field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select{height:44px;font-size:16px}
  .action-item{grid-template-columns:1fr;gap:8px;padding:18px 0}.action-item .btn{grid-column:1;grid-row:auto;justify-self:start;width:auto;margin-top:4px}
  .logs-hero-tools{margin-top:20px}.logs-hero-tools .btn{flex:1}
  .logs-toolbar{grid-template-columns:1fr 1fr;padding:14px;gap:12px}
  .logs-toolbar .field:last-child{grid-column:1/-1}.logs-table-wrap{max-height:65vh}
  .modal-overlay{padding:12px}.modal-box{max-height:calc(100dvh - 24px);padding:20px}
  .ptz-list-item{padding:12px}
  footer{font-size:11px}
}
@media(prefers-reduced-motion:reduce){
  *,*::before,*::after{animation:none!important;transition:none!important;scroll-behavior:auto!important}
}
</style>
</head>
<body>
<button class="skip-link" id="skipToContent" type="button">Pular para o conteúdo</button>
<div class="navigation">
  <div class="navigation-inner">
    <nav class="tabs" role="tablist" aria-label="Seções do painel">
      <button class="tab" id="cameraTab" type="button" role="tab" data-view="camera" aria-controls="cameraView" aria-selected="true">Visão geral</button>
      <button class="tab" id="feedTab" type="button" role="tab" data-view="feed" aria-controls="feedView" aria-selected="false" tabindex="-1">Prévia</button>
      <button class="tab" id="diagnosticsTab" type="button" role="tab" data-view="diagnostics" aria-controls="diagnosticsView" aria-selected="false" tabindex="-1">Diagnóstico</button>
      <button class="tab" id="logsTab" type="button" role="tab" data-view="logs" aria-controls="logsView" aria-selected="false" tabindex="-1">Logs</button>
      <button class="tab" id="resourcesTab" type="button" role="tab" data-view="resources" aria-controls="resourcesView" aria-selected="false" tabindex="-1">Recursos</button>
      <button class="tab tab-admin-start" id="actionsTab" type="button" role="tab" data-view="actions" aria-controls="actionsView" aria-selected="false" tabindex="-1">Ações</button>
      <button class="tab" id="settingsTab" type="button" role="tab" data-view="settings" aria-controls="settingsView" aria-selected="false" tabindex="-1">Configurações</button>
    </nav>
    <div class="connection"><span>ESP32</span><div class="status-pill" id="deviceStatus"><span class="beat"></span><span id="deviceStatusText">conectando…</span></div></div>
  </div>
</div>
<main id="mainContent" tabindex="-1">

<div id="cameraView" role="tabpanel" tabindex="0" aria-labelledby="cameraTab" data-view-panel="camera">

<section class="device-header" id="hero">
  <div class="device-copy">
    <span class="badge" id="badge"><i></i><span id="stateText">carregando</span></span>
    <h1 class="device-name" id="camName">—</h1>
    <div class="device-meta" id="camSub">aguardando o primeiro ciclo de verificação…</div>
  </div>
  <div class="rate">
    <div class="rate-label"><span>Disponibilidade</span><b id="availValue">—</b></div>
    <div class="rate-track" id="arc" aria-hidden="true"></div>
    <small>Sucesso nas verificações conclusivas</small>
  </div>
</section>

<section class="probe-panel" aria-labelledby="probesTitle">
  <table class="probe-table">
    <caption id="probesTitle">Saúde dos serviços</caption>
    <thead><tr><th scope="col">Serviço</th><th scope="col">Resultado</th><th scope="col">Detalhes</th></tr></thead>
    <tbody>
      <tr><th scope="row">ONVIF<i class="pip" id="p-onvif"></i></th><td class="card-v" id="v-onvif">—</td><td class="card-m" id="m-onvif">—</td></tr>
      <tr><th scope="row">RTSP<i class="pip" id="p-rtsp"></i></th><td class="card-v" id="v-rtsp">—</td><td class="card-m" id="m-rtsp">—</td></tr>
      <tr><th scope="row">Imagem JPEG<i class="pip" id="p-image"></i></th><td class="card-v" id="v-image">—</td><td class="card-m" id="m-image">—</td></tr>
      <tr><th scope="row">Verificações</th><td class="card-v" id="v-checks">—</td><td class="card-m" id="m-checks">—</td></tr>
    </tbody>
  </table>
</section>

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

</div>

<div id="feedView" role="tabpanel" tabindex="0" aria-labelledby="feedTab" data-view-panel="feed" hidden>
  <section class="viewer" aria-labelledby="viewerTitle">
    <div class="viewer-h">
      <div class="viewer-copy">
        <h1 id="viewerTitle">Prévia de diagnóstico</h1>
        <p id="feedMeta">Quadros JPEG protegidos pelo ESP32 · máximo de 1 FPS · sem expor credenciais</p>
      </div>
      <div class="viewer-tools">
        <button class="btn primary ptz-open-btn" type="button" id="ptzMenuButton" aria-haspopup="dialog" aria-controls="ptzPresetModal" disabled>Posições PTZ <span class="ptz-count" id="ptzPresetCount">—</span></button>
      </div>
      <div class="form-result" id="ptzActionStatus" hidden></div>
    </div>
    <div class="viewer-stage">
      <img id="cameraFeed" alt="Prévia JPEG da câmera monitorada" decoding="async">
      <div class="feed-state" id="feedState">Carregando o primeiro quadro da câmera…</div>
      <div class="feed-stamp" id="feedStamp">aguardando imagem</div>
      <button type="button" class="feed-fs-btn" id="feedFsBtn" aria-label="Ver em tela cheia" aria-pressed="false" title="Tela cheia">
        <svg class="ic-expand" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M9 3H5a2 2 0 0 0-2 2v4M15 3h4a2 2 0 0 1 2 2v4M9 21H5a2 2 0 0 1-2-2v-4M15 21h4a2 2 0 0 0 2-2v-4"/></svg>
        <svg class="ic-collapse" viewBox="0 0 24 24" aria-hidden="true" focusable="false" hidden><path d="M4 9V5a2 2 0 0 1 2-2h4M20 9V5a2 2 0 0 0-2-2h-4M4 15v4a2 2 0 0 0 2 2h4M20 15v4a2 2 0 0 1-2 2h-4"/></svg>
      </button>
    </div>
  </section>
</div>

<div id="diagnosticsView" role="tabpanel" tabindex="0" aria-labelledby="diagnosticsTab" data-view-panel="diagnostics" hidden>
  <h1>Diagnóstico completo (JSON)</h1>
  <div class="diagnostic-output">
    <button class="btn diagnostic-copy" id="copyDiagnostics" type="button" aria-label="Copiar JSON do diagnóstico" disabled><span id="diagnosticCopyStatus" role="status" aria-live="polite">Copiar JSON</span></button>
    <pre id="raw">Carregando…</pre>
  </div>
</div>

<div id="logsView" role="tabpanel" tabindex="0" aria-labelledby="logsTab" data-view-panel="logs" hidden>
  <section class="device-header" style="--sc:var(--info);--sbg:rgba(14,165,233,.14);--sbd:rgba(14,165,233,.32)">
    <div class="device-copy">
      <span class="badge"><i></i><span id="logsState">carregando histórico</span></span>
      <h1 class="device-name">Histórico persistente</h1>
      <div class="device-meta">Conexões com a câmera e operações importantes, preservadas entre reinicializações.</div>
    </div>
    <div class="logs-hero-tools">
      <button class="btn" id="logsRefresh" type="button">Atualizar</button>
      <a class="btn primary" id="logsExport" href="/api/logs/export" download="camera-watchdog-logs.csv">Exportar CSV completo</a>
    </div>
  </section>

  <div class="grid">
    <article class="card">
      <div class="card-h">Registros armazenados</div>
      <div class="card-v info" id="logsCount">—</div>
      <div class="card-m" id="logsCountMeta">limite circular de 10.000 registros</div>
      <div class="meter"><i id="logsCountMeter"></i></div>
    </article>
    <article class="card">
      <div class="card-h">Partição de logs</div>
      <div class="card-v info" id="logsStorage">—</div>
      <div class="card-m" id="logsStorageMeta">aguardando dados do LittleFS</div>
      <div class="meter"><i id="logsStorageMeter"></i></div>
    </article>
    <article class="card">
      <div class="card-h">Integridade</div>
      <div class="card-v" id="logsIntegrity">—</div>
      <div class="card-m" id="logsIntegrityMeta">registros descartados e erros de escrita</div>
    </article>
  </div>

  <section class="logs-card" aria-labelledby="logsTableTitle">
    <div class="logs-toolbar">
      <div class="field">
        <label for="logsSearch">Filtrar registros <span>nos itens carregados</span></label>
        <input id="logsSearch" type="text" maxlength="80" placeholder="Evento, destino, detalhe ou status">
      </div>
      <div class="field">
        <label for="logsLevel">Nível</label>
        <select id="logsLevel">
          <option value="">Todos</option>
          <option value="info">Informação</option>
          <option value="warning">Aviso</option>
          <option value="error">Erro</option>
        </select>
      </div>
      <div class="field">
        <label for="logsCategory">Categoria</label>
        <select id="logsCategory">
          <option value="">Todas</option>
          <option value="system">Sistema</option>
          <option value="wifi">Wi-Fi</option>
          <option value="camera">Câmera</option>
          <option value="discovery">Descoberta</option>
          <option value="onvif">ONVIF</option>
          <option value="snapshot">Snapshot</option>
          <option value="rtsp">RTSP</option>
          <option value="health">Health-check</option>
          <option value="action">Ações</option>
          <option value="notification">Notificações</option>
          <option value="settings">Configurações</option>
        </select>
      </div>
      <div class="field">
        <label for="logsScope">Visualização</label>
        <select id="logsScope">
          <option value="diagnostic">Diagnóstico</option>
          <option value="all">Todas as tentativas</option>
        </select>
      </div>
    </div>
    <div class="logs-table-wrap">
      <table class="logs-table">
        <thead>
          <tr><th id="logsTableTitle">Data e hora</th><th>Nível</th><th>Evento</th><th>Destino</th><th>Resultado</th><th>Duração</th><th>Detalhes</th></tr>
        </thead>
        <tbody id="logsTableBody"></tbody>
      </table>
      <div class="logs-empty" id="logsEmpty">Nenhum registro carregado.</div>
    </div>
    <div class="logs-foot">
      <div class="form-result" id="logsStatus">Abra esta aba para consultar o histórico persistente.</div>
      <button class="btn" id="logsLoadMore" type="button" hidden>Carregar mais 100</button>
    </div>
  </section>
</div>

<div id="resourcesView" role="tabpanel" tabindex="0" aria-labelledby="resourcesTab" data-view-panel="resources" hidden>
  <section class="device-header" id="resourcesHero">
    <div class="device-copy">
      <span class="badge" id="resourcesBadge"><i></i><span id="resourcesState">carregando</span></span>
      <h1 class="device-name" id="resourcesChip">ESP32</h1>
      <div class="device-meta" id="resourcesMeta">aguardando dados do dispositivo…</div>
    </div>
    <div class="rate">
      <div class="rate-label"><span>Heap livre</span><b id="resourcesHeapPercent">—</b></div>
      <div class="rate-track" id="resourcesArc" aria-hidden="true"></div>
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
    <article class="card">
      <div class="card-h">Catálogo ONVIF<i class="pip" id="p-ronvif"></i></div>
      <div class="card-v info" id="r-onvif">—</div>
      <div class="card-m" id="r-onvif-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Relógio da câmera<i class="pip" id="p-rclock"></i></div>
      <div class="card-v info" id="r-clock">—</div>
      <div class="card-m" id="r-clock-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Eventos anunciados<i class="pip" id="p-revents"></i></div>
      <div class="card-v info" id="r-events">—</div>
      <div class="card-m" id="r-events-meta">—</div>
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

<div id="actionsView" role="tabpanel" tabindex="0" aria-labelledby="actionsTab" data-view-panel="actions" hidden>
  <section class="settings-card actions-card">
    <div class="settings-card-h"><div><h1>Controle manual</h1><p>Ações sensíveis exigem confirmação e respeitam os limites de segurança do firmware.</p></div></div>
    <div class="action-grid">
      <div class="action-item"><b>Verificar agora</b><span>Executa ONVIF, RTSP quando necessário e validação JPEG.</span><button class="btn" type="button" data-admin-action="health-check" disabled>Executar</button></div>
      <div class="action-item"><b>Redescobrir câmera</b><span>Repete WS-Discovery e confirma o endpoint.</span><button class="btn" type="button" data-admin-action="rediscover" disabled>Redescobrir</button></div>
      <div class="action-item"><b>Reiniciar câmera</b><span>Envia ONVIF SystemReboot respeitando cooldown e limite da janela.</span><button class="btn warn" type="button" data-admin-action="reboot-camera" disabled>Reiniciar câmera</button></div>
      <div class="action-item"><b>Limpar limitador</b><span>Zera cooldown e tentativas registradas na janela atual.</span><button class="btn warn" type="button" data-admin-action="reset-reboot-limiter" disabled>Limpar limite</button></div>
      <div class="action-item"><b>Zerar logs</b><span>Remove o histórico anterior e mantém apenas o marcador da limpeza.</span><button class="btn bad" type="button" data-admin-action="clear-logs" disabled>Zerar logs</button></div>
      <div class="action-item"><b>Reiniciar ESP32</b><span>Reinicia somente o watchdog, sem interromper a alimentação da câmera.</span><button class="btn bad" type="button" data-admin-action="reboot-esp32" disabled>Reiniciar ESP32</button></div>
    </div>
    <div class="form-result" id="adminActionStatus">Nenhuma ação solicitada nesta sessão.</div>
  </section>
</div>

<div id="settingsView" role="tabpanel" tabindex="0" aria-labelledby="settingsTab" data-view-panel="settings" hidden>
  <section class="device-header" id="settingsHero" style="--sc:var(--info);--sbg:rgba(14,165,233,.14);--sbd:rgba(14,165,233,.32)">
    <div class="device-copy">
      <span class="badge"><i></i><span id="settingsSource">carregando configurações</span></span>
      <h1 class="device-name">Configurações do watchdog</h1>
      <div class="device-meta" id="settingsMeta">Valores persistentes na NVS, validados também pelo firmware.</div>
    </div>
    <div class="settings-kpi">
      <span>alerta de imagem estimado</span>
      <b id="settingsAlertEstimate">—</b>
    </div>
  </section>

  <form id="settingsForm" autocomplete="off" novalidate>
    <div class="settings-tabbar">
      <nav class="settings-tablist" role="tablist" aria-label="Categorias das configurações">
        <button class="settings-tab" id="settingsIdentificationTab" type="button" role="tab" data-settings-section="identification" aria-controls="settingsIdentificationPanel" aria-selected="true" tabindex="0">Identificação</button>
        <button class="settings-tab" id="settingsAuthenticationTab" type="button" role="tab" data-settings-section="authentication" aria-controls="settingsAuthenticationPanel" aria-selected="false" tabindex="-1">Autenticação da câmera</button>
        <button class="settings-tab" id="settingsMonitoringTab" type="button" role="tab" data-settings-section="monitoring" aria-controls="settingsMonitoringPanel" aria-selected="false" tabindex="-1">Monitoramento</button>
        <button class="settings-tab" id="settingsRecoveryTab" type="button" role="tab" data-settings-section="recovery" aria-controls="settingsRecoveryPanel" aria-selected="false" tabindex="-1">Recuperação</button>
        <button class="settings-tab" id="settingsNetworkTab" type="button" role="tab" data-settings-section="network" aria-controls="settingsNetworkPanel" aria-selected="false" tabindex="-1">Rede e notificações</button>
        <button class="settings-tab" id="settingsCommunicationTab" type="button" role="tab" data-settings-section="communication" aria-controls="settingsCommunicationPanel" aria-selected="false" tabindex="-1">Comunicação</button>
        <button class="settings-tab" id="settingsHardwareTab" type="button" role="tab" data-settings-section="hardware" aria-controls="settingsHardwarePanel" aria-selected="false" tabindex="-1">Alerta local</button>
      </nav>
    </div>

    <div class="settings-panels">
      <section class="settings-card settings-panel" id="settingsIdentificationPanel" role="tabpanel" aria-labelledby="settingsIdentificationTab" data-settings-section-panel="identification">
        <div class="settings-card-h"><div><h2>Identificação e interface</h2><p>Nome, endereço local, perfil JPEG e ritmo do painel.</p></div></div>
        <div class="form-grid">
          <div class="field"><label for="cfgCameraName">Nome da câmera <span>1–48 caracteres</span></label><input id="cfgCameraName" type="text" minlength="1" maxlength="48" required><small>Exibido no painel e nos assuntos dos e-mails.</small></div>
          <div class="field"><label for="cfgLocalHostname">Endereço local <span>1–31 caracteres</span></label><input id="cfgLocalHostname" type="text" minlength="1" maxlength="31" pattern="[A-Za-z0-9](?:[A-Za-z0-9\-]{0,29}[A-Za-z0-9])?" placeholder="camera-watchdog" required><small>Use letras, números e hífens. O acesso será feito por http://nome-escolhido.local/.</small></div>
          <div class="field"><label for="cfgSnapshotProfileMatch">Perfil ONVIF preferido <span>até 32 caracteres</span></label><input id="cfgSnapshotProfileMatch" type="text" maxlength="32" placeholder="snap"><small>O firmware prioriza um perfil JPEG compatível; padrão: “snap”.</small></div>
          <div class="field"><label for="cfgCameraRtspPort">Porta RTSP <span>1–65535</span></label><input id="cfgCameraRtspPort" type="number" min="1" max="65535" step="1" required><small>Padrão: 554.</small></div>
          <div class="field"><label for="cfgFeedMinimum">Intervalo mínimo da prévia <span>segundos</span></label><input id="cfgFeedMinimum" type="number" min="0.25" max="10" step="0.05" required><small>Mínimo permitido: 0,25 s; use 0,25 s com ciclo de 0,5 s para até 2 FPS.</small></div>
          <div class="field"><label for="cfgFeedTarget">Ciclo-alvo da prévia <span>segundos</span></label><input id="cfgFeedTarget" type="number" min="0.5" max="30" step="0.1" required><small>Padrão estável: 5 s; mínimo permitido: 0,5 s, limitando a prévia a 2 FPS.</small></div>
          <div class="field"><label for="cfgStatusRefresh">Atualização do painel <span>segundos</span></label><input id="cfgStatusRefresh" type="number" min="1" max="30" step="0.5" required><small>Padrão: 3 s.</small></div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsAuthenticationPanel" role="tabpanel" aria-labelledby="settingsAuthenticationTab" data-settings-section-panel="authentication" hidden>
        <div class="settings-card-h"><div><h2>Autenticação da câmera</h2><p>Credenciais usadas pelo watchdog para acessar a imagem e os serviços da câmera. Não altera a conta na câmera.</p></div></div>
        <div class="form-grid">
          <div class="field"><label for="cfgCameraUsername">Usuário da câmera</label><input id="cfgCameraUsername" type="text" maxlength="64" autocomplete="off" autocapitalize="none" spellcheck="false"><small>Deixe vazio quando a câmera permitir acesso sem autenticação.</small></div>
          <div class="field"><label for="cfgCameraPasswordAction">Senha da câmera</label><select id="cfgCameraPasswordAction"><option value="keep">Manter a senha atual</option><option value="replace">Definir ou substituir a senha</option><option value="clear">Remover usuário e senha</option></select><small id="cfgCameraPasswordStatus" aria-live="polite">Aguardando consulta.</small></div>
          <div class="field"><label for="cfgCameraPassword">Nova senha</label><input id="cfgCameraPassword" type="password" maxlength="128" autocomplete="new-password" disabled><small>Ao substituir, pode deixar vazio se a câmera usar usuário sem senha. Espaços fazem parte da senha.</small></div>
          <div class="estimate-box">A senha salva não é exibida nem devolvida ao navegador. Ao salvar alterações nas credenciais, a conexão é renovada e uma nova verificação é iniciada. Restaurar padrões mantém estas credenciais.</div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsMonitoringPanel" role="tabpanel" aria-labelledby="settingsMonitoringTab" data-settings-section-panel="monitoring" hidden>
        <div class="settings-card-h"><div><h2>Monitoramento</h2><p>Confirmação de falhas e recuperações.</p></div></div>
        <div class="form-grid">
          <div class="field"><label for="cfgHealthCheckInterval">Intervalo entre verificações <span>10–3600 s</span></label><input id="cfgHealthCheckInterval" type="number" min="10" max="3600" step="1" required><small>Padrão: 15 s.</small></div>
          <div class="field"><label for="cfgStartupGrace">Tolerância após ligar <span>0–600 s</span></label><input id="cfgStartupGrace" type="number" min="0" max="600" step="1" required><small>Padrão: 90 s; também protege contra reboot durante a partida.</small></div>
          <div class="field"><label for="cfgFailuresBeforeRediscovery">Falhas para redescobrir <span>1–20</span></label><input id="cfgFailuresBeforeRediscovery" type="number" min="1" max="20" step="1" required><small>Padrão: 2.</small></div>
          <div class="field"><label for="cfgFailuresBeforeOffline">Falhas para declarar offline <span>1–20</span></label><input id="cfgFailuresBeforeOffline" type="number" min="1" max="20" step="1" required><small>Padrão: 3; deve ser igual ou maior que a redescoberta.</small></div>
          <div class="field"><label for="cfgSuccessesBeforeRecovery">Sucessos para confirmar recuperação <span>1–10</span></label><input id="cfgSuccessesBeforeRecovery" type="number" min="1" max="10" step="1" required><small>Padrão: 2.</small></div>
          <div class="field"><label for="cfgVideoFailuresBeforeAlert">Falhas JPEG para alerta <span>1–20</span></label><input id="cfgVideoFailuresBeforeAlert" type="number" min="1" max="20" step="1" required><small>Padrão: 2.</small></div>
          <div class="field"><label for="cfgVideoFailuresBeforeReboot">Falhas JPEG para reboot <span>1–20</span></label><input id="cfgVideoFailuresBeforeReboot" type="number" min="1" max="20" step="1" required><small>Padrão: 3; não pode anteceder o alerta.</small></div>
          <div class="estimate-box">Com estes valores: alerta de imagem em aproximadamente <b id="estimateAlert">—</b>, reboot em <b id="estimateReboot">—</b> e offline em <b id="estimateOffline">—</b>.</div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsRecoveryPanel" role="tabpanel" aria-labelledby="settingsRecoveryTab" data-settings-section-panel="recovery" hidden>
        <div class="settings-card-h"><div><h2>Recuperação automática</h2><p>Limites contra ciclos de reinicialização.</p></div></div>
        <div class="form-grid">
          <div class="field"><div class="checkline"><input id="cfgAutoReboot" type="checkbox"><label for="cfgAutoReboot"><b>Permitir ONVIF SystemReboot automático</b><span>O reboot manual continua respeitando cooldown e limite da janela.</span></label></div></div>
          <div class="field"><label for="cfgRebootGrace">Tolerância após reboot <span>30–600 s</span></label><input id="cfgRebootGrace" type="number" min="30" max="600" step="1" required><small>Padrão: 60 s sem sondas, protegendo a partida da câmera.</small></div>
          <div class="field"><label for="cfgRebootCooldown">Cooldown entre reboots <span>2–1440 min</span></label><input id="cfgRebootCooldown" type="number" min="2" max="1440" step="1" required><small>Padrão: 5 min.</small></div>
          <div class="field"><label for="cfgRebootWindow">Janela de contabilização <span>1–168 h</span></label><input id="cfgRebootWindow" type="number" min="1" max="168" step="1" required><small>Padrão: 1 h; somente comandos ONVIF aceitos consomem a cota.</small></div>
          <div class="field"><label for="cfgMaxReboots">Máximo de reboots na janela <span>1–20</span></label><input id="cfgMaxReboots" type="number" min="1" max="20" step="1" required><small>Padrão: 3 comandos aceitos por hora; no máximo um a cada 5 min.</small></div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsNetworkPanel" role="tabpanel" aria-labelledby="settingsNetworkTab" data-settings-section-panel="network" hidden>
        <div class="settings-card-h"><div><h2>Rede e notificações</h2><p>Descoberta da câmera e repetição dos alertas.</p></div></div>
        <div class="form-grid">
          <div class="field"><div class="checkline"><input id="cfgAutoScan" type="checkbox"><label for="cfgAutoScan"><b>Permitir varredura automática da sub-rede</b><span>Usada apenas após falhas confirmadas e limitada a 254 hosts.</span></label></div></div>
          <div class="field"><div class="checkline"><input id="cfgEmailAlerts" type="checkbox"><label for="cfgEmailAlerts"><b>Ativar alertas por e-mail</b><span>Desativar cancela notificações pendentes, mas não apaga as credenciais do Resend.</span></label></div></div>
          <div class="field"><div class="checkline"><input id="cfgEmailIpChange" type="checkbox"><label for="cfgEmailIpChange"><b>Avisar quando a câmera mudar de IP</b><span>Aplicado somente quando os alertas por e-mail estiverem ativos.</span></label></div></div>
          <div class="field"><label for="cfgWifiTimeout">Timeout inicial do Wi-Fi <span>5–120 s</span></label><input id="cfgWifiTimeout" type="number" min="5" max="120" step="1" required><small>Usado no próximo boot.</small></div>
          <div class="field"><label for="cfgDiscoveryTimeout">Tempo do WS-Discovery <span>1–15 s</span></label><input id="cfgDiscoveryTimeout" type="number" min="1" max="15" step="0.1" required><small>Padrão: 3,5 s.</small></div>
          <div class="field"><label for="cfgRediscoveryInterval">Intervalo entre redescobertas <span>30–3600 s</span></label><input id="cfgRediscoveryInterval" type="number" min="30" max="3600" step="1" required><small>Padrão: 30 s.</small></div>
          <div class="field"><label for="cfgSubnetScanInterval">Intervalo entre varreduras <span>1–1440 min</span></label><input id="cfgSubnetScanInterval" type="number" min="1" max="1440" step="1" required><small>Padrão: 5 min.</small></div>
          <div class="field"><label for="cfgEmailRetry">Repetição de tentativa de e-mail <span>30–3600 s</span></label><input id="cfgEmailRetry" type="number" min="30" max="3600" step="1" required><small>Padrão: 60 s.</small></div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsCommunicationPanel" role="tabpanel" aria-labelledby="settingsCommunicationTab" data-settings-section-panel="communication" hidden>
        <div class="settings-card-h"><div><h2>Comunicação</h2><p>Timeouts e nova resolução do snapshot.</p></div></div>
        <div class="form-grid">
          <div class="field"><label for="cfgTcpTimeout">Conexão TCP <span>200–5000 ms</span></label><input id="cfgTcpTimeout" type="number" min="200" max="5000" step="50" required><small>Padrão: 900 ms.</small></div>
          <div class="field"><label for="cfgServiceTimeout">Resposta ONVIF/RTSP <span>500–10000 ms</span></label><input id="cfgServiceTimeout" type="number" min="500" max="10000" step="100" required><small>Padrão estável: 3000 ms.</small></div>
          <div class="field"><label for="cfgScanTimeout">Conexão durante varredura <span>20–1000 ms</span></label><input id="cfgScanTimeout" type="number" min="20" max="1000" step="10" required><small>Padrão: 300 ms.</small></div>
          <div class="field"><label for="cfgSnapshotTimeout">Timeout do JPEG <span>1–30 s</span></label><input id="cfgSnapshotTimeout" type="number" min="1" max="30" step="0.5" required><small>Padrão: 5 s.</small></div>
          <div class="field"><label for="cfgSnapshotResolveRetry">Nova resolução do URI <span>5–600 s</span></label><input id="cfgSnapshotResolveRetry" type="number" min="5" max="600" step="1" required><small>Padrão: 15 s.</small></div>
          <div class="field"><label for="cfgSnapshotFailuresReresolve">Falhas antes de resolver o URI <span>1–10</span></label><input id="cfgSnapshotFailuresReresolve" type="number" min="1" max="10" step="1" required><small>Padrão: 2.</small></div>
        </div>
      </section>

      <section class="settings-card settings-panel" id="settingsHardwarePanel" role="tabpanel" aria-labelledby="settingsHardwareTab" data-settings-section-panel="hardware" hidden>
        <div class="settings-card-h"><div><h2>Alerta local</h2><p>LED integrado do ESP32.</p></div></div>
        <div class="form-grid">
          <div class="field"><div class="checkline"><input id="cfgLedEnabled" type="checkbox"><label for="cfgLedEnabled"><b>Piscar o LED quando não estiver online</b><span>O GPIO permanece fixo no firmware para evitar conflito de hardware.</span></label></div></div>
          <div class="field"><label for="cfgLedBlink">Intervalo da piscada <span>200–10000 ms</span></label><input id="cfgLedBlink" type="number" min="200" max="10000" step="100" required><small>Padrão: 1000 ms.</small></div>
        </div>
      </section>
    </div>

    <div class="settings-savebar">
      <div class="button-row">
        <button class="btn warn" id="settingsReset" type="button" disabled>Restaurar padrões</button>
        <button class="btn primary" id="settingsSave" type="submit" disabled>Salvar configurações</button>
      </div>
      <div class="form-result" id="settingsFormStatus">Carregue a aba para consultar os valores atuais.</div>
    </div>
  </form>
</div>

<div class="modal-overlay" id="ptzPresetModal" hidden>
  <div class="modal-box ptz-modal-box" role="dialog" aria-modal="true" aria-labelledby="ptzModalTitle" aria-describedby="ptzMeta">
    <div class="ptz-modal-head">
      <div>
        <h3 id="ptzModalTitle">Posições PTZ</h3>
        <p class="modal-desc" id="ptzMeta">Aguardando o catálogo ONVIF da câmera.</p>
      </div>
      <button class="btn" type="button" id="ptzModalClose">Fechar</button>
    </div>
    <div class="field ptz-filter">
      <label for="ptzPresetFilter">Filtrar posições <span>nome ou token</span></label>
      <input id="ptzPresetFilter" type="text" maxlength="64" autocomplete="off" placeholder="Ex.: Daemon, portão ou 12">
    </div>
    <div class="ptz-list" id="ptzPresetList" role="list">
      <div class="ptz-empty">O ESP32 consultará os presets diretamente na câmera.</div>
    </div>
    <div class="ptz-modal-foot"><span id="ptzVisibleCount">Nenhuma posição carregada</span><span>A Prévia fica pausada enquanto este controle está aberto.</span></div>
  </div>
</div>

<div class="modal-overlay" id="adminConfirmModal" hidden>
  <div class="modal-box" role="dialog" aria-modal="true" aria-labelledby="adminConfirmTitle">
    <h3 id="adminConfirmTitle">Confirmar ação administrativa</h3>
    <p class="modal-desc" id="adminConfirmDesc"></p>
    <div class="field" id="adminConfirmPasswordField" hidden><label for="adminConfirmPassword">Senha administrativa</label><input id="adminConfirmPassword" type="password" maxlength="96" autocomplete="current-password"></div>
    <div class="form-result bad" id="adminConfirmError" hidden></div>
    <div class="button-row modal-actions">
      <button class="btn" type="button" id="adminConfirmCancel">Cancelar</button>
      <button class="btn primary" type="button" id="adminConfirmSubmit">Confirmar</button>
    </div>
  </div>
</div>

<footer id="foot">ESP32 Camera Watchdog</footer>
</main>

<script>
const $ = id => document.getElementById(id);
const PALETTE = {
  online:    ['Online',      'var(--ok)',   'rgba(16,185,129,.14)', 'rgba(16,185,129,.32)'],
  degraded:  ['Instável',    'var(--warn)', 'rgba(245,158,11,.14)', 'rgba(245,158,11,.32)'],
  recovering:['Recuperando', 'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)'],
  offline:   ['Offline',     'var(--bad)',  'rgba(244,63,94,.14)', 'rgba(244,63,94,.32)'],
  discovering:['Procurando', 'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)'],
  starting:  ['Iniciando',   'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)']
};
const HISTORY_LIMIT = 60;
const resourceHistory = { heap: [], rssi: [] };
function viewFromHash() {
  if (location.hash === '#actions') return 'actions';
  if (location.hash === '#settings') return 'settings';
  if (location.hash === '#logs') return 'logs';
  if (location.hash === '#diagnostics') return 'diagnostics';
  if (location.hash === '#resources') return 'resources';
  if (location.hash === '#feed') return 'feed';
  return 'camera';
}

let activeView = viewFromHash();
let activeSettingsSection = 'identification';
let feedMinimumIntervalMs = 5000;
let feedTargetCycleMs = 5000;
let statusRefreshIntervalMs = 3000;
let snapshotHttpTimeoutMs = 5000;
let statusRefreshTimer;
let statusLoading = false;
let statusAbortController = null;
let statusRequestGeneration = 0;
let settingsLoaded = false;
let settingsLoading = false;
let settingsLoadPromise = null;
let settingsBusy = false;
let settingsWriteToken = '';
let settingsPasswordRequired = false;
let ptzPresets = [];
let ptzCatalog = {};
let ptzControlsEnabled = false;
let persistentLogs = [];
let persistentLogsTotal = 0;
let persistentLogsHasMore = false;
let persistentLogsLoading = false;
let persistentLogsLoaded = false;

function duration(totalSeconds) {
  const s = Math.max(0, Math.floor(totalSeconds));
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
  const m = Math.floor(s % 3600 / 60);
  if (d) return d + 'd ' + h + 'h';
  if (h) return h + 'h ' + m + 'min';
  if (m) return m + 'min ' + (s % 60) + 's';
  return s + 's';
}

function localizeTimestampText(value) {
  if (typeof value !== 'string') return value;
  const match = value.match(/^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)(.*)$/);
  if (!match) return value;
  const timestamp = new Date(match[1]);
  return Number.isNaN(timestamp.getTime())
    ? value
    : timestamp.toLocaleString('pt-BR') + match[2];
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
    ? ['Recursos estáveis', 'var(--ok)', 'rgba(16,185,129,.14)', 'rgba(16,185,129,.32)']
    : heapTone === 'warn'
      ? ['Heap em atenção', 'var(--warn)', 'rgba(245,158,11,.14)', 'rgba(245,158,11,.32)']
      : ['Heap baixo', 'var(--bad)', 'rgba(244,63,94,.14)', 'rgba(244,63,94,.32)'];
  const resourceHero = $('resourcesHero');
  resourceHero.style.setProperty('--sc', toneValues[1]);
  resourceHero.style.setProperty('--sbg', toneValues[2]);
  resourceHero.style.setProperty('--sbd', toneValues[3]);
  $('resourcesState').textContent = toneValues[0];
  $('resourcesChip').textContent = resources.chip.model + ' rev. ' + resources.chip.revision;
  $('resourcesMeta').textContent = resources.chip.cores + ' núcleos · '
    + resources.chip.cpuFrequencyMHz + ' MHz · último reset: '
    + (resources.chip.resetReason || 'desconhecido') + ' · ligado há '
    + duration(d.uptimeSeconds);
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

  const onvif = d.onvif || {};
  const onvifReady = Boolean(onvif.catalogResolved && !onvif.usedMediaFallback);
  $('r-onvif').textContent = (onvif.serviceCount || 0) + ' serviços';
  const onvifTone = !onvif.catalogResolved ? 'info' : onvifReady ? 'ok' : 'warn';
  $('r-onvif').className = 'card-v ' + onvifTone;
  $('r-onvif-meta').textContent = !onvif.catalogResolved
    ? 'aguardando consulta GetServices'
    : onvif.usedMediaFallback
      ? 'Media1 convencional em uso · ' + (onvif.lastResult || 'catálogo indisponível')
      : (onvif.lastLatencyMs || 0) + ' ms · endpoints validados no IP da câmera';
  pip('p-ronvif', onvifTone);

  const cameraClock = onvif.clock || {};
  const clockDriftMs = Number(cameraClock.driftMilliseconds || 0);
  const absoluteClockDriftMs = Math.abs(clockDriftMs);
  const clockTone = !cameraClock.checked
    ? 'info'
    : !cameraClock.referenceSynchronized || !cameraClock.utcValid
      ? 'warn'
      : cameraClock.healthy ? 'ok' : absoluteClockDriftMs <= 30000 ? 'warn' : 'bad';
  $('r-clock').textContent = !cameraClock.checked
    ? 'aguardando amostra'
    : !cameraClock.referenceSynchronized
      ? 'referência NTP indisponível'
      : !cameraClock.utcValid
      ? 'amostra inconclusiva'
      : absoluteClockDriftMs < 1000
        ? 'alinhado'
        : (absoluteClockDriftMs / 1000).toFixed(1) + ' s '
          + (clockDriftMs > 0 ? 'adiantada' : 'atrasada');
  $('r-clock').className = 'card-v ' + clockTone;
  const cameraClockResult = cameraClock.referenceSynchronized
    ? (cameraClock.lastResult || 'aguardando observação do relógio')
    : 'Referência NTP do ESP32 indisponível; desvio não avaliado';
  $('r-clock-meta').textContent = cameraClockResult
    + ' · ' + (cameraClock.dateTimeType || 'modo não informado')
    + ' · ' + (cameraClock.reportedTimeZone || 'timezone não informado')
    + (cameraClock.referenceSynchronized ? ' · referência NTP do ESP32' : '');
  pip('p-rclock', clockTone);

  const events = onvif.events || {};
  const eventsServiceAvailable = Boolean(
    onvif.services && onvif.services.events && onvif.services.events.available
  );
  const eventKinds = [
    events.motion && 'movimento',
    events.tamper && 'tamper',
    events.signalLoss && 'perda de sinal',
    events.imageQuality && 'qualidade de imagem'
  ].filter(Boolean);
  const eventsTone = events.propertiesLoaded
    ? 'ok'
    : !onvif.catalogResolved || !eventsServiceAvailable ? 'info' : 'warn';
  $('r-events').textContent = events.propertiesLoaded
    ? (events.topicCount || 0) + ' tópicos'
    : !onvif.catalogResolved
      ? 'aguardando catálogo'
      : eventsServiceAvailable ? 'não consultados' : 'não anunciado';
  $('r-events').className = 'card-v ' + eventsTone;
  $('r-events-meta').textContent = events.propertiesLoaded
    ? (eventKinds.join(' · ') || 'sem tópicos monitoráveis identificados')
    : !eventsServiceAvailable && onvif.catalogResolved
      ? 'a câmera não anunciou o serviço Events'
      : 'GetEventProperties ainda não retornou dados';
  pip('p-revents', eventsTone);

  resourceHistory.heap.push(heapPercent);
  resourceHistory.rssi.push(d.wifi.connected ? d.wifi.rssi : -100);
  if (resourceHistory.heap.length > HISTORY_LIMIT) resourceHistory.heap.shift();
  if (resourceHistory.rssi.length > HISTORY_LIMIT) resourceHistory.rssi.shift();
  $('heapHistory').setAttribute('points', historyPoints(resourceHistory.heap, 0, 100));
  $('rssiHistory').setAttribute('points', historyPoints(resourceHistory.rssi, -100, -30));
  $('historyHeap').textContent = Math.round(heapPercent) + '%';
  $('historyRssi').textContent = d.wifi.connected ? d.wifi.rssi + ' dBm' : 'desconectado';
}

const LOG_LEVEL_LABELS = { info: 'Info', warning: 'Aviso', error: 'Erro' };
const LOG_OUTCOME_LABELS = {
  info: 'informação', started: 'iniciado', success: 'sucesso', failure: 'falha', skipped: 'ignorado'
};
const LOG_EVENT_LABELS = {
  system_boot: 'Inicialização do ESP32', system_restart: 'Reinicialização do ESP32',
  wifi_connect: 'Conexão Wi-Fi', wifi_disconnect: 'Desconexão Wi-Fi', wifi_reconnect: 'Reconexão Wi-Fi',
  camera_state: 'Estado da câmera', incident: 'Incidente', health_check: 'Health-check',
  discovery_probe: 'Envio WS-Discovery', discovery_response: 'Resposta WS-Discovery',
  discovery_result: 'Resultado da descoberta', subnet_scan: 'Varredura da rede',
  tcp_scan: 'Tentativa TCP', onvif_probe: 'Sonda ONVIF', onvif_soap: 'Requisição ONVIF/SOAP',
  snapshot_http: 'Requisição de snapshot', snapshot_transfer: 'Transferência JPEG', rtsp_probe: 'Sonda RTSP',
  email_http: 'Envio de e-mail', settings_saved: 'Configurações salvas', settings_reset: 'Padrões restaurados',
  admin_action: 'Ação administrativa', camera_reboot: 'Reboot da câmera', ptz_command: 'Comando PTZ',
  sntp_sync: 'Sincronização NTP', runtime_context: 'Contexto do firmware',
  settings_context: 'Contexto das configurações', camera_target_load: 'Destino da câmera carregado',
  camera_target_save: 'Destino da câmera salvo', logs_cleared: 'Histórico de logs zerado'
};

function persistentLogFlags(entry) {
  const flags = entry.flags || {};
  return [
    flags.outbound && 'saída',
    flags.connectionReused && 'conexão reutilizada',
    flags.authenticationRetry && 'autenticação',
    flags.manual && 'manual',
    flags.scheduled && 'automático',
    entry.timeoutMs && 'timeout ' + entry.timeoutMs + ' ms'
  ].filter(Boolean).join(' · ');
}

function persistentLogStatusLabel(entry) {
  if (!entry.status) return '';
  if (entry.event === 'runtime_context' || entry.event === 'settings_context') {
    return 'schema ' + entry.status;
  }
  const detail = String(entry.detail || '');
  if (
    detail.startsWith('connect_') ||
    detail.startsWith('connection_') ||
    detail.endsWith('_unreachable')
  ) {
    return entry.status > 0 ? 'errno ' + entry.status : 'erro de socket';
  }
  return String(entry.status);
}

function persistentLogValueLabel(entry) {
  if (!entry.value || entry.timeoutMs) return '';
  if (entry.event === 'runtime_context') return 'padrões rev. ' + entry.value;
  if (entry.event === 'logs_cleared') return entry.value + ' registros removidos';
  if (entry.event === 'camera_target_load' || entry.event === 'camera_target_save') {
    return 'RTSP ' + entry.value;
  }
  if (entry.event === 'discovery_response') return entry.value + ' bytes';
  return 'valor ' + entry.value;
}

function persistentLogMatches(entry) {
  const level = $('logsLevel').value;
  const category = $('logsCategory').value;
  const scope = $('logsScope').value;
  const query = $('logsSearch').value.trim().toLocaleLowerCase('pt-BR');
  if (level && entry.level !== level) return false;
  if (category && entry.category !== category) return false;
  const routineTcpFailure = entry.event === 'tcp_scan' && entry.outcome === 'info' &&
    ['port_closed', 'connection_refused', 'connect_timeout'].includes(entry.detail);
  if (scope === 'diagnostic' && routineTcpFailure) return false;
  if (!query) return true;
  const searchable = [
    entry.id, entry.bootId, entry.contextId, entry.timestamp, entry.timestampUtc,
    entry.level, entry.category, entry.event, entry.outcome, entry.targetIp, entry.targetPort,
    entry.status, entry.timeoutMs, entry.detail, persistentLogFlags(entry),
    LOG_EVENT_LABELS[entry.event]
  ].join(' ').toLocaleLowerCase('pt-BR');
  return searchable.includes(query);
}

function appendPersistentLogCell(row, text, className) {
  const cell = document.createElement('td');
  if (className) cell.className = className;
  cell.textContent = text;
  row.append(cell);
  return cell;
}

function renderPersistentLogs() {
  const entries = persistentLogs.filter(persistentLogMatches);
  const fragment = document.createDocumentFragment();

  entries.forEach(entry => {
    const row = document.createElement('tr');
    const timestamp = appendPersistentLogCell(row, localizeTimestampText(entry.timestamp), 'mono');
    const timestampMeta = document.createElement('span');
    timestampMeta.className = 'logs-flags';
    timestampMeta.textContent = [
      '#' + entry.id,
      entry.bootId ? 'boot ' + entry.bootId : '',
      entry.contextId ? 'contexto ' + entry.contextId : '',
      entry.timeSynchronized ? '' : 'sem horário NTP',
      'uptime ' + entry.uptimeMs + ' ms'
    ].filter(Boolean).join(' · ');
    timestamp.append(document.createElement('br'), timestampMeta);

    const levelCell = document.createElement('td');
    const level = document.createElement('span');
    level.className = 'logs-level ' + entry.level;
    level.textContent = LOG_LEVEL_LABELS[entry.level] || entry.level;
    levelCell.append(level);
    row.append(levelCell);

    const eventCell = document.createElement('td');
    const eventName = document.createElement('span');
    eventName.className = 'logs-event';
    eventName.textContent = LOG_EVENT_LABELS[entry.event] || entry.event;
    const category = document.createElement('span');
    category.className = 'logs-flags';
    category.textContent = entry.category;
    eventCell.append(eventName, category);
    row.append(eventCell);

    appendPersistentLogCell(
      row,
      entry.targetIp
        ? entry.targetIp + (entry.targetPort ? ':' + entry.targetPort : '')
        : entry.targetPort ? 'externo:' + entry.targetPort : 'local',
      'mono'
    );
    appendPersistentLogCell(
      row,
      (LOG_OUTCOME_LABELS[entry.outcome] || entry.outcome)
        + (persistentLogStatusLabel(entry) ? ' · ' + persistentLogStatusLabel(entry) : ''),
      'mono'
    );
    appendPersistentLogCell(row, entry.durationMs + ' ms', 'mono');

    const detailCell = appendPersistentLogCell(row, entry.detail || '—', 'logs-detail');
    const flags = [
      persistentLogFlags(entry),
      persistentLogValueLabel(entry)
    ].filter(Boolean).join(' · ');
    if (flags) {
      const flagsElement = document.createElement('span');
      flagsElement.className = 'logs-flags';
      flagsElement.textContent = flags;
      detailCell.append(document.createElement('br'), flagsElement);
    }
    fragment.append(row);
  });

  $('logsTableBody').replaceChildren(fragment);
  $('logsEmpty').hidden = entries.length > 0;
  $('logsEmpty').textContent = persistentLogs.length
    ? 'Nenhum dos registros carregados corresponde aos filtros.'
    : 'Nenhum registro persistente disponível.';
  $('logsLoadMore').hidden = !persistentLogsHasMore;
  $('logsStatus').className = 'form-result';
  $('logsStatus').textContent = persistentLogsLoaded
    ? entries.length + ' exibidos · ' + persistentLogs.length + ' carregados de ' + persistentLogsTotal
    : 'Carregando histórico persistente…';
}

function renderPersistentLogStats(payload) {
  const total = Number(payload.total) || 0;
  const capacity = Number(payload.capacity) || 0;
  const storage = payload.storage || {};
  const countPercent = capacity ? total / capacity * 100 : 0;
  const storagePercent = storage.totalBytes ? storage.usedBytes / storage.totalBytes * 100 : 0;
  const integrityErrors = (Number(storage.dropped) || 0) + (Number(storage.writeErrors) || 0);

  $('logsCount').textContent = total.toLocaleString('pt-BR');
  $('logsCountMeta').textContent = capacity
    ? Math.round(countPercent) + '% do limite circular de ' + capacity.toLocaleString('pt-BR')
    : 'armazenamento indisponível';
  $('logsCountMeter').style.width = Math.min(100, countPercent) + '%';
  $('logsCountMeter').className = countPercent >= 90 ? 'warn' : '';

  $('logsStorage').textContent = formatBytes(Number(storage.usedBytes) || 0);
  $('logsStorageMeta').textContent = formatBytes(Number(storage.fileBytes) || 0)
    + ' no arquivo · ' + formatBytes(Number(storage.totalBytes) || 0) + ' disponíveis na partição';
  $('logsStorageMeter').style.width = Math.min(100, storagePercent) + '%';
  $('logsStorageMeter').className = storagePercent >= 90 ? 'warn' : '';

  $('logsIntegrity').textContent = integrityErrors ? integrityErrors + ' ocorrências' : 'íntegro';
  $('logsIntegrity').className = 'card-v ' + (integrityErrors ? 'bad' : 'ok');
  $('logsIntegrityMeta').textContent = (Number(storage.dropped) || 0) + ' descartados · '
    + (Number(storage.writeErrors) || 0) + ' erros de escrita · '
    + (Number(storage.pending) || 0) + ' pendentes';
  $('logsState').textContent = payload.ready ? 'histórico persistente ativo' : 'partição de logs indisponível';
}

async function loadPersistentLogs(reset) {
  if (persistentLogsLoading) return;
  persistentLogsLoading = true;
  $('logsRefresh').disabled = true;
  $('logsLoadMore').disabled = true;
  if (reset) {
    persistentLogs = [];
    persistentLogsHasMore = false;
    persistentLogsLoaded = false;
    renderPersistentLogs();
  }

  try {
    const offset = reset ? 0 : persistentLogs.length;
    const response = await fetch('/api/logs?offset=' + offset + '&limit=100', { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const payload = await response.json();
    const entries = Array.isArray(payload.entries) ? payload.entries : [];
    if (reset) {
      persistentLogs = entries;
    } else {
      const knownIds = new Set(persistentLogs.map(entry => entry.id));
      persistentLogs.push(...entries.filter(entry => !knownIds.has(entry.id)));
    }
    persistentLogsTotal = Number(payload.total) || 0;
    persistentLogsHasMore = Boolean(payload.hasMore) && persistentLogs.length < persistentLogsTotal;
    persistentLogsLoaded = true;
    renderPersistentLogStats(payload);
    renderPersistentLogs();
  } catch (error) {
    $('logsState').textContent = 'falha ao consultar os logs';
    $('logsStatus').textContent = 'Não foi possível carregar os logs: ' + error.message;
    $('logsStatus').className = 'form-result bad';
  } finally {
    persistentLogsLoading = false;
    $('logsRefresh').disabled = false;
    $('logsLoadMore').disabled = false;
  }
}

$('logsRefresh').addEventListener('click', () => loadPersistentLogs(true));
$('logsLoadMore').addEventListener('click', () => loadPersistentLogs(false));
['logsSearch', 'logsLevel', 'logsCategory', 'logsScope'].forEach(id => {
  $(id).addEventListener(id === 'logsSearch' ? 'input' : 'change', renderPersistentLogs);
});

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

function setRtspProbe(probes) {
  const value = $('v-rtsp'), meta = $('m-rtsp');
  if (probes.rtspAttempted) {
    setProbe(
      'rtsp',
      probes.rtspTcp,
      probes.rtspProtocol,
      probes.rtspStatus,
      probes.rtspLatencyMs,
      'RTSP'
    );
    return;
  }
  value.textContent = probes.onvifHttp ? 'sonda dispensada' : 'aguardando';
  value.className = 'card-v info';
  pip('p-rtsp', 'info');
  meta.textContent = probes.onvifHttp
    ? 'ONVIF respondeu; a sonda RTSP bruta não foi necessária'
    : 'a sonda RTSP ainda não foi executada';
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

  if (!snapshot.conclusive) {
    value.textContent = 'teste adiado';
    value.className = 'card-v info';
    pip('p-image', 'info');
    meta.textContent = snapshot.detail || 'aguardando uma amostra JPEG conclusiva';
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
let feedWatchdogTimer;
let feedRequestGeneration = 0;
let feedDeliveredFrames = 0;
let feedHealthyMetaPrefix = '';
let feedPausedForPtz = false;
let feedActivationPending = false;
// Os intervalos da previa sao atualizados pelo JSON do firmware.

function updateFeedHealthyMeta() {
  if (!feedHealthyMetaPrefix) return;
  $('feedMeta').textContent = feedHealthyMetaPrefix + ' · '
    + feedDeliveredFrames + ' quadros entregues';
}

function cancelFeedLoad(resetFailures) {
  feedRequestGeneration++;
  clearTimeout(feedWatchdogTimer);
  feedWatchdogTimer = null;
  const frame = $('cameraFeed');
  frame.onload = null;
  frame.onerror = null;
  frame.removeAttribute('src');
  feedLoading = false;
  if (resetFailures) feedFailures = 0;
}

function pauseFeedForPtz() {
  feedPausedForPtz = true;
  clearTimeout(feedTimer);
  feedTimer = null;
  $('feedStamp').textContent = 'prévia pausada para controle PTZ';
}

function resumeFeedAfterPtz(delay) {
  feedPausedForPtz = false;
  if (!document.hidden && activeView === 'feed') scheduleFeed(delay || 0);
}

async function waitForFeedIdle() {
  for (let attempt = 0; attempt < 20 && feedLoading; attempt++) {
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  if (feedLoading) {
    cancelFeedLoad(false);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}

function scheduleFeed(delay) {
  clearTimeout(feedTimer);
  if (document.hidden || activeView !== 'feed' || feedPausedForPtz || feedActivationPending) return;
  feedTimer = setTimeout(() => {
    feedTimer = null;
    loadFeed();
  }, delay);
}

function loadFeed() {
  if (document.hidden || activeView !== 'feed' || feedLoading || feedPausedForPtz || feedActivationPending) return;
  if (statusLoading) {
    scheduleFeed(250);
    return;
  }
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
  const requestGeneration = ++feedRequestGeneration;
  clearTimeout(feedWatchdogTimer);
  feedWatchdogTimer = setTimeout(() => {
    if (requestGeneration !== feedRequestGeneration || !feedLoading) return;
    cancelFeedLoad(false);
    feedFailures++;
    frame.classList.remove('ready');
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'A imagem demorou demais; tentando novamente…';
    $('feedStamp').textContent = 'prévia temporariamente indisponível';
    scheduleFeed(Math.min(8000, 1000 * Math.pow(2, Math.min(feedFailures, 3))));
  }, Math.max(12000, Math.min(65000, snapshotHttpTimeoutMs * 2 + 5000)));
  frame.onload = () => {
    if (requestGeneration !== feedRequestGeneration) return;
    clearTimeout(feedWatchdogTimer);
    feedWatchdogTimer = null;
    feedLoading = false;
    feedFailures = 0;
    frame.classList.add('ready');
    $('feedState').classList.add('hidden');
    $('feedStamp').textContent = feedPausedForPtz
      ? 'prévia pausada para controle PTZ'
      : 'atualizado ' + new Date().toLocaleTimeString('pt-BR');
    lastOk = Date.now();
    feedDeliveredFrames++;
    updateFeedHealthyMeta();
    const elapsed = Date.now() - feedRequestedAtMs;
    scheduleFeed(Math.max(feedMinimumIntervalMs, feedTargetCycleMs - elapsed));
  };
  frame.onerror = () => {
    if (requestGeneration !== feedRequestGeneration) return;
    clearTimeout(feedWatchdogTimer);
    feedWatchdogTimer = null;
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

function activateSettingsSection(name) {
  const available = [...document.querySelectorAll('[data-settings-section-panel]')];
  const target = available.find(panel => panel.dataset.settingsSectionPanel === name);
  if (!target) return;

  activeSettingsSection = name;
  available.forEach(panel => {
    panel.hidden = panel !== target;
  });
  document.querySelectorAll('.settings-tab[data-settings-section]').forEach(tab => {
    const selected = tab.dataset.settingsSection === name;
    tab.setAttribute('aria-selected', selected ? 'true' : 'false');
    tab.tabIndex = selected ? 0 : -1;
  });
}

document.querySelectorAll('.settings-tab[data-settings-section]').forEach(tab => {
  tab.addEventListener('click', () => activateSettingsSection(tab.dataset.settingsSection));
});

function validateSettingsForm() {
  [['cfgCameraUsername', 64], ['cfgCameraPassword', 128]].forEach(([id, limit]) => {
    const input = $(id);
    let error = '';
    if (!input.disabled) {
      if (new TextEncoder().encode(input.value).length > limit) {
        error = 'O limite é de ' + limit + ' bytes; caracteres acentuados podem ocupar mais de um byte.';
      } else if (/[\u0000-\u001F\u007F]/.test(input.value)) {
        error = 'Não use caracteres de controle.';
      } else if (id === 'cfgCameraUsername' && input.value.includes(':')) {
        error = 'O usuário não pode conter dois-pontos.';
      }
    }
    input.setCustomValidity(error);
  });
  const invalid = $('settingsForm').querySelector(':invalid');
  if (!invalid) return true;
  const panel = invalid.closest('[data-settings-section-panel]');
  if (panel) activateSettingsSection(panel.dataset.settingsSectionPanel);
  invalid.reportValidity();
  return false;
}

const SETTINGS_FIELDS = {
  cameraName: { id: 'cfgCameraName', kind: 'text' },
  cameraUsername: { id: 'cfgCameraUsername', kind: 'text', trim: false },
  localHostname: { id: 'cfgLocalHostname', kind: 'text' },
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
  target.textContent = localizeTimestampText(message);
  target.className = 'form-result' + (tone ? ' ' + tone : '');
}

function setAdminActionStatus(message, tone) {
  const target = $('adminActionStatus');
  target.textContent = localizeTimestampText(message);
  target.className = 'form-result' + (tone ? ' ' + tone : '');
}

function setPtzActionStatus(message, tone) {
  const target = $('ptzActionStatus');
  const isProblem = tone === 'bad';
  target.hidden = !isProblem;
  target.textContent = isProblem ? localizeTimestampText(message) : '';
  target.className = 'form-result' + (isProblem ? ' bad' : '');
}

function setMutationControlsEnabled(enabled) {
  $('settingsSave').disabled = !enabled;
  $('settingsReset').disabled = !enabled;
  document.querySelectorAll('[data-admin-action]').forEach(button => {
    button.disabled = !enabled;
  });
  document.querySelectorAll('[data-ptz-preset-token]').forEach(button => {
    button.disabled = !enabled || !ptzControlsEnabled;
  });
}

function settingsAuthorizationReady(source) {
  if (!settingsWriteToken) {
    if (source === 'actions') {
      setAdminActionStatus('As ações permanecem bloqueadas até a autorização ser carregada.', 'bad');
    } else if (source === 'ptz') {
      setPtzActionStatus('O controle PTZ aguarda a autorização do ESP32.', 'bad');
    } else {
      setSettingsStatus('As configurações ainda não foram carregadas. Aguarde e tente novamente.', 'bad');
    }
    loadSettings(true);
    return false;
  }

  return true;
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

function updateCameraCredentialFields() {
  const action = $('cfgCameraPasswordAction').value;
  $('cfgCameraUsername').disabled = action === 'clear';
  $('cfgCameraUsername').setCustomValidity('');
  $('cfgCameraPassword').disabled = action !== 'replace';
  $('cfgCameraPassword').setCustomValidity('');
  if (action !== 'replace') $('cfgCameraPassword').value = '';
}

$('cfgCameraPasswordAction').addEventListener('change', updateCameraCredentialFields);

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
  $('cfgCameraPasswordAction').value = 'keep';
  $('cfgCameraPassword').value = '';
  $('cfgCameraPasswordStatus').textContent = payload.security && payload.security.cameraPasswordConfigured
    ? 'Senha configurada. Salvar outros ajustes mantém a senha atual.'
    : 'Nenhuma senha configurada.';
  updateCameraCredentialFields();
  $('settingsSource').textContent = payload.source === 'nvs'
    ? 'configurações salvas na NVS'
    : 'valores padrão de fábrica';
  $('settingsMeta').textContent = 'esquema ' + payload.schemaVersion
    + ' · padrões ' + (payload.defaultsRevision || 0) + ' · revisão ' + payload.revision
    + ' · ' + localizeTimestampText(payload.lastResult || 'configurações carregadas');

  feedMinimumIntervalMs = Number(values.feedMinIntervalMs) || feedMinimumIntervalMs;
  feedTargetCycleMs = Number(values.feedTargetCycleMs) || feedTargetCycleMs;
  statusRefreshIntervalMs = Number(values.statusRefreshIntervalMs) || statusRefreshIntervalMs;
  settingsLoaded = true;
  updateSettingsEstimates();
  setMutationControlsEnabled(true);
  if (activeView === 'actions') {
    setAdminActionStatus('Autorização carregada. Escolha uma ação administrativa.', 'ok');
  } else if (activeView === 'feed') {
    if (!ptzCatalog.command || Number(ptzCatalog.command.attempts) === 0) {
      setPtzActionStatus('Controle PTZ autorizado. Abra a lista para escolher uma posição.', 'ok');
    }
  } else {
    setSettingsStatus('Configurações carregadas. Alterações só são gravadas ao clicar em Salvar.', 'ok');
  }
}

function loadSettings(force) {
  if (settingsLoading) return settingsLoadPromise || Promise.resolve();
  if (settingsLoaded && !force) return Promise.resolve();
  settingsLoading = true;
  settingsLoadPromise = (async () => {
    if (activeView === 'actions') {
      setAdminActionStatus('Carregando autorização administrativa…', 'warn');
    } else if (activeView === 'feed') {
      setPtzActionStatus('Carregando autorização do controle PTZ…', 'warn');
    } else {
      setSettingsStatus('Lendo configurações do ESP32…', 'warn');
    }
    try {
      const response = await fetch('/api/settings', { cache: 'no-store' });
      const payload = await response.json();
      if (!response.ok || !payload.ok) throw new Error(payload.detail || 'HTTP ' + response.status);
      populateSettings(payload);
    } catch (error) {
      setMutationControlsEnabled(false);
      if (activeView === 'actions') {
        setAdminActionStatus('Falha ao carregar autorização: ' + error.message, 'bad');
      } else if (activeView === 'feed') {
        setPtzActionStatus('Falha ao carregar autorização PTZ: ' + error.message, 'bad');
      } else {
        setSettingsStatus('Falha ao carregar: ' + error.message, 'bad');
      }
    } finally {
      settingsLoading = false;
      settingsLoadPromise = null;
    }
  })();
  return settingsLoadPromise;
}

function collectMutationParams(includeSettings, password) {
  const params = new URLSearchParams();
  params.set('writeToken', settingsWriteToken);
  if (settingsPasswordRequired) {
    params.set('adminPassword', password || '');
  }

  if (includeSettings) {
    Object.entries(SETTINGS_FIELDS).forEach(([key, descriptor]) => {
      const input = $(descriptor.id);
      if (descriptor.kind === 'bool') {
        params.set(key, input.checked ? '1' : '0');
      } else if (descriptor.kind === 'text') {
        params.set(key, descriptor.trim === false ? input.value : input.value.trim());
      } else {
        const numeric = Number(input.value);
        params.set(key, String(Math.round(numeric * (descriptor.scale || 1))));
      }
    });
    const passwordAction = $('cfgCameraPasswordAction').value;
    params.set('cameraPasswordAction', passwordAction);
    params.set('cameraPassword', passwordAction === 'replace' ? $('cfgCameraPassword').value : '');
    if (passwordAction === 'clear') params.set('cameraUsername', '');
  }
  return params;
}

async function postForm(url, params, options) {
  const response = await fetch(url, {
    method: 'POST',
    cache: 'no-store',
    ...(options || {}),
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
$('settingsForm').addEventListener('submit', event => {
  event.preventDefault();
  if (settingsBusy || !validateSettingsForm() || !settingsAuthorizationReady('settings')) return;
  openConfirmModal('Gravar as configurações alteradas na NVS.', password => saveSettings(password));
});

async function saveSettings(password) {
  settingsBusy = true;
  $('settingsSave').disabled = true;
  $('settingsReset').disabled = true;
  setSettingsStatus('Validando e gravando na NVS…', 'warn');
  try {
    const payload = await postForm('/api/settings', collectMutationParams(true, password));
    populateSettings(payload);
    setSettingsStatus(
      'Configurações salvas e aplicadas. Endereço: http://'
        + payload.values.localHostname + '.local/',
      'ok'
    );
    refresh();
  } catch (error) {
    setSettingsStatus('Não foi possível salvar: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

$('settingsReset').addEventListener('click', () => {
  if (settingsBusy || !settingsAuthorizationReady('settings')) return;
  openConfirmModal('Restaurar os ajustes operacionais para os padrões de fábrica, mantendo o usuário e a senha da câmera.', password => resetSettings(password));
});

async function resetSettings(password) {
  settingsBusy = true;
  $('settingsSave').disabled = true;
  $('settingsReset').disabled = true;
  setSettingsStatus('Restaurando os valores de fábrica…', 'warn');
  try {
    const payload = await postForm('/api/settings/reset', collectMutationParams(false, password));
    populateSettings(payload);
    setSettingsStatus(
      'Padrões restaurados e aplicados. Endereço: http://'
        + payload.values.localHostname + '.local/',
      'ok'
    );
    refresh();
  } catch (error) {
    setSettingsStatus('Não foi possível restaurar: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

const ADMIN_ACTIONS = {
  'health-check': ['/api/actions/health-check', 'Verificação manual solicitada.', false],
  rediscover: ['/api/actions/rediscover', 'Redescoberta solicitada.', false],
  'reboot-camera': ['/api/actions/reboot-camera', 'Reboot ONVIF solicitado.', true],
  'reset-reboot-limiter': ['/api/actions/reset-reboot-limiter', 'Limitador de reboots será limpo.', true],
  'clear-logs': ['/api/actions/clear-logs', 'Todos os registros atuais serão removidos permanentemente e substituídos por um marcador de auditoria.', true],
  'reboot-esp32': ['/api/actions/reboot-esp32', 'O ESP32 será reiniciado.', true]
};

let pendingConfirm = null;
let pendingConfirmResumesFeed = false;

function openConfirmModal(description, onConfirm, resumeFeedOnCancel) {
  pendingConfirm = onConfirm;
  pendingConfirmResumesFeed = Boolean(resumeFeedOnCancel);
  $('adminConfirmDesc').textContent = description + ' Confirme para continuar.';
  $('adminConfirmPasswordField').hidden = !settingsPasswordRequired;
  $('adminConfirmPassword').value = '';
  $('adminConfirmError').hidden = true;
  $('adminConfirmModal').hidden = false;
  (settingsPasswordRequired ? $('adminConfirmPassword') : $('adminConfirmSubmit')).focus();
}

function closeConfirmModal(resumePausedFeed) {
  const shouldResumeFeed = Boolean(resumePausedFeed && pendingConfirmResumesFeed);
  $('adminConfirmModal').hidden = true;
  pendingConfirm = null;
  pendingConfirmResumesFeed = false;
  if (shouldResumeFeed) resumeFeedAfterPtz(0);
}

async function executeAdminAction(action, password) {
  settingsBusy = true;
  document.querySelectorAll('[data-admin-action],[data-ptz-preset-token]').forEach(item => { item.disabled = true; });
  setAdminActionStatus('Enviando ação ao ESP32…', 'warn');
  try {
    const payload = await postForm(action[0], collectMutationParams(false, password));
    setAdminActionStatus(payload.message || action[1], 'ok');
    setTimeout(refresh, 700);
  } catch (error) {
    setAdminActionStatus('Ação recusada: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

document.querySelectorAll('[data-admin-action]').forEach(button => {
  button.addEventListener('click', () => {
    const action = ADMIN_ACTIONS[button.dataset.adminAction];
    if (!action || settingsBusy || !settingsAuthorizationReady('actions')) return;
    openConfirmModal(action[1], password => executeAdminAction(action, password));
  });
});

async function refreshPtzCommandResult(previousAttempts) {
  for (let attempt = 0; attempt < 5; attempt++) {
    await new Promise(resolve => setTimeout(resolve, attempt ? 350 : 600));
    await refresh(true);
    const currentAttempts = Number(ptzCatalog.command && ptzCatalog.command.attempts) || 0;
    if (currentAttempts > previousAttempts) return;
  }
}

async function executePtzPreset(presetToken, password) {
  const previousAttempts = Number(ptzCatalog.command && ptzCatalog.command.attempts) || 0;
  let commandQueued = false;
  settingsBusy = true;
  document.querySelectorAll('[data-admin-action],[data-ptz-preset-token]').forEach(item => { item.disabled = true; });
  pauseFeedForPtz();
  setPtzActionStatus('Enviando posição ' + presetToken + ' à câmera…', 'warn');
  try {
    await waitForFeedIdle();
    const params = collectMutationParams(false, password);
    params.set('presetToken', presetToken);
    const payload = await postForm('/api/actions/goto-ptz-preset', params);
    commandQueued = true;
    setPtzActionStatus(payload.message || 'Movimento PTZ enfileirado.', 'ok');
  } catch (error) {
    setPtzActionStatus('Movimento recusado: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
    if (commandQueued) await refreshPtzCommandResult(previousAttempts);
    resumeFeedAfterPtz(250);
  }
}

function ptzPresetTitle(preset) {
  const token = String(preset.token || '');
  const publishedName = String(preset.name || '');
  return preset.daemon
    ? 'Ponto Daemon'
    : publishedName && publishedName !== token
      ? publishedName
      : 'Posição ' + token;
}

function filterPtzPresets(presets, query) {
  const normalizedQuery = String(query || '').trim().toLocaleLowerCase('pt-BR');
  if (!normalizedQuery) return presets;
  return presets.filter(preset => {
    const searchable = ptzPresetTitle(preset) + ' ' + String(preset.token || '');
    return searchable.toLocaleLowerCase('pt-BR').includes(normalizedQuery);
  });
}

function renderPtzPresetList() {
  const list = $('ptzPresetList');
  const fragment = document.createDocumentFragment();
  const visiblePresets = filterPtzPresets(ptzPresets, $('ptzPresetFilter').value);

  if (!ptzCatalog.available || !ptzCatalog.presetsLoaded || !ptzPresets.length) {
    const empty = document.createElement('div');
    empty.className = 'ptz-empty';
    empty.textContent = !ptzCatalog.available
      ? 'A câmera não anunciou o serviço PTZ no catálogo ONVIF.'
      : !ptzCatalog.presetsLoaded
        ? localizeTimestampText(ptzCatalog.lastResult || 'Consultando os presets da câmera…')
        : 'A câmera não publicou posições para este perfil.';
    fragment.append(empty);
  } else if (!visiblePresets.length) {
    const empty = document.createElement('div');
    empty.className = 'ptz-empty';
    empty.textContent = 'Nenhuma posição corresponde ao filtro informado.';
    fragment.append(empty);
  } else {
    visiblePresets.forEach(preset => {
      const token = String(preset.token || '');
      const titleText = ptzPresetTitle(preset);
      const item = document.createElement('div');
      item.className = 'ptz-list-item';
      item.setAttribute('role', 'listitem');
      const copy = document.createElement('div');
      copy.className = 'ptz-list-copy';
      const title = document.createElement('b');
      title.textContent = titleText;
      const detail = document.createElement('span');
      detail.textContent = preset.daemon
        ? 'Token ONVIF ' + token + ' · retorno Daemon confirmado'
        : 'Token ONVIF ' + token;
      const button = document.createElement('button');
      button.className = 'btn' + (preset.daemon ? ' primary' : '');
      button.type = 'button';
      button.dataset.ptzPresetToken = token;
      button.dataset.ptzPresetLabel = titleText;
      button.disabled = !ptzControlsEnabled;
      button.textContent = 'Ir';
      copy.append(title, detail);
      item.append(copy, button);
      fragment.append(item);
    });
  }

  list.replaceChildren(fragment);
  $('ptzVisibleCount').textContent = ptzPresets.length
    ? visiblePresets.length === ptzPresets.length
      ? plural(ptzPresets.length, 'posição disponível', 'posições disponíveis')
      : visiblePresets.length + ' de ' + ptzPresets.length + ' posições exibidas'
    : 'Nenhuma posição carregada';
}

function openPtzPresetModal() {
  if ($('ptzMenuButton').disabled) return;
  pauseFeedForPtz();
  $('ptzPresetFilter').value = '';
  renderPtzPresetList();
  $('ptzPresetModal').hidden = false;
  $('ptzModalClose').focus();
}

function closePtzPresetModal(resumePausedFeed) {
  $('ptzPresetModal').hidden = true;
  if (resumePausedFeed) resumeFeedAfterPtz(0);
}

$('ptzMenuButton').addEventListener('click', openPtzPresetModal);
$('ptzModalClose').addEventListener('click', () => closePtzPresetModal(true));
$('ptzPresetModal').addEventListener('click', event => {
  if (event.target === $('ptzPresetModal')) closePtzPresetModal(true);
});
$('ptzPresetFilter').addEventListener('input', renderPtzPresetList);
$('ptzPresetList').addEventListener('click', event => {
  const button = event.target.closest('[data-ptz-preset-token]');
  if (!button || settingsBusy || !settingsAuthorizationReady('ptz')) return;
  const presetToken = button.dataset.ptzPresetToken;
  const presetLabel = button.dataset.ptzPresetLabel || 'posição ' + presetToken;
  closePtzPresetModal(false);
  openConfirmModal(
    'Mover a câmera para ' + presetLabel + '.',
    password => executePtzPreset(presetToken, password),
    true
  );
});

$('adminConfirmCancel').addEventListener('click', () => closeConfirmModal(true));
$('adminConfirmModal').addEventListener('click', event => {
  if (event.target === $('adminConfirmModal')) closeConfirmModal(true);
});
document.addEventListener('keydown', event => {
  if (event.key !== 'Escape') return;
  if (!$('adminConfirmModal').hidden) {
    closeConfirmModal(true);
  } else if (!$('ptzPresetModal').hidden) {
    closePtzPresetModal(true);
  }
});
$('adminConfirmPassword').addEventListener('keydown', event => {
  if (event.key === 'Enter') {
    event.preventDefault();
    $('adminConfirmSubmit').click();
  }
});
$('adminConfirmSubmit').addEventListener('click', () => {
  if (!pendingConfirm) return;
  const password = $('adminConfirmPassword').value;
  if (settingsPasswordRequired && !password) {
    $('adminConfirmError').textContent = 'Informe a senha administrativa antes de continuar.';
    $('adminConfirmError').hidden = false;
    $('adminConfirmPassword').focus();
    return;
  }
  const action = pendingConfirm;
  closeConfirmModal(false);
  action(password);
});

function renderPtz(d) {
  ptzCatalog = d.onvif && d.onvif.ptz ? d.onvif.ptz : {};
  ptzPresets = Array.isArray(ptzCatalog.presets) ? ptzCatalog.presets : [];
  const pending = Boolean(d.adminAction && d.adminAction.pending !== 'none');
  ptzControlsEnabled = Boolean(settingsWriteToken) && !settingsBusy && !pending;
  const usableCount = ptzPresets.length;
  const menuButton = $('ptzMenuButton');
  menuButton.disabled = !ptzCatalog.available || !ptzCatalog.presetsLoaded || !usableCount;
  $('ptzPresetCount').textContent = !ptzCatalog.presetsLoaded
    ? '—'
    : String(usableCount) + (ptzCatalog.truncated ? '+' : '');
  menuButton.setAttribute(
    'aria-label',
    usableCount
      ? 'Abrir ' + plural(usableCount, 'posição PTZ', 'posições PTZ')
      : 'Posições PTZ indisponíveis'
  );

  $('ptzMeta').textContent = !ptzCatalog.available
    ? 'Serviço PTZ não anunciado pela câmera.'
    : !ptzCatalog.presetsLoaded
      ? localizeTimestampText(ptzCatalog.lastResult || 'Consultando a câmera…')
      : (ptzCatalog.reportedPresetCount || usableCount) + ' posições publicadas · perfil '
        + (ptzCatalog.profileName || ptzCatalog.profileToken || 'ONVIF') + ' · atualização há '
        + duration(ptzCatalog.ageSeconds || 0)
        + (ptzCatalog.truncated ? ' · lista limitada pelo ESP32' : '');

  renderPtzPresetList();

  if (
    !settingsBusy &&
    ptzCatalog.command &&
    Number(ptzCatalog.command.attempts) > 0 &&
    ptzCatalog.command.lastResult
  ) {
    setPtzActionStatus(
      ptzCatalog.command.lastResult,
      ptzCatalog.command.lastStatus >= 200 && ptzCatalog.command.lastStatus < 300 ? 'ok' : 'bad'
    );
  } else if (!ptzCatalog.available) {
    setPtzActionStatus('Esta câmera não anunciou controle PTZ.', 'bad');
  } else if (!ptzCatalog.presetsLoaded) {
    setPtzActionStatus('Consultando as posições PTZ da câmera…', 'warn');
  } else if (!usableCount) {
    setPtzActionStatus('A câmera não publicou posições PTZ.', 'bad');
  } else if (!settingsWriteToken) {
    setPtzActionStatus('Carregando autorização do controle PTZ…', 'warn');
  } else {
    setPtzActionStatus('', '');
  }
}

function render(d) {
  const [label, color, bg, border] = PALETTE[d.state] || ['Desconhecido', 'var(--dim)', '', ''];
  const hero = $('hero');
  hero.style.setProperty('--sc', color);
  hero.style.setProperty('--sbg', bg);
  hero.style.setProperty('--sbd', border);
  $('stateText').textContent = label + ' · há ' + duration(d.stateSinceSeconds);

  $('camName').textContent = d.camera.name;
  const esp32Address = d.wifi.mdnsAvailable
    ? d.wifi.localUrl
    : 'http://' + d.wifi.ip + '/';
  $('camSub').innerHTML = d.camera.known
    ? 'IP <b>' + d.camera.ip + '</b> · RTSP <b>' + d.camera.rtspPort + '</b> · ESP32 em <b>' + esp32Address + '</b>'
    : 'Câmera ainda não localizada na rede · ESP32 em <b>' + esp32Address + '</b>';
  cameraKnown = d.camera.known;
  cameraRecovering = Boolean(d.recovery && d.recovery.graceActive);

  if (d.ui) {
    feedMinimumIntervalMs = Number(d.ui.feedMinimumIntervalMs) || feedMinimumIntervalMs;
    feedTargetCycleMs = Number(d.ui.feedTargetCycleMs) || feedTargetCycleMs;
    statusRefreshIntervalMs = Number(d.ui.statusRefreshIntervalMs) || statusRefreshIntervalMs;
    snapshotHttpTimeoutMs = Number(d.ui.snapshotHttpTimeoutMs) || snapshotHttpTimeoutMs;
  }

  if (d.adminAction && d.adminAction.lastResult && activeView === 'actions') {
    setAdminActionStatus(d.adminAction.lastResult, d.adminAction.pending === 'none' ? '' : 'warn');
  }
  renderPtz(d);

  if (d.snapshot) {
    const previewFps = d.ui && d.ui.previewMaximumFps ? d.ui.previewMaximumFps : 1;
    if (d.snapshot.healthy) {
      feedDeliveredFrames = Number(d.snapshot.served) || 0;
      feedHealthyMetaPrefix = 'Máximo ' + previewFps + ' FPS · perfil '
        + (d.snapshot.profile || 'ONVIF') + ' · último teste com '
        + formatBytes(d.snapshot.bytesRead);
      updateFeedHealthyMeta();
    } else {
      feedHealthyMetaPrefix = '';
      $('feedMeta').textContent = d.recovery && d.recovery.graceActive
        ? 'Máximo ' + previewFps + ' FPS · a câmera está reiniciando; a prévia será retomada automaticamente'
        : !d.snapshot.conclusive
          ? 'Máximo ' + previewFps + ' FPS · teste JPEG aguardando nova amostra conclusiva'
        : 'Máximo ' + previewFps + ' FPS · imagem indisponível · '
          + (d.snapshot.detail || 'aguardando novo teste');
    }
  }

  if (activeView === 'feed' && !feedLoading && !feedTimer) scheduleFeed(0);

  const c = d.counters;
  const conclusiveChecks = c.successfulChecks + c.failedChecks;
  const rate = conclusiveChecks ? c.successfulChecks / conclusiveChecks * 100 : 0;
  $('arc').style.setProperty('--p', conclusiveChecks ? rate : 0);
  $('availValue').textContent = conclusiveChecks ? rate.toFixed(rate >= 99.95 ? 0 : 1) + '%' : '—';

  const p = d.probes;
  setProbe('onvif', p.onvifTcp, p.onvifHttp, p.onvifStatus, p.onvifLatencyMs, 'HTTP');
  setRtspProbe(p);
  setImageProbe(d.snapshot, d.recovery);

  const rssi = d.wifi.rssi;
  const level = !d.wifi.connected ? 0 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
  const wifi = $('v-wifi');
  wifi.className = 'card-v ' + (level >= 3 ? 'ok' : level === 2 ? 'warn' : 'bad');
  [...$('bars').children].forEach((bar, i) => bar.classList.toggle('on', i < level));
  $('rssi').textContent = d.wifi.connected ? rssi + ' dBm' : 'desconectado';
  $('m-wifi').textContent = d.wifi.connected
    ? d.wifi.ssid + ' · ' + esp32Address + ' · gateway ' + d.wifi.gateway
    : 'tentando reconectar';
  pip('p-wifi', level >= 3 ? 'ok' : level === 2 ? 'warn' : 'bad');

  const currentSequence = c.consecutiveInconclusiveChecks
    ? plural(c.consecutiveInconclusiveChecks, 'teste inconclusivo seguido', 'testes inconclusivos seguidos')
    : c.consecutiveFailures
      ? plural(c.consecutiveFailures, 'falha consecutiva', 'falhas consecutivas')
      : plural(c.consecutiveSuccesses, 'sucesso seguido', 'sucessos seguidos');
  $('v-checks').textContent = c.totalChecks;
  $('m-checks').textContent = plural(c.failedChecks, 'falha no total', 'falhas no total') + ' · '
    + (c.inconclusiveChecks ? plural(c.inconclusiveChecks, 'inconclusiva', 'inconclusivas') + ' · ' : '')
    + currentSequence;

  $('l-event').textContent = localizeTimestampText(d.lastEvent);
  $('l-url').textContent = d.camera.onvifUrl || 'desconhecido';
  $('l-image').textContent = localizeTimestampText(
    d.snapshot.lastResult || d.snapshot.detail || 'ainda não testado'
  );
  $('l-reboot').textContent = localizeTimestampText(
    d.recovery.lastResult
      + (d.recovery.graceActive ? ' · aguardando ' + duration(d.recovery.graceRemainingSeconds) : '')
  );
  $('l-disc').textContent = localizeTimestampText(d.lastDiscovery);
  $('l-scan').textContent = localizeTimestampText(d.lastSubnetScan);
  const mailResult = $('l-mail');
  const mailTag = document.createElement('span');
  mailTag.className = 'tag ' + (d.resendConfigured ? 'ok' : 'bad');
  mailTag.textContent = d.resendConfigured ? 'Resend ativo' : 'Resend não configurado';
  mailResult.replaceChildren(
    mailTag,
    document.createTextNode(
      ' ' + c.emailsSent + ' enviados, ' + c.emailErrors + ' erros — '
        + localizeTimestampText(d.lastEmailResult)
    )
  );

  $('raw').textContent = JSON.stringify(d, null, 2);
  $('copyDiagnostics').disabled = false;
  $('foot').textContent = 'ESP32 Camera Watchdog · firmware ' + d.firmwareVersion
    + ' · ligado há ' + duration(d.uptimeSeconds);
  renderResources(d);

  lastOk = Date.now();
}

function scheduleStatusRefresh(delay) {
  clearTimeout(statusRefreshTimer);
  statusRefreshTimer = null;
  if (
    document.hidden ||
    activeView === 'feed'
  ) return;
  statusRefreshTimer = setTimeout(refresh, delay == null ? statusRefreshIntervalMs : delay);
}

async function refresh(force) {
  clearTimeout(statusRefreshTimer);
  statusRefreshTimer = null;
  if (statusLoading) return;
  if (activeView === 'feed' && lastOk && !force) return;
  if (activeView === 'feed' && feedLoading) {
    scheduleStatusRefresh(250);
    return;
  }
  statusLoading = true;
  const requestGeneration = ++statusRequestGeneration;
  const abortController = typeof AbortController === 'function'
    ? new AbortController()
    : null;
  statusAbortController = abortController;
  let requestTimeout;
  try {
    const timeoutMs = Math.max(5000, Math.min(30000, statusRefreshIntervalMs * 2));
    const payload = await Promise.race([
      (async () => {
        const response = await fetch('/api/status', {
          cache: 'no-store',
          signal: abortController ? abortController.signal : undefined
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        return response.json();
      })(),
      new Promise((resolve, reject) => {
        requestTimeout = setTimeout(() => {
          if (abortController) abortController.abort();
          reject(new Error('status_timeout'));
        }, timeoutMs);
      })
    ]);
    if (requestGeneration !== statusRequestGeneration) return;
    render(payload);
    $('deviceStatus').classList.remove('stale');
  } catch (error) {
    if (requestGeneration === statusRequestGeneration && !document.hidden) {
      $('deviceStatus').classList.add('stale');
      $('deviceStatusText').textContent = 'sem resposta do dispositivo';
    }
  } finally {
    clearTimeout(requestTimeout);
    if (requestGeneration !== statusRequestGeneration) return;
    statusAbortController = null;
    statusLoading = false;
    scheduleStatusRefresh(statusRefreshIntervalMs);
  }
}

setInterval(() => {
  if (!lastOk || $('deviceStatus').classList.contains('stale')) return;
  const ago = Math.round((Date.now() - lastOk) / 1000);
  $('deviceStatusText').textContent = ago < 2 ? 'atualizado agora' : 'atualizado há ' + ago + 's';
}, 1000);

function activateView(name, updateHash) {
  const nextView = ['actions', 'settings', 'diagnostics', 'logs', 'resources', 'feed'].includes(name)
    ? name
    : 'camera';
  activeView = nextView;
  document.querySelectorAll('[data-view-panel]').forEach(panel => {
    panel.hidden = panel.dataset.viewPanel !== nextView;
  });
  document.querySelectorAll('.tab[data-view]').forEach(tab => {
    const selected = tab.dataset.view === nextView;
    tab.setAttribute('aria-selected', selected ? 'true' : 'false');
    tab.tabIndex = selected ? 0 : -1;
  });
  const selectedTab = document.querySelector('.tab[aria-selected="true"]');
  document.title = selectedTab.textContent + ' · Camera Watchdog';

  if (nextView === 'settings' || nextView === 'actions') {
    loadSettings(false);
  }

  if (nextView === 'logs') {
    loadPersistentLogs(true);
  }

  if (nextView === 'feed') {
    feedPausedForPtz = false;
    feedActivationPending = true;
    $('feedState').textContent = cameraRecovering
      ? 'A câmera está reiniciando…'
      : cameraKnown ? 'Atualizando a imagem…' : 'Aguardando a câmera ser localizada…';
    $('feedState').classList.remove('hidden');
    $('feedStamp').textContent = cameraRecovering
      ? 'recuperação automática em andamento'
      : 'aguardando imagem';
    loadSettings(false)
      .then(() => refresh(true))
      .finally(() => {
        feedActivationPending = false;
        if (activeView === 'feed') scheduleFeed(0);
      });
  } else {
    feedPausedForPtz = false;
    feedActivationPending = false;
    $('ptzPresetModal').hidden = true;
    if (isNativeFullscreen()) (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    if (feedStage.classList.contains('fs-fallback')) exitFallbackFullscreen();
    clearTimeout(feedTimer);
    feedTimer = null;
    cancelFeedLoad(true);
    const frame = $('cameraFeed');
    frame.classList.remove('ready');
    scheduleStatusRefresh(0);
  }

  const nextHash = '#' + nextView;
  if (updateHash && location.hash !== nextHash) location.hash = nextHash;
  if (updateHash) window.scrollTo(0, 0);
}

document.querySelectorAll('.tab[data-view]').forEach(tab => {
  tab.addEventListener('click', () => activateView(tab.dataset.view, true));
});

$('skipToContent').addEventListener('click', () => $('mainContent').focus());

let diagnosticCopyTimer;
$('copyDiagnostics').addEventListener('click', async () => {
  const button = $('copyDiagnostics');
  if (button.getAttribute('aria-busy') === 'true') return;
  const status = $('diagnosticCopyStatus');
  clearTimeout(diagnosticCopyTimer);
  button.setAttribute('aria-busy', 'true');
  status.className = '';
  status.textContent = 'Copiando…';
  try {
    if (window.isSecureContext && navigator.clipboard) {
      await navigator.clipboard.writeText($('raw').textContent);
    } else {
      // O acesso HTTP local do ESP32 nao disponibiliza a Clipboard API.
      const selection = window.getSelection();
      const previousRanges = [];
      for (let i = 0; i < selection.rangeCount; i++) {
        previousRanges.push(selection.getRangeAt(i).cloneRange());
      }
      const range = document.createRange();
      range.selectNodeContents($('raw'));
      try {
        selection.removeAllRanges();
        selection.addRange(range);
        if (!document.execCommand('copy')) throw new Error('copy_failed');
      } finally {
        selection.removeAllRanges();
        previousRanges.forEach(previousRange => selection.addRange(previousRange));
      }
    }
    status.className = 'ok';
    status.textContent = 'JSON copiado';
  } catch (error) {
    status.className = 'bad';
    status.textContent = 'Falha ao copiar';
  } finally {
    button.removeAttribute('aria-busy');
    diagnosticCopyTimer = setTimeout(() => {
      status.className = '';
      status.textContent = 'Copiar JSON';
    }, 3000);
  }
});

document.querySelectorAll('[role="tablist"]').forEach(tablist => {
  const tabs = [...tablist.querySelectorAll('[role="tab"]')];
  const settingsTabs = tablist.classList.contains('settings-tablist');
  const compact = window.matchMedia('(max-width:680px)');
  const updateOrientation = () => {
    tablist.setAttribute('aria-orientation', settingsTabs && !compact.matches ? 'vertical' : 'horizontal');
  };
  updateOrientation();
  compact.addEventListener('change', updateOrientation);
  tabs.forEach((tab, index) => {
    tab.addEventListener('keydown', event => {
      const previousKey = settingsTabs && !compact.matches ? 'ArrowUp' : 'ArrowLeft';
      const nextKey = settingsTabs && !compact.matches ? 'ArrowDown' : 'ArrowRight';
      if (![previousKey, nextKey, 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      let nextIndex = index;
      if (event.key === previousKey) nextIndex = (index - 1 + tabs.length) % tabs.length;
      if (event.key === nextKey) nextIndex = (index + 1) % tabs.length;
      if (event.key === 'Home') nextIndex = 0;
      if (event.key === 'End') nextIndex = tabs.length - 1;
      tabs[nextIndex].focus();
      tabs[nextIndex].click();
    });
  });
});

window.addEventListener('hashchange', () => {
  const nextView = viewFromHash();
  if (nextView !== activeView) activateView(nextView, false);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearTimeout(feedTimer);
    clearTimeout(statusRefreshTimer);
    feedTimer = null;
    cancelFeedLoad(false);
    if (activeView === 'feed') {
      $('cameraFeed').classList.remove('ready');
      $('feedState').classList.remove('hidden');
      $('feedState').textContent = 'Prévia pausada enquanto a aba está oculta.';
      $('feedStamp').textContent = 'aguardando retorno à aba';
    }
    statusRequestGeneration++;
    if (statusAbortController) statusAbortController.abort();
    statusAbortController = null;
    statusLoading = false;
    return;
  }

  refresh();
  if (activeView === 'feed') scheduleFeed(0);
  if (activeView === 'settings' || activeView === 'actions') loadSettings(true);
  if (activeView === 'logs') loadPersistentLogs(true);
});

activateSettingsSection(activeSettingsSection);
activateView(activeView, false);
if (activeView !== 'feed') refresh();
</script>
</body>
</html>
)HTML";

}
