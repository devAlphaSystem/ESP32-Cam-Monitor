#include <Arduino.h>

namespace CameraWatchdog {

extern const char DASHBOARD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#0b0f14">
<link rel="icon" type="image/svg+xml" sizes="any" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='14' fill='%23141b25'/%3E%3Cpath d='M14 24h9l4-5h12l4 5h7a5 5 0 0 1 5 5v18a5 5 0 0 1-5 5H14a5 5 0 0 1-5-5V29a5 5 0 0 1 5-5Z' fill='none' stroke='%2338bdf8' stroke-width='4' stroke-linejoin='round'/%3E%3Ccircle cx='32' cy='38' r='9' fill='none' stroke='%23e9eff7' stroke-width='4'/%3E%3Ccircle cx='50' cy='18' r='6' fill='%2334d399' stroke='%230b0f14' stroke-width='3'/%3E%3C/svg%3E">
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
.status-pill{display:flex;align-items:center;gap:8px;font-size:12.5px;color:var(--muted);
background:var(--surface);border:1px solid var(--line);border-radius:999px;padding:7px 14px;white-space:nowrap}
.beat{width:7px;height:7px;border-radius:50%;background:var(--ok);flex:none;animation:beat 2.4s infinite}
@keyframes beat{0%{box-shadow:0 0 0 0 rgba(52,211,153,.5)}70%{box-shadow:0 0 0 7px rgba(52,211,153,0)}100%{box-shadow:0 0 0 0 rgba(52,211,153,0)}}
.status-pill.stale{color:var(--bad);border-color:rgba(248,113,113,.35)}
.status-pill.stale .beat{background:var(--bad);animation:none}

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
.hole{position:absolute;inset:10px;border-radius:50%;background:var(--surface);display:grid;place-items:center;line-height:1.15;overflow:hidden}
.hole>div{width:100%;min-width:0;padding:0 4px;box-sizing:border-box;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;text-align:center}
.hole b{display:block;font-size:22px;font-weight:680;letter-spacing:-.02em;line-height:1}
.hole span{display:block;max-width:100%;font-size:10.5px;color:var(--dim);letter-spacing:.05em;line-height:1.2;text-transform:uppercase}

/* previa da camera */
.viewer{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);
box-shadow:var(--shadow);overflow:hidden}
.viewer-h{padding:15px 17px;border-bottom:1px solid var(--line);display:flex;flex-wrap:wrap;
align-items:flex-start;justify-content:space-between;gap:12px 16px}
.viewer-copy{min-width:0;flex:1 1 300px}
.viewer-h h2{font-size:14px;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.viewer-h p{margin:3px 0 0;font-size:12.5px;color:var(--dim)}
.viewer-tools{display:flex;align-items:center;gap:9px}
.ptz-open-btn{display:inline-flex;align-items:center;gap:9px;white-space:nowrap}
.ptz-count{display:inline-grid;min-width:23px;height:20px;place-items:center;padding:0 6px;border-radius:999px;
background:rgba(255,255,255,.09);color:var(--text);font:11px/1 var(--mono)}
.viewer-h .form-result{margin:0;flex-basis:100%}
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

/* configuracoes e acoes */
.settings-kpi{min-width:170px;padding:13px 16px;border:1px solid var(--line);border-radius:11px;
background:rgba(255,255,255,.025);text-align:right}
.settings-kpi span{display:block;color:var(--dim);font-size:10.5px;letter-spacing:.07em;text-transform:uppercase}
.settings-kpi b{display:block;margin-top:4px;color:var(--info);font-size:21px;letter-spacing:-.02em}
.settings-tabbar{margin-top:14px;padding:4px;background:var(--surface);border:1px solid var(--line);
border-radius:12px;box-shadow:var(--shadow);overflow:hidden}
.settings-tablist{display:flex;gap:4px;overflow-x:auto;overscroll-behavior-inline:contain;scrollbar-width:thin}
.settings-tab{flex:1 1 0;min-height:39px;padding:8px 14px;border:0;border-radius:8px;background:transparent;
color:var(--muted);cursor:pointer;font:inherit;font-size:12.5px;font-weight:650;white-space:nowrap;text-align:center;transition:.16s ease}
.settings-tab:hover{color:var(--text);background:var(--surface2)}
.settings-tab[aria-selected="true"]{color:var(--text);background:var(--line2);box-shadow:inset 0 0 0 1px rgba(96,165,250,.14)}
.settings-tab:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.settings-panels{margin-top:12px}
.settings-panel[hidden]{display:none!important}
.settings-card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);padding:20px}
.settings-card-h{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:17px;
padding-bottom:14px;border-bottom:1px solid var(--line)}
.settings-card-h h2{font-size:16px;color:var(--text)}
.settings-card-h p{margin:3px 0 0;color:var(--dim);font-size:12.5px}
.form-grid{display:grid;grid-template-columns:minmax(0,1fr);gap:15px}
.field{min-width:0}.field.wide{grid-column:auto}
.field label{display:flex;align-items:baseline;justify-content:space-between;gap:8px;margin-bottom:6px;
color:var(--muted);font-size:12.5px;font-weight:620}
.field label span{color:var(--dim);font-size:10.5px;font-weight:500;white-space:nowrap}
.field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select{width:100%;height:42px;
border:1px solid var(--line2);border-radius:9px;background:#0e141d;color:var(--text);padding:0 11px;
font:13px/1.2 var(--mono);outline:none;transition:.16s ease}
.field input:focus,.field select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(56,189,248,.11)}
.field input:disabled{opacity:.55;cursor:not-allowed}
.field small{display:block;margin-top:5px;color:var(--dim);font-size:10.8px;line-height:1.45}
.checkline{display:flex;align-items:flex-start;gap:10px;padding:12px;border:1px solid var(--line);
border-radius:9px;background:rgba(255,255,255,.018)}
.checkline input{width:17px;height:17px;margin:2px 0 0;accent-color:var(--ok);flex:none}
.checkline label{display:flex;flex-direction:column;align-items:flex-start;gap:2px;margin:0;color:var(--muted);font-size:12.5px;cursor:pointer}
.checkline label b{display:block;color:var(--text);font-size:12.5px;font-weight:620}
.checkline label span{display:block;margin-top:0;color:var(--dim);font-size:10.8px;line-height:1.4;white-space:normal}
.estimate-box{padding:12px 13px;border:1px solid rgba(96,165,250,.24);border-radius:9px;
background:rgba(96,165,250,.065);color:var(--muted);font-size:12px}
.estimate-box b{color:var(--info)}
.settings-savebar{display:flex;flex-direction:column;align-items:flex-end;gap:10px;margin-top:14px;
background:var(--surface);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);padding:16px 18px}
.settings-savebar .form-result{text-align:right}
.button-row{display:flex;flex-wrap:wrap;gap:9px;align-items:center}
.btn{min-height:39px;border:1px solid var(--line2);border-radius:9px;background:var(--surface2);color:var(--text);
padding:8px 14px;cursor:pointer;font:inherit;font-size:12.5px;font-weight:650;transition:.16s ease}
.btn:hover{border-color:#456078;background:#202b3a}.btn:disabled{opacity:.48;cursor:wait}
.btn.primary{background:#166c91;border-color:#2388b3;color:#fff}.btn.primary:hover{background:#197da7}
.btn.warn{border-color:rgba(251,191,36,.35);color:var(--warn);background:rgba(251,191,36,.07)}
.btn.bad{border-color:rgba(248,113,113,.35);color:var(--bad);background:rgba(248,113,113,.07)}
.form-result{flex:1 1 100%;min-height:20px;color:var(--dim);font-size:12px}
.form-result.ok{color:var(--ok)}.form-result.warn{color:var(--warn)}.form-result.bad{color:var(--bad)}
.actions-card{margin-top:0}
.actions-card .form-result{margin-top:18px}
.action-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:11px}
.action-item{display:flex;flex-direction:column;align-items:flex-start;gap:8px;min-height:150px;padding:15px;
border:1px solid var(--line);border-radius:10px;background:rgba(255,255,255,.018)}
.action-item b{font-size:13px}.action-item span{color:var(--dim);font-size:10.8px;line-height:1.45;min-height:32px}
.action-item .btn{width:100%;margin-top:auto}

/* logs persistentes */
.logs-hero-tools{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:9px}
.logs-hero-tools .btn{text-decoration:none;display:inline-flex;align-items:center;justify-content:center}
.logs-card{margin-top:14px;background:var(--surface);border:1px solid var(--line);border-radius:var(--r);
box-shadow:var(--shadow);overflow:hidden}
.logs-toolbar{display:grid;grid-template-columns:minmax(180px,1fr) 150px 150px 180px;gap:10px;padding:14px 16px;
border-bottom:1px solid var(--line)}
.logs-toolbar .field label{margin-bottom:5px}
.logs-table-wrap{overflow:auto;max-height:620px;overscroll-behavior:contain}
.logs-table{width:100%;border-collapse:collapse;min-width:960px;font-size:12px}
.logs-table th{position:sticky;top:0;z-index:1;padding:10px 12px;background:#111821;color:var(--dim);
text-align:left;font-size:10.5px;letter-spacing:.06em;text-transform:uppercase;border-bottom:1px solid var(--line)}
.logs-table td{padding:10px 12px;border-top:1px solid var(--line);color:var(--muted);vertical-align:top}
.logs-table tbody tr:first-child td{border-top:0}
.logs-table tbody tr:hover td{background:rgba(255,255,255,.018)}
.logs-table .mono{font-size:11.5px;white-space:nowrap}
.logs-level{display:inline-block;padding:2px 7px;border-radius:999px;font-size:10px;font-weight:700;text-transform:uppercase}
.logs-level.info{color:var(--info);background:rgba(96,165,250,.12)}
.logs-level.warning{color:var(--warn);background:rgba(251,191,36,.12)}
.logs-level.error{color:var(--bad);background:rgba(248,113,113,.12)}
.logs-event{color:var(--text);font-family:var(--mono);font-size:11.5px;white-space:nowrap}
.logs-detail{max-width:300px;color:var(--muted);overflow-wrap:anywhere}
.logs-flags{display:block;margin-top:3px;color:var(--dim);font-size:10px}
.logs-empty{padding:32px 16px;text-align:center;color:var(--dim);font-size:12.5px}
.logs-foot{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;padding:13px 16px;
border-top:1px solid var(--line)}
.logs-foot .form-result{flex:1 1 260px}

/* modal de confirmacao */
.modal-overlay{position:fixed;inset:0;z-index:50;display:grid;place-items:center;padding:16px;
background:rgba(5,7,10,.72);backdrop-filter:blur(2px)}
.modal-box{width:100%;max-width:380px;background:var(--surface);border:1px solid var(--line2);
border-radius:var(--r);box-shadow:var(--shadow);padding:20px}
.modal-box h3{margin:0;font-size:16px;letter-spacing:-.01em}
.modal-desc{margin:8px 0 16px;color:var(--muted);font-size:12.5px;line-height:1.5}
.modal-actions{justify-content:flex-end;margin-top:16px}
.ptz-modal-box{max-width:540px;max-height:calc(100vh - 32px);display:flex;flex-direction:column;overflow:hidden}
.ptz-modal-head{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;flex:none}
.ptz-modal-head .btn{min-height:34px;padding:5px 11px}
.ptz-filter{flex:none;margin-bottom:12px}
.ptz-list{min-height:130px;max-height:min(52vh,480px);overflow-y:auto;overscroll-behavior:contain;
border:1px solid var(--line);border-radius:10px;background:#0e141d;scrollbar-width:thin}
.ptz-list-item{display:flex;align-items:center;gap:12px;padding:11px 12px;border-top:1px solid var(--line)}
.ptz-list-item:first-child{border-top:0}
.ptz-list-copy{min-width:0;flex:1}
.ptz-list-copy b{display:block;font-size:13px;overflow-wrap:anywhere}
.ptz-list-copy span{display:block;margin-top:2px;color:var(--dim);font:10.8px/1.4 var(--mono);overflow-wrap:anywhere}
.ptz-list-item .btn{min-width:68px;flex:none}
.ptz-empty{padding:26px 18px;text-align:center;color:var(--dim);font-size:12.5px;line-height:1.55}
.ptz-modal-foot{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:11px;
color:var(--dim);font-size:11.5px;flex:none}

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
.action-grid{grid-template-columns:repeat(2,minmax(0,1fr))}
}
@media(max-width:560px){
main{padding:14px 12px 36px}
.top-tools{width:100%;justify-content:space-between;min-width:0}
.tabs{flex:1;min-width:0;overflow-x:auto;scrollbar-width:none}.tabs::-webkit-scrollbar{display:none}
.tab{flex:0 0 auto}
.hero{padding:18px;gap:16px}
.hero-name{font-size:23px}
.gauge{width:96px;height:96px;margin:0}
.grid{grid-template-columns:1fr;gap:10px}
.grid.grid-3{grid-template-columns:1fr}
.form-grid,.action-grid{grid-template-columns:1fr}
.settings-card{padding:16px}
.settings-savebar{align-items:stretch}
.settings-savebar .form-result{text-align:left}
.button-row{width:100%}.button-row .btn{flex:1 1 auto}
.settings-kpi{width:100%;text-align:left}
.settings-tab{padding-inline:12px}
.logs-toolbar{grid-template-columns:1fr}.logs-hero-tools{width:100%;justify-content:stretch}
.logs-hero-tools .btn{flex:1 1 auto}.logs-table-wrap{max-height:68vh}
.viewer-stage{min-height:0}
.viewer-tools{width:100%}.ptz-open-btn{width:100%;justify-content:center}
.modal-overlay{padding:8px}.ptz-modal-box{max-height:calc(100vh - 16px);padding:16px}
.ptz-list{max-height:54vh}.ptz-list-item{padding:10px}.ptz-list-item .btn{min-width:60px;padding-inline:10px}
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
      <button class="tab" id="logsTab" type="button" role="tab" data-view="logs" aria-controls="logsView" aria-selected="false">Logs</button>
      <button class="tab" id="settingsTab" type="button" role="tab" data-view="settings" aria-controls="settingsView" aria-selected="false">Configurações</button>
      <button class="tab" id="actionsTab" type="button" role="tab" data-view="actions" aria-controls="actionsView" aria-selected="false">Ações</button>
    </nav>
    <div class="status-pill" id="deviceStatus"><span class="beat"></span><span id="deviceStatusText">conectando…</span></div>
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
      <div class="hole"><div><b id="availValue">—</b><span>conclusivos OK</span></div></div>
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
      <div class="viewer-copy">
        <h2 id="viewerTitle">Prévia de diagnóstico</h2>
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
        <div class="hole"><div><b id="resourcesHeapPercent">—</b><span>heap livre</span></div></div>
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

<div id="logsView" role="tabpanel" aria-labelledby="logsTab" data-view-panel="logs" hidden>
  <section class="hero" style="--sc:var(--info);--sbg:rgba(96,165,250,.13);--sbd:rgba(96,165,250,.28)">
    <div class="hero-main">
      <span class="badge"><i></i><span id="logsState">carregando histórico</span></span>
      <h2 class="hero-name">Histórico persistente</h2>
      <div class="hero-sub">Conexões com a câmera e operações importantes, preservadas entre reinicializações.</div>
    </div>
    <div class="logs-hero-tools">
      <button class="btn" id="logsRefresh" type="button">Atualizar</button>
      <a class="btn primary" id="logsExport" href="/api/logs/export" download="camera-watchdog-logs.csv">Exportar CSV completo</a>
    </div>
  </section>

  <div class="grid grid-3">
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

  <form id="settingsForm" autocomplete="off" novalidate>
    <div class="settings-tabbar">
      <nav class="settings-tablist" role="tablist" aria-label="Categorias das configurações">
        <button class="settings-tab" id="settingsIdentificationTab" type="button" role="tab" data-settings-section="identification" aria-controls="settingsIdentificationPanel" aria-selected="true" tabindex="0">Identificação</button>
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
          <div class="field"><label for="cfgLocalHostname">Endereço local <span>1–31 caracteres</span></label><input id="cfgLocalHostname" type="text" minlength="1" maxlength="31" pattern="[A-Za-z0-9](?:[A-Za-z0-9-]{0,29}[A-Za-z0-9])?" placeholder="camera-watchdog" required><small>Use letras, números e hífens. O acesso será feito por http://nome-escolhido.local/.</small></div>
          <div class="field"><label for="cfgSnapshotProfileMatch">Perfil ONVIF preferido <span>até 32 caracteres</span></label><input id="cfgSnapshotProfileMatch" type="text" maxlength="32" placeholder="snap"><small>O firmware prioriza um perfil JPEG compatível; padrão: “snap”.</small></div>
          <div class="field"><label for="cfgCameraRtspPort">Porta RTSP <span>1–65535</span></label><input id="cfgCameraRtspPort" type="number" min="1" max="65535" step="1" required><small>Padrão: 554.</small></div>
          <div class="field"><label for="cfgFeedMinimum">Intervalo mínimo da prévia <span>segundos</span></label><input id="cfgFeedMinimum" type="number" min="0.25" max="10" step="0.05" required><small>Mínimo permitido: 0,25 s; use 0,25 s com ciclo de 0,5 s para até 2 FPS.</small></div>
          <div class="field"><label for="cfgFeedTarget">Ciclo-alvo da prévia <span>segundos</span></label><input id="cfgFeedTarget" type="number" min="0.5" max="30" step="0.1" required><small>Padrão estável: 5 s; mínimo permitido: 0,5 s, limitando a prévia a 2 FPS.</small></div>
          <div class="field"><label for="cfgStatusRefresh">Atualização do painel <span>segundos</span></label><input id="cfgStatusRefresh" type="number" min="1" max="30" step="0.5" required><small>Padrão: 3 s.</small></div>
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

<div id="actionsView" role="tabpanel" aria-labelledby="actionsTab" data-view-panel="actions" hidden>
  <section class="settings-card actions-card">
    <div class="settings-card-h"><div><h2>Controle manual</h2><p>Ações sensíveis exigem confirmação e respeitam os limites de segurança do firmware.</p></div></div>
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
  online:    ['Online',      'var(--ok)',   'rgba(52,211,153,.13)', 'rgba(52,211,153,.28)'],
  degraded:  ['Instável',    'var(--warn)', 'rgba(251,191,36,.13)', 'rgba(251,191,36,.28)'],
  recovering:['Recuperando', 'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)'],
  offline:   ['Offline',     'var(--bad)',  'rgba(248,113,113,.13)','rgba(248,113,113,.28)'],
  discovering:['Procurando', 'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)'],
  starting:  ['Iniciando',   'var(--info)', 'rgba(96,165,250,.13)', 'rgba(96,165,250,.28)']
};
const HISTORY_LIMIT = 60;
const resourceHistory = { heap: [], rssi: [] };
function viewFromHash() {
  if (location.hash === '#actions') return 'actions';
  if (location.hash === '#settings') return 'settings';
  if (location.hash === '#logs') return 'logs';
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

function activateSettingsSection(name, focusTab) {
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
    if (selected && focusTab) tab.focus();
  });
}

document.querySelectorAll('.settings-tab[data-settings-section]').forEach((tab, index, tabs) => {
  tab.addEventListener('click', () => activateSettingsSection(tab.dataset.settingsSection, false));
  tab.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    let nextIndex = index;
    if (event.key === 'ArrowLeft') nextIndex = (index - 1 + tabs.length) % tabs.length;
    if (event.key === 'ArrowRight') nextIndex = (index + 1) % tabs.length;
    if (event.key === 'Home') nextIndex = 0;
    if (event.key === 'End') nextIndex = tabs.length - 1;
    activateSettingsSection(tabs[nextIndex].dataset.settingsSection, true);
  });
});

function validateSettingsForm() {
  const invalid = $('settingsForm').querySelector(':invalid');
  if (!invalid) return true;
  const panel = invalid.closest('[data-settings-section-panel]');
  if (panel) activateSettingsSection(panel.dataset.settingsSectionPanel, false);
  invalid.reportValidity();
  return false;
}

const SETTINGS_FIELDS = {
  cameraName: { id: 'cfgCameraName', kind: 'text' },
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
        params.set(key, input.value.trim());
      } else {
        const numeric = Number(input.value);
        params.set(key, String(Math.round(numeric * (descriptor.scale || 1))));
      }
    });
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
  openConfirmModal('Restaurar todos os valores ajustáveis para os padrões de fábrica.', password => resetSettings(password));
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
  const nextView = ['actions', 'settings', 'logs', 'resources', 'feed'].includes(name)
    ? name
    : 'camera';
  activeView = nextView;
  document.querySelectorAll('[data-view-panel]').forEach(panel => {
    panel.hidden = panel.dataset.viewPanel !== nextView;
  });
  document.querySelectorAll('.tab[data-view]').forEach(tab => {
    tab.setAttribute('aria-selected', tab.dataset.view === nextView ? 'true' : 'false');
  });
  const viewTitle = nextView === 'actions'
    ? 'Ações'
    : nextView === 'settings'
      ? 'Configurações'
      : nextView === 'logs'
        ? 'Logs'
      : nextView === 'resources'
        ? 'Recursos'
        : nextView === 'feed' ? 'Prévia' : 'Câmera';
  document.title = viewTitle + ' · Camera Watchdog';

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
}

document.querySelectorAll('.tab[data-view]').forEach(tab => {
  tab.addEventListener('click', () => activateView(tab.dataset.view, true));
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

activateSettingsSection(activeSettingsSection, false);
activateView(activeView, false);
if (activeView !== 'feed') refresh();
</script>
</body>
</html>
)HTML";

}
