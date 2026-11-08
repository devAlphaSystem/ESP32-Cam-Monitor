#include <Arduino.h>

namespace CameraWatchdog {

extern const char DASHBOARD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#070708">
<link rel="icon" type="image/svg+xml" sizes="any" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='16' fill='%23242a31'/%3E%3Cpath d='m32 12 19 8v13c0 8-10 15-19 19-9-4-19-11-19-19V20Z' fill='none' stroke='%23e9edf2' stroke-width='3' stroke-linejoin='round'/%3E%3Ccircle cx='32' cy='29' r='7' fill='none' stroke='%23e9edf2' stroke-width='3'/%3E%3Cpath d='M32 36v7' stroke='%23e9edf2' stroke-width='3'/%3E%3C/svg%3E">
<title>Camera Watchdog</title>
<style>
*,*::before,*::after{box-sizing:border-box}
:root{
  --bg:#0b0b0c;--header:#070708;--sidebar:#141415;--surface:#161617;--surface-alt:#1d1d1f;--surface-hover:#252527;
  --line:#2a2a2d;--line-strong:#6b6b73;--text:#ececee;--muted:#a4a4ab;--dim:#8b8b93;
  --ok:#80b99a;--warn:#d4b078;--bad:#dc949b;--info:#91b4d5;--accent:#4da3ff;
  --button-primary:#1f66c7;--button-primary-hover:#2a74d6;--on-primary:#ffffff;
  --stage:#050506;--radius:8px;--header-h:56px;--control-h:36px;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  --sans:"Segoe UI",system-ui,-apple-system,BlinkMacSystemFont,sans-serif;
}
html{-webkit-text-size-adjust:100%;color-scheme:dark;scrollbar-color:var(--line-strong) var(--bg)}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 var(--sans);-webkit-font-smoothing:antialiased}
::selection{background:#4da3ff4d;color:var(--text)}
button,input,select{font:inherit}
input,select{caret-color:var(--accent)}
button,a,input,select{-webkit-tap-highlight-color:transparent}
button,a{touch-action:manipulation}
h1,h2,h3,p{margin:0}
h1{font-size:20px;font-weight:600;line-height:1.3;letter-spacing:-.02em}
h2,h3{font-size:14px;font-weight:600;line-height:1.4}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
.skip-link{position:fixed;left:12px;top:-80px;z-index:1001;padding:12px 16px;border:1px solid var(--accent);border-radius:6px;background:var(--surface);color:var(--text)}
.skip-link:focus{top:12px}

/* Shell: header, camera sidebar and content */
.app-header{position:sticky;top:0;z-index:50;display:flex;align-items:center;gap:28px;height:var(--header-h);padding:0 20px 0 18px;border-bottom:1px solid var(--line);background:var(--header)}
.brand{display:flex;align-items:center;flex:none;color:var(--text);font-size:15px;font-weight:600;letter-spacing:-.01em;text-decoration:none}
.tabs{display:flex;align-items:center;gap:4px;min-width:0;overflow-x:auto;scrollbar-width:none}
.tab,.settings-tab{flex:none;min-height:var(--control-h);padding:0 12px;border:0;border-radius:6px;background:transparent;color:var(--muted);font-size:14px;font-weight:500;white-space:nowrap;cursor:pointer;transition:background-color .15s,color .15s}
.tab:hover,.settings-tab:hover:not(:disabled){color:var(--text);background:var(--surface)}
.tab[aria-selected="true"],.settings-tab[aria-selected="true"]{color:var(--text);background:var(--surface-alt);font-weight:600}
.tab:focus-visible,.settings-tab:focus-visible{outline-offset:-2px}
.settings-tab:disabled{opacity:.45;cursor:not-allowed}
.status-pill{display:flex;align-items:center;gap:8px;flex:none;padding:5px 12px;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:12px;white-space:nowrap}
.header-status{display:flex;align-items:center;gap:8px;flex:none;margin-left:auto}
.beat{width:6px;height:6px;background:var(--ok);border-radius:50%;flex:none}
.status-pill.stale{color:var(--bad)}.status-pill.stale .beat{background:var(--bad)}
.app-shell{display:grid;grid-template-columns:232px minmax(0,1fr);min-height:calc(100vh - var(--header-h))}
.app-sidebar{position:sticky;top:var(--header-h);display:flex;flex-direction:column;height:calc(100vh - var(--header-h));min-width:0;border-right:1px solid var(--line);background:var(--sidebar)}
.sidebar-nav{display:flex;flex-direction:column;flex:1;min-height:0;padding:16px 10px 10px}
.sidebar-h{display:flex;align-items:baseline;justify-content:space-between;gap:8px;padding:0 8px 10px}
.sidebar-h h2{color:var(--muted);font-size:13px;font-weight:600}
.sidebar-h span{color:var(--dim);font-size:12px;font-variant-numeric:tabular-nums}
.cam-list{display:flex;flex-direction:column;gap:2px;min-height:0;margin:0;padding:0;list-style:none;overflow-y:auto;overscroll-behavior:contain}
.cam-item{display:grid;grid-template-columns:8px minmax(0,1fr);align-items:center;column-gap:10px;width:100%;min-height:44px;padding:6px 8px;border:0;border-radius:6px;background:transparent;color:var(--text);text-align:left;cursor:pointer;transition:background-color .15s}
.cam-item:hover{background:var(--surface-alt)}
.cam-item[aria-current="true"]{background:var(--surface-hover)}
.cam-item[aria-current="true"] .cam-item-name{font-weight:600}
.cam-item:focus-visible{outline-offset:-2px}
.state-dot{width:8px;height:8px;border-radius:50%;background:var(--sc,var(--dim));flex:none}
.cam-item .state-dot{grid-row:1/3}
.cam-item-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px;line-height:1.35}
.cam-item-state{color:var(--muted);font-size:11px;line-height:1.35}
.cam-empty{padding:4px 8px;color:var(--dim);font-size:12px}
.app-sidebar footer{padding:12px 18px 16px;border-top:1px solid var(--line);color:var(--dim);font-size:11px;line-height:1.6}
main{min-width:0;padding:20px 24px 24px}
[data-view-panel]{min-width:0}
.overview-note{margin:14px 0 0;color:var(--muted);font-size:12px}
.overview-note a{color:var(--accent)}

/* Subheader band */
.device-header{display:flex;align-items:center;justify-content:space-between;gap:24px;min-height:64px;padding:12px 16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.device-copy{flex:1;min-width:0}
.device-title{display:flex;flex-wrap:wrap;align-items:center;gap:6px 12px}
.device-name{overflow-wrap:anywhere}
.device-meta{margin-top:2px;color:var(--muted);font-size:13px;line-height:1.6;overflow-wrap:anywhere}
.badge{display:inline-flex;align-items:center;gap:7px;padding:3px 8px;border:1px solid var(--sbd,var(--line));border-radius:4px;background:var(--sbg,var(--surface-alt));color:var(--sc,var(--muted));font-size:12px;line-height:1.4;white-space:nowrap}
.badge i{width:6px;height:6px;border-radius:50%;background:currentColor;flex:none}
.rate{width:200px;flex:none}
.rate-label{display:flex;align-items:center;justify-content:space-between;gap:16px;color:var(--muted);font-size:13px}
.rate-label b{font-size:20px;font-weight:600;color:var(--text);font-variant-numeric:tabular-nums}
.rate-track{height:4px;margin-top:6px;border-radius:2px;background:var(--line);overflow:hidden}
.rate-track::before{content:"";display:block;width:clamp(0%,calc(var(--p,0)*1%),100%);height:100%;background:var(--sc,var(--dim))}

/* Overview counters and camera grid */
.kpi-row{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:16px}
.kpi{display:flex;align-items:center;justify-content:space-between;gap:12px;min-width:0;padding:14px 16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.kpi-label{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:13px}
.kpi-value{font-size:24px;font-weight:600;line-height:1.2;font-variant-numeric:tabular-nums}
.camera-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:16px;margin:16px 0 0;padding:0;list-style:none}
.camera-card{position:relative;isolation:isolate;display:flex;flex-direction:column;min-width:0;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface);overflow:hidden;transition:border-color .15s}
.camera-card:hover{border-color:var(--line-strong)}
.camera-card-head{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:48px;padding:8px 16px}
.camera-card-head h2{min-width:0}
.camera-card-open{display:block;max-width:100%;padding:0;border:0;background:none;color:var(--text);font:inherit;text-align:left;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
.camera-card-open::after{content:"";position:absolute;inset:0;z-index:1;border-radius:var(--radius)}
.camera-card-open:focus-visible{outline:none}
.camera-card-open:focus-visible::after{outline:2px solid var(--accent);outline-offset:-2px}
.camera-card-media{position:relative;display:grid;place-items:center;aspect-ratio:16/9;background:var(--stage);overflow:hidden}
.camera-card-media img{grid-area:1/1;width:100%;height:100%;object-fit:contain;opacity:0;transition:opacity .2s}
.camera-card-media[data-preview="ready"] img,.camera-card-media[data-preview="refreshing"] img{opacity:1}
.card-overlay{position:absolute;inset:0;z-index:2;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;padding:16px;color:var(--muted);font-size:13px;text-align:center;pointer-events:none}
.card-overlay .btn{position:relative;pointer-events:auto}
.camera-card-media[data-preview="ready"] .card-overlay{display:none}
.camera-card-media[data-preview="refreshing"] .card-overlay{inset:8px 8px auto auto;flex-direction:row;gap:7px;padding:4px 8px;border-radius:4px;background:#000000b3;color:#e6e6e8;font-size:11px}
.camera-card-media[data-preview="placeholder"] .spinner,.camera-card-media[data-preview="failed"] .spinner{display:none}
.spinner{width:28px;height:28px;border:3px solid #ffffff26;border-top-color:var(--accent);border-radius:50%;animation:spin .9s linear infinite}
.camera-card-media[data-preview="refreshing"] .spinner{width:12px;height:12px;border-width:2px}
@keyframes spin{to{transform:rotate(360deg)}}
.card-stamp{position:absolute;z-index:2;left:8px;bottom:8px;padding:3px 7px;border-radius:4px;background:#000000b3;color:#e6e6e8;font-size:11px;font-variant-numeric:tabular-nums;pointer-events:none}
.card-stamp:empty,.camera-card-media[data-preview="placeholder"] .card-stamp,.camera-card-media[data-preview="failed"] .card-stamp{display:none}
.camera-card-foot{display:flex;flex-direction:column;gap:2px;padding:12px 16px;color:var(--muted);font-size:12px;line-height:1.5}
.camera-card-meta{display:flex;flex-wrap:wrap;gap:2px 12px}
.camera-card-meta .mono{color:var(--text);font-size:12px}
.check-stale{color:var(--warn)}
.camera-card-event{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--dim)}

/* Camera: stage, information cards and collapsed diagnostics */
.camera-layout{display:grid;grid-template-columns:minmax(0,1fr) minmax(280px,340px);grid-template-areas:"stage side";align-items:stretch;gap:16px;margin-top:16px}
.stage-card{grid-area:stage;min-width:0;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface);overflow:hidden}
.stage-bar{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;min-height:56px;padding:10px 16px}
.stage-bar h2{margin-right:auto}
.stage-card>.form-result{padding:8px 16px;border-top:1px solid var(--line)}
.camera-side{grid-area:side;display:grid;grid-template-rows:repeat(3,auto) minmax(0,1fr);gap:16px;min-width:0}
.card-text{margin-top:8px;font-size:13px;line-height:1.6;overflow-wrap:anywhere}
.health-list{margin:4px 0 0;padding:0;list-style:none}
.health-list li{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
.health-list li:first-child{border-top:0}
.health-name{display:flex;align-items:center;gap:8px;font-size:13px;font-weight:500}
.health-list .card-v{margin:0;font-size:13px;font-weight:500;text-align:right}
.pip{display:inline-block;width:6px;height:6px;border-radius:50%;background:var(--dim);flex:none}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}
.pip.ok{background:var(--ok)}.pip.warn{background:var(--warn)}.pip.bad{background:var(--bad)}.pip.info{background:var(--info)}
.dev-diagnostics{margin-top:16px}
.dev-diagnostics,.settings-advanced{border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.dev-diagnostics summary,.settings-advanced summary{display:flex;flex-wrap:wrap;align-items:center;gap:10px;min-height:48px;padding:10px 16px;color:var(--muted);font-size:14px;font-weight:600;list-style:none;cursor:pointer}
.dev-diagnostics summary::-webkit-details-marker,.settings-advanced summary::-webkit-details-marker{display:none}
.dev-diagnostics summary::before,.settings-advanced summary::before{content:"";width:6px;height:6px;border-right:1.5px solid currentColor;border-bottom:1.5px solid currentColor;transform:rotate(-45deg);transition:transform .15s}
.dev-diagnostics[open] summary::before,.settings-advanced[open] summary::before{transform:rotate(45deg)}
.dev-diagnostics summary:hover,.settings-advanced summary:hover{color:var(--text)}
.dev-diagnostics summary:focus-visible,.settings-advanced summary:focus-visible{outline-offset:-2px}
.dev-body{padding:16px;border-top:1px solid var(--line)}
.dev-list{margin:0}
.dev-list div{display:grid;grid-template-columns:160px minmax(0,1fr);gap:16px;padding:10px 0;border-bottom:1px solid var(--line)}
.dev-list div:first-child{padding-top:0}
.dev-list div:last-child{border-bottom:0;padding-bottom:0}
.dev-list dt{color:var(--muted);font-size:12px}
.dev-list dd{margin:0;font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.dev-link{margin-left:auto;padding:4px 0;color:var(--accent);font-size:12px;font-weight:500;text-decoration:none}
#l-url{font:12px/1.7 var(--mono)}
.tag{display:inline-block;padding:1px 6px;border-radius:4px;font-size:11px;font-weight:500}
.tag.ok{background:#18261e;color:var(--ok)}.tag.bad{background:#2a1c1f;color:var(--bad)}

/* Cards, resources and history */
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));grid-auto-rows:1fr;gap:16px;margin-top:16px}
.card{min-width:0;padding:16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
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
.chart-card{margin-top:16px;padding:16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.chart-head{display:flex;flex-wrap:wrap;align-items:flex-start;justify-content:space-between;gap:16px}
.chart-head p{margin-top:4px;color:var(--dim);font-size:12px}
.legend{display:flex;flex-wrap:wrap;gap:18px;color:var(--muted);font-size:12px}
.legend span{display:flex;align-items:center;gap:7px}.legend b{color:var(--text);font-weight:500;font-variant-numeric:tabular-nums}
.legend i{width:12px;height:3px;background:var(--info)}.legend .wifi-dot{background:var(--warn)}
.chart{display:block;width:100%;height:210px;margin-top:16px}
.chart line{stroke:var(--line);stroke-width:1;vector-effect:non-scaling-stroke}
.chart polyline{fill:none;stroke:var(--info);stroke-width:2;stroke-linecap:round;stroke-linejoin:round;vector-effect:non-scaling-stroke}
.chart .wifi-line{stroke:var(--warn);stroke-dasharray:5 3}

/* Video stage */
.segmented{display:inline-flex;padding:2px;border:1px solid var(--line);border-radius:6px;background:var(--bg)}
.segmented button{min-height:30px;padding:0 12px;border:0;border-radius:4px;background:transparent;color:var(--muted);font:500 13px/1 var(--sans);cursor:pointer;transition:background-color .15s,color .15s}
.segmented button:hover{color:var(--text)}
.segmented button[aria-pressed="true"]{background:var(--surface-hover);color:var(--text)}
.segmented button:focus-visible{outline-offset:-2px}
.btn.ptz-open-btn{gap:8px;padding-block:0;white-space:nowrap}
.ptz-count{display:inline-grid;min-width:21px;height:20px;place-items:center;padding:0 5px;border-radius:4px;background:#ffffff1f;font-size:11px;font-variant-numeric:tabular-nums}
.viewer-stage{position:relative;display:grid;place-items:center;aspect-ratio:16/9;background:var(--stage);overflow:hidden}
.viewer-stage img{position:relative;z-index:1;grid-area:1/1;width:100%;height:100%;object-fit:contain;opacity:0}
.viewer-stage img.ready{opacity:1;cursor:zoom-in}
.feed-state{position:absolute;z-index:3;inset:0;display:grid;place-items:center;text-align:center;padding:24px;background:#050506e0;color:#e6e6e8;font-size:14px}
.feed-state.hidden{opacity:0;pointer-events:none}
.feed-stamp{position:absolute;z-index:4;left:12px;bottom:12px;max-width:calc(100% - 24px);padding:5px 9px;border-radius:4px;background:#000000b3;color:#e6e6e8;font-size:11px;line-height:1.5;font-variant-numeric:tabular-nums}
.feed-stamp:empty{display:none}
.feed-fs-btn{position:absolute;z-index:5;right:12px;top:12px;display:grid;place-items:center;width:40px;height:40px;border:1px solid #ffffff40;border-radius:6px;background:#000000b3;color:#fff;cursor:pointer}
.feed-fs-btn:hover{background:var(--surface-hover)}
.feed-fs-btn svg{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen,.viewer-stage.fs-fallback{aspect-ratio:auto;min-height:0;background:#000}
.viewer-stage:fullscreen,.viewer-stage:-webkit-full-screen{width:100%;height:100%}
.viewer-stage.fs-fallback{position:fixed;inset:0;z-index:999;width:100vw;height:100vh}
.viewer-stage:fullscreen img,.viewer-stage:-webkit-full-screen img,.viewer-stage.fs-fallback img{cursor:zoom-out}
body.fs-locked{overflow:hidden}
.viewer-stage video{position:relative;z-index:1;grid-area:1/1;width:100%;height:100%;object-fit:contain;background:#000;opacity:0;cursor:zoom-in}
.viewer-stage video.ready{opacity:1}
.viewer-stage:fullscreen video,.viewer-stage:-webkit-full-screen video,.viewer-stage.fs-fallback video{cursor:zoom-out}
.live-badge{position:absolute;z-index:4;left:12px;top:12px;display:flex;align-items:center;gap:8px;padding:5px 10px;border-radius:4px;background:#000000b3;color:#e6e6e8;font:600 11px/1.5 var(--sans);letter-spacing:.06em}
.live-badge i{width:8px;height:8px;border-radius:50%;background:var(--dim)}
.live-badge.on i{background:var(--bad);animation:livepulse 1.6s ease-in-out infinite}
@keyframes livepulse{50%{opacity:.3}}
.feed-state .btn{margin-top:14px}

/* Settings */
.settings-summary{flex:none;max-width:100%}
#settingsForm{display:grid;gap:16px;margin-top:16px}
.settings-tablist{display:flex;align-items:center;gap:4px;overflow-x:auto;scrollbar-width:none}
.settings-tab-divided{margin-left:12px}
.settings-panels{min-width:0}
.settings-panel{display:grid;align-content:start;gap:16px;min-width:0}
.settings-group{min-width:0;padding:16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.settings-group h3{margin:0 0 16px;overflow-wrap:anywhere}
.group-note{margin:-8px 0 16px;color:var(--muted);font-size:13px;line-height:1.6}
.dev-body>.group-note{margin-top:0}
.form-grid{display:grid;margin:-12px 0}
.form-grid>*{padding:12px 0}
.form-grid>*+*{border-top:1px solid var(--line)}
.field{min-width:0}
.form-grid>.field{display:grid;grid-template-columns:minmax(0,1fr) minmax(200px,320px);column-gap:24px;align-items:center}
.form-grid>.field>label{grid-column:1;grid-row:1;align-self:end;justify-content:flex-start;margin:0}
.form-grid>.field>small{grid-column:1;grid-row:2;align-self:start;margin:2px 0 0}
.form-grid>.field>input,.form-grid>.field>select,.form-grid>.field>.btn{grid-column:2;grid-row:1/3}
.form-grid>.field>.checkline{grid-column:1/-1}
.field label{display:flex;align-items:baseline;justify-content:space-between;gap:4px 12px;margin-bottom:6px;font-size:13px;font-weight:500}
.field label span{flex:none;color:var(--dim);font-size:11px;font-weight:400;white-space:nowrap}
.field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select,.live-quality{height:var(--control-h);padding:0 10px;border:1px solid var(--line-strong);border-radius:6px;background:var(--surface);color:var(--text);font:14px/1.3 var(--sans)}
.field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select{width:100%}
.live-quality{font-size:13px;font-weight:500}
.field input:focus,.field select:focus,.live-quality:focus{border-color:var(--accent)}
.field input:disabled,.field select:disabled{opacity:.55;cursor:not-allowed}
.field small{display:block;margin-top:6px;color:var(--muted);font-size:12px;line-height:1.6}
.checkline{display:flex;flex-direction:row-reverse;align-items:center;justify-content:space-between;gap:24px}
.checkline input{appearance:none;-webkit-appearance:none;position:relative;flex:none;width:36px;height:20px;margin:0;border:1px solid var(--line-strong);border-radius:999px;background:var(--surface-alt);cursor:pointer;transition:background-color .15s,border-color .15s}
.checkline input::after{content:"";position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;background:var(--muted);transition:transform .15s,background-color .15s}
.checkline input:checked{background:var(--button-primary);border-color:var(--button-primary)}
.checkline input:checked::after{transform:translateX(16px);background:var(--on-primary)}
.checkline input:disabled{opacity:.55;cursor:not-allowed}
.checkline label{display:block;flex:1;min-width:0;margin:0;cursor:pointer}
.checkline label b{display:block;font-size:13px;font-weight:500}
.checkline label span{display:block;margin-top:2px;color:var(--muted);font-size:12px;line-height:1.6;white-space:normal}
.form-note{margin:0;color:var(--muted);font-size:12px;line-height:1.6}
.form-note b{color:var(--text);font-weight:600;font-variant-numeric:tabular-nums}
.settings-savebar{position:sticky;bottom:16px;z-index:5;display:flex;align-items:center;justify-content:space-between;gap:16px 24px;padding:12px 16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface)}
.settings-save-info{min-width:0}
.settings-save-scope{color:var(--text);font-size:13px;line-height:1.5}
.settings-savebar .form-result{min-height:0;margin-top:2px}
.settings-savebar .button-row{flex:none}
.button-row{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
.btn{display:inline-flex;align-items:center;justify-content:center;min-height:var(--control-h);padding:8px 14px;border:1px solid var(--line-strong);border-radius:6px;background:var(--surface);color:var(--text);font:500 13px/1.4 var(--sans);text-decoration:none;cursor:pointer;transition:background-color .15s,border-color .15s}
.btn:hover{background:var(--surface-hover);border-color:var(--muted)}
.btn:disabled,.btn[aria-disabled="true"]{opacity:.45;cursor:not-allowed}
.btn.primary{background:var(--button-primary);border-color:var(--button-primary);color:var(--on-primary)}.btn.primary:hover{background:var(--button-primary-hover);border-color:var(--button-primary-hover)}
.btn.warn{border-color:#806b49;color:var(--warn)}.btn.warn:hover{background:#2a2419;border-color:var(--warn)}
.btn.bad{border-color:#85555b;color:var(--bad)}.btn.bad:hover{background:#2a1c1f;border-color:var(--bad)}
.form-result{min-height:18px;color:var(--dim);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.form-result.ok{color:var(--ok)}.form-result.warn{color:var(--warn)}.form-result.bad{color:var(--bad)}
.action-grid{margin:-12px 0}
.action-item{display:grid;grid-template-columns:180px minmax(0,1fr) 164px;align-items:center;gap:24px;padding:12px 0}
.action-item+.action-item{border-top:1px solid var(--line)}
.action-item b{font-size:14px;font-weight:600}.action-item span{color:var(--muted);font-size:13px;line-height:1.6}
.action-item .btn{width:100%}
.diagnostic-output{position:relative}
.diagnostic-copy{position:absolute;top:12px;right:12px;z-index:1;min-width:144px}
#raw{margin:0;padding:16px;border:1px solid var(--line);border-radius:var(--radius);background:var(--surface);white-space:pre-wrap;overflow-wrap:anywhere;color:var(--muted);font:12px/1.6 var(--mono)}

/* Logs */
.settings-panel .grid{margin-top:0}
.logs-card{border:1px solid var(--line);border-radius:var(--radius);background:var(--surface);overflow:hidden}
.logs-toolbar{display:grid;grid-template-columns:minmax(220px,2fr) repeat(3,minmax(0,1fr)) auto;align-items:end;gap:16px;padding:16px;border-bottom:1px solid var(--line)}
.logs-toolbar .button-row{flex-wrap:nowrap}
.logs-table-wrap{overflow:auto;max-height:620px;overscroll-behavior:contain}
.logs-table{width:100%;border-collapse:collapse;min-width:1050px;font-size:13px}
.logs-table th{position:sticky;top:0;z-index:2;padding:12px;background:var(--surface-alt);color:var(--muted);text-align:left;font-size:12px;font-weight:500;border-bottom:1px solid var(--line)}
.logs-table td{padding:12px;border-top:1px solid var(--line);vertical-align:top}
.logs-table tbody tr:first-child td{border-top:0}.logs-table tbody tr:hover td{background:var(--surface-hover)}
.logs-table .mono{font-size:12px;white-space:nowrap}
.logs-level{display:inline-block;padding:2px 6px;border-radius:4px;font-size:11px;font-weight:500}
.logs-level.info{color:var(--info);background:#1a2430}.logs-level.warning{color:var(--warn);background:#2a2419}.logs-level.error{color:var(--bad);background:#2a1c1f}
.logs-event{color:var(--text);font-size:13px;white-space:nowrap}
.logs-detail{max-width:340px;color:var(--muted);overflow-wrap:anywhere;line-height:1.6}
.logs-flags{display:block;margin-top:4px;color:var(--dim);font-size:11px;line-height:1.6}
.logs-empty{padding:36px 16px;text-align:center;color:var(--dim);font-size:13px}
.logs-foot{display:flex;flex-wrap:wrap;align-items:center;gap:12px;padding:12px 16px;border-top:1px solid var(--line)}
.logs-foot .form-result{flex:1 1 260px;min-height:0}
.logs-badge{--sc:var(--info);--sbg:rgba(14,165,233,.14);--sbd:rgba(14,165,233,.32)}

/* Dialogs */
.modal-overlay{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#050506cc}
.modal-box{width:100%;max-width:460px;max-height:calc(100dvh - 40px);overflow-y:auto;padding:24px;background:var(--surface-alt);border:1px solid var(--line-strong);border-radius:10px}
.modal-box h3{font-size:18px}
.modal-desc{margin:10px 0 20px;color:var(--muted);font-size:13px;line-height:1.7}
.modal-actions{justify-content:flex-end;margin-top:22px}
.merge-modal-box{max-width:620px}
.merge-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}
.merge-review{margin-top:20px;border:1px solid var(--line);border-radius:6px;background:var(--surface)}
.merge-records{display:grid;grid-template-columns:repeat(2,minmax(0,1fr))}
.merge-record{min-width:0;padding:14px 16px}
.merge-record+.merge-record{border-left:1px solid var(--line)}
.merge-record span{display:block;color:var(--dim);font-size:12px}
.merge-record b{display:block;margin-top:5px;font-size:14px;font-weight:600;overflow-wrap:anywhere}
.merge-record small{display:block;margin-top:7px;color:var(--muted);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.merge-note{padding:12px 16px;border-top:1px solid var(--line);color:var(--warn);font-size:12px;line-height:1.6}
.merge-review-empty{padding:16px;color:var(--dim);font-size:13px}
.ptz-modal-box{max-width:600px;display:flex;flex-direction:column;overflow:hidden}
.ptz-modal-head{display:flex;align-items:flex-start;justify-content:space-between;gap:18px;flex:none}
.ptz-filter{flex:none;margin-bottom:16px}
.ptz-list{min-height:80px;max-height:min(50vh,480px);overflow-y:auto;overscroll-behavior:contain;border:1px solid var(--line);border-radius:6px;background:var(--surface)}
.ptz-list-item{display:flex;align-items:center;gap:14px;padding:12px 14px;border-top:1px solid var(--line)}
.ptz-list-item:first-child{border:0}.ptz-list-item:hover{background:var(--surface-hover)}
.ptz-list-copy{min-width:0;flex:1}.ptz-list-copy b{display:block;font-size:14px;font-weight:600;overflow-wrap:anywhere}
.ptz-list-copy span{display:block;margin-top:4px;color:var(--dim);font-size:12px;line-height:1.6;overflow-wrap:anywhere}
.ptz-list-item .btn{min-width:60px;flex:none}
.ptz-empty{padding:28px 16px;text-align:center;color:var(--dim);font-size:13px;line-height:1.7}
.ptz-modal-foot{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;margin-top:16px;color:var(--dim);font-size:12px;flex:none}

@media(max-width:1400px){
  .camera-layout{grid-template-columns:minmax(0,1fr);grid-template-areas:"stage" "side"}
  .camera-side{grid-template-columns:repeat(auto-fit,minmax(200px,1fr));grid-template-rows:none}
}
@media(max-width:1000px){
  .app-header{gap:16px;padding:0 16px}
  .app-shell{grid-template-columns:minmax(0,1fr)}
  .app-sidebar{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line)}
  .sidebar-nav{flex-direction:row;align-items:center;gap:12px;padding:8px 16px}
  .sidebar-h{flex:none;padding:0}
  .cam-list{flex:1;flex-direction:row;gap:6px;min-width:0;padding:2px 0;overflow-x:auto;overflow-y:hidden}
  .cam-list li{flex:none}
  .cam-item{width:auto;max-width:220px;border:1px solid var(--line)}
  .app-sidebar footer{padding:0 16px 8px;border-top:0}
  main{padding:16px}
  .grid{grid-template-columns:repeat(2,minmax(0,1fr))}
  .logs-toolbar{grid-template-columns:repeat(3,minmax(0,1fr))}.logs-toolbar .field:first-child,.logs-toolbar .button-row{grid-column:1/-1}
  .device-header{gap:16px}
  .action-item{grid-template-columns:minmax(0,1fr) 164px;gap:6px 24px}.action-item span{grid-column:1}.action-item .btn{grid-column:2;grid-row:1/3}
}
@media(max-width:860px){
  .app-header{position:static;flex-wrap:wrap;height:auto;min-height:var(--header-h);row-gap:4px}
  .brand{min-height:52px}
  .tabs{order:3;width:100%;padding-bottom:6px}
}
@media(max-width:680px){
  .app-header{column-gap:12px;padding:0 max(16px,env(safe-area-inset-right)) 0 max(16px,env(safe-area-inset-left))}
  .tab,.settings-tab{min-height:44px;font-size:13px}
  .status-pill{font-size:11px}
  .sidebar-nav{padding-inline:max(16px,env(safe-area-inset-left)) max(16px,env(safe-area-inset-right))}
  main{padding:16px max(16px,env(safe-area-inset-right)) 16px max(16px,env(safe-area-inset-left))}
  h1{font-size:18px}
  .device-header{display:block}
  .rate{width:100%;margin-top:12px}.rate-label b{font-size:18px}
  .settings-summary{display:flex;width:fit-content;margin:12px 0 0 auto}
  .kpi-row{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
  .kpi{padding:12px}.kpi-value{font-size:20px}
  .camera-grid{grid-template-columns:minmax(0,1fr)}
  .camera-side{grid-template-columns:minmax(0,1fr)}
  .segmented button{min-height:40px}
  .live-quality{height:44px}
  .dev-list div{grid-template-columns:1fr;gap:4px}
  .grid{grid-template-columns:1fr}
  .chart{height:180px}
  .feed-state{padding:16px;font-size:13px}
  .form-grid>.field{grid-template-columns:minmax(0,1fr)}
  .form-grid>.field>input,.form-grid>.field>select,.form-grid>.field>.btn{grid-column:1;grid-row:2;margin-top:6px}
  .form-grid>.field>small{grid-row:3}
  .settings-savebar{position:static;flex-direction:column;align-items:stretch}
  .settings-savebar .button-row{display:grid;grid-template-columns:1fr 1fr;align-items:stretch}
  .btn{min-height:44px;padding-inline:12px}
  .field input[type="text"],.field input[type="password"],.field input[type="number"],.field input[type="url"],.field select{height:44px;font-size:16px}
  .action-item{grid-template-columns:1fr;gap:8px}.action-item .btn{grid-column:1;grid-row:auto;justify-self:start;width:auto;margin-top:4px}
  .logs-toolbar{grid-template-columns:1fr 1fr;gap:12px}
  .logs-toolbar .field:nth-child(4){grid-column:1/-1}.logs-toolbar .button-row .btn{flex:1}.logs-table-wrap{max-height:65vh}
  .modal-overlay{padding:12px}.modal-box{max-height:calc(100dvh - 24px);padding:20px}
  .merge-fields,.merge-records{grid-template-columns:1fr}.merge-record+.merge-record{border-left:0;border-top:1px solid var(--line)}
  .ptz-list-item{padding:12px}
}
@media(prefers-reduced-motion:reduce){
  *,*::before,*::after{animation:none!important;transition:none!important;scroll-behavior:auto!important}
}
</style>
</head>
<body>
<button class="skip-link" id="skipToContent" type="button">Skip to content</button>
<header class="app-header">
  <a class="brand" href="#overview">Camera Watchdog</a>
  <nav class="tabs" role="tablist" aria-label="Dashboard sections">
    <button class="tab" id="overviewTab" type="button" role="tab" data-view="overview" aria-controls="overviewView" aria-selected="true">Overview</button>
    <button class="tab" id="cameraTab" type="button" role="tab" data-view="camera" aria-controls="cameraView" aria-selected="false" tabindex="-1">Camera</button>
    <button class="tab" id="resourcesTab" type="button" role="tab" data-view="resources" aria-controls="resourcesView" aria-selected="false" tabindex="-1">Resources</button>
    <button class="tab" id="settingsTab" type="button" role="tab" data-view="settings" aria-controls="settingsView" aria-selected="false" tabindex="-1">Settings</button>
  </nav>
  <div class="header-status"><div class="status-pill" id="deviceStatus"><span class="beat"></span><span id="deviceStatusText">connecting…</span></div><div class="status-pill" id="deviceUptime">uptime —</div></div>
</header>
<div class="app-shell">
<aside class="app-sidebar">
  <nav class="sidebar-nav" aria-label="Cameras">
    <div class="sidebar-h"><h2>Cameras</h2><span id="cameraCount"></span></div>
    <ul class="cam-list" id="cameraList"></ul>
    <p class="cam-empty" id="cameraListEmpty">Loading cameras…</p>
  </nav>
  <footer id="foot">ESP32 Camera Watchdog</footer>
</aside>
<main id="mainContent" tabindex="-1">

<div id="overviewView" role="tabpanel" tabindex="0" aria-labelledby="overviewTab" data-view-panel="overview">
  <div class="kpi-row">
    <article class="kpi"><span class="kpi-label">Registered</span><b class="kpi-value" id="overviewTotal">0</b></article>
    <article class="kpi" style="--sc:var(--ok)"><span class="kpi-label"><i class="state-dot"></i>Online</span><b class="kpi-value" id="overviewOnline">0</b></article>
    <article class="kpi" style="--sc:var(--bad)"><span class="kpi-label"><i class="state-dot"></i>Offline</span><b class="kpi-value" id="overviewOffline">0</b></article>
    <article class="kpi" style="--sc:var(--info)"><span class="kpi-label"><i class="state-dot"></i>Recovering</span><b class="kpi-value" id="overviewRecovering">0</b></article>
  </div>
  <p class="overview-note" id="overviewOthers" hidden></p>
  <ul class="camera-grid" id="overviewCameras" aria-label="Camera previews"></ul>
  <p class="overview-note" id="overviewEmpty" hidden>No camera registered. Open <a href="#actions">Settings &gt; Actions</a> to rediscover cameras.</p>
  <p class="overview-note" id="registryResult"></p>
</div>

<div id="cameraView" role="tabpanel" tabindex="0" aria-labelledby="cameraTab" data-view-panel="camera" hidden>

<section class="device-header" id="hero">
  <div class="device-copy">
    <div class="device-title"><h1 class="device-name" id="camName">—</h1><span class="badge" id="badge"><i></i><span id="stateText">loading</span></span></div>
    <div class="device-meta" id="cameraMeta">Loading camera information…</div>
  </div>
  <div class="rate">
    <div class="rate-label"><span>Availability</span><b id="availValue">—</b></div>
    <div class="rate-track" id="arc" aria-hidden="true"></div>
  </div>
</section>

<div class="camera-layout">
  <section class="stage-card" aria-labelledby="stageTitle">
    <div class="stage-bar">
      <h2 id="stageTitle">Live video</h2>
      <div class="segmented" role="group" aria-label="Video source">
        <button type="button" data-camera-mode="live" aria-pressed="true">Live</button>
        <button type="button" data-camera-mode="snapshot" aria-pressed="false">Snapshot</button>
      </div>
      <select class="live-quality" id="liveQuality" data-source="live" aria-label="Live video quality">
        <option value="fluid">Standard</option>
        <option value="high">High resolution</option>
      </select>
      <button class="btn ptz-open-btn" type="button" id="ptzMenuButton" aria-haspopup="dialog" aria-controls="ptzPresetModal" disabled>PTZ presets <span class="ptz-count" id="ptzPresetCount">—</span></button>
    </div>
    <div class="viewer-stage" id="cameraStage" data-mode="live">
      <video id="liveVideo" data-source="live" muted playsinline autoplay disablepictureinpicture></video>
      <img id="cameraFeed" data-source="snapshot" alt="JPEG preview of the monitored camera" decoding="async" hidden>
      <div class="feed-state" id="liveState" data-source="live"><div><span id="liveStateText">Connecting to the camera…</span><br><button class="btn" type="button" id="liveRetry" hidden>Try again</button></div></div>
      <div class="feed-state" id="feedState" data-source="snapshot" hidden>Loading the first camera frame…</div>
      <div class="live-badge" id="liveBadge" data-source="live"><i></i><span id="liveBadgeText">NO SIGNAL</span></div>
      <div class="feed-stamp" id="liveStamp" data-source="live">waiting for video</div>
      <div class="feed-stamp" id="feedStamp" data-source="snapshot" hidden>waiting for image</div>
      <button type="button" class="feed-fs-btn" id="feedFsBtn" aria-label="View full screen" aria-pressed="false" title="Full screen">
        <svg class="ic-expand" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M9 3H5a2 2 0 0 0-2 2v4M15 3h4a2 2 0 0 1 2 2v4M9 21H5a2 2 0 0 1-2-2v-4M15 21h4a2 2 0 0 0 2-2v-4"/></svg>
        <svg class="ic-collapse" viewBox="0 0 24 24" aria-hidden="true" focusable="false" hidden><path d="M4 9V5a2 2 0 0 1 2-2h4M20 9V5a2 2 0 0 0-2-2h-4M4 15v4a2 2 0 0 0 2 2h4M20 15v4a2 2 0 0 1-2 2h-4"/></svg>
      </button>
    </div>
    <div class="form-result" id="ptzActionStatus" hidden></div>
  </section>

  <div class="camera-side">
    <article class="card">
      <h2>Service health</h2>
      <ul class="health-list">
        <li><span class="health-name"><i class="pip" id="p-onvif"></i>ONVIF</span><span class="card-v" id="v-onvif">—</span></li>
        <li><span class="health-name"><i class="pip" id="p-rtsp"></i>RTSP</span><span class="card-v" id="v-rtsp">—</span></li>
        <li><span class="health-name"><i class="pip" id="p-image"></i>JPEG image</span><span class="card-v" id="v-image">—</span></li>
      </ul>
    </article>
    <article class="card">
      <h2>Automatic recovery</h2>
      <p class="card-text" id="l-reboot">—</p>
    </article>
    <article class="card">
      <h2>Email alerts</h2>
      <p class="card-text" id="l-mail">—</p>
    </article>
    <article class="card">
      <h2>Last event</h2>
      <p class="card-text" id="l-event">—</p>
    </article>
  </div>
</div>

<details class="dev-diagnostics">
  <summary>Development diagnostics<a class="dev-link" href="#diagnostics">Open the full JSON in Settings &gt; Diagnostics</a></summary>
  <div class="dev-body">
    <dl class="dev-list">
      <div><dt>ONVIF probe</dt><dd id="m-onvif">—</dd></div>
      <div><dt>RTSP probe</dt><dd id="m-rtsp">—</dd></div>
      <div><dt>JPEG probe</dt><dd id="m-image">—</dd></div>
      <div><dt>Checks</dt><dd><span id="v-checks">—</span> checks · <span id="m-checks">—</span></dd></div>
      <div><dt>ONVIF endpoint</dt><dd id="l-url">—</dd></div>
      <div><dt>Last JPEG</dt><dd id="l-image">—</dd></div>
      <div><dt>ONVIF discovery</dt><dd id="l-disc">—</dd></div>
      <div><dt>Network scan</dt><dd id="l-scan">—</dd></div>
      <div><dt>Snapshot media</dt><dd id="feedMeta">JPEG frames from the selected camera</dd></div>
      <div><dt>Live media</dt><dd id="liveMeta">H.264 video from the selected camera</dd></div>
    </dl>
  </div>
</details>

</div>

<div id="resourcesView" role="tabpanel" tabindex="0" aria-labelledby="resourcesTab" data-view-panel="resources" hidden>
  <section class="device-header" id="resourcesHero">
    <div class="device-copy">
      <div class="device-title"><h1 class="device-name" id="resourcesChip">ESP32</h1><span class="badge" id="resourcesBadge"><i></i><span id="resourcesState">loading</span></span></div>
      <div class="device-meta" id="resourcesMeta">Loading device information…</div>
    </div>
    <div class="rate">
      <div class="rate-label"><span>Free heap</span><b id="resourcesHeapPercent">—</b></div>
      <div class="rate-track" id="resourcesArc" aria-hidden="true"></div>
    </div>
  </section>

  <div class="grid">
    <article class="card">
      <div class="card-h">Free heap<i class="pip" id="p-rheap"></i></div>
      <div class="card-v" id="r-heap">—</div>
      <div class="card-m" id="r-heap-meta">—</div>
      <div class="meter"><i id="r-heap-meter"></i></div>
    </article>
    <article class="card">
      <div class="card-h">Largest allocatable block<i class="pip" id="p-rblock"></i></div>
      <div class="card-v" id="r-block">—</div>
      <div class="card-m" id="r-block-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Lowest heap since boot<i class="pip" id="p-rmin"></i></div>
      <div class="card-v" id="r-min">—</div>
      <div class="card-m" id="r-min-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Firmware and flash</div>
      <div class="card-v info" id="r-flash">—</div>
      <div class="card-m" id="r-flash-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">PSRAM<i class="pip" id="p-rpsram"></i></div>
      <div class="card-v" id="r-psram">—</div>
      <div class="card-m" id="r-psram-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">ESP32 Wi-Fi<i class="pip" id="p-wifi"></i></div>
      <div class="card-v" id="v-wifi"><span class="bars" id="bars"><i></i><i></i><i></i><i></i></span><span id="rssi">—</span></div>
      <div class="card-m" id="m-wifi">—</div>
    </article>
    <article class="card">
      <div class="card-h">ONVIF catalog<i class="pip" id="p-ronvif"></i></div>
      <div class="card-v info" id="r-onvif">—</div>
      <div class="card-m" id="r-onvif-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Camera clock<i class="pip" id="p-rclock"></i></div>
      <div class="card-v info" id="r-clock">—</div>
      <div class="card-m" id="r-clock-meta">—</div>
    </article>
    <article class="card">
      <div class="card-h">Advertised events<i class="pip" id="p-revents"></i></div>
      <div class="card-v info" id="r-events">—</div>
      <div class="card-m" id="r-events-meta">—</div>
    </article>
  </div>

  <section class="chart-card" aria-labelledby="historyTitle">
    <div class="chart-head">
      <div>
        <h2 id="historyTitle">Recent history</h2>
        <p>Up to three minutes of this browser session</p>
      </div>
      <div class="legend">
        <span><i></i>Heap <b id="historyHeap">—</b></span>
        <span><i class="wifi-dot"></i>Wi-Fi <b id="historyRssi">—</b></span>
      </div>
    </div>
    <svg class="chart" viewBox="0 0 600 180" preserveAspectRatio="none" role="img" aria-label="Free heap and Wi-Fi signal history">
      <line x1="0" y1="20" x2="600" y2="20"></line>
      <line x1="0" y1="90" x2="600" y2="90"></line>
      <line x1="0" y1="160" x2="600" y2="160"></line>
      <polyline id="heapHistory" points=""></polyline>
      <polyline class="wifi-line" id="rssiHistory" points=""></polyline>
    </svg>
  </section>
</div>

<div id="settingsView" role="tabpanel" tabindex="0" aria-labelledby="settingsTab" data-view-panel="settings" hidden>
  <section class="device-header" id="settingsHero" style="--sc:var(--info);--sbg:rgba(14,165,233,.14);--sbd:rgba(14,165,233,.32)">
    <div class="device-copy">
      <div class="device-title"><h1 class="device-name" id="settingsTitle">Device settings</h1></div>
      <div class="device-meta" id="settingsMeta">Values stored in NVS, also validated by the firmware.</div>
    </div>
    <span class="badge settings-summary"><i></i><span id="settingsSource">loading settings</span></span>
  </section>

  <form id="settingsForm" autocomplete="off" novalidate>
    <nav class="settings-tablist" role="tablist" aria-label="Settings categories">
      <button class="settings-tab" id="settingsGlobalTab" type="button" role="tab" data-settings-section="global" aria-controls="settingsGlobalPanel" aria-selected="true">Device</button>
      <button class="settings-tab settings-tab-divided" id="settingsCameraTab" type="button" role="tab" data-settings-section="camera" aria-controls="settingsCameraPanel" aria-selected="false" tabindex="-1">Camera</button>
      <button class="settings-tab" id="settingsMonitoringTab" type="button" role="tab" data-settings-section="monitoring" aria-controls="settingsMonitoringPanel" aria-selected="false" tabindex="-1">Monitoring</button>
      <button class="settings-tab" id="settingsRecoveryTab" type="button" role="tab" data-settings-section="recovery" aria-controls="settingsRecoveryPanel" aria-selected="false" tabindex="-1">Recovery</button>
      <button class="settings-tab settings-tab-divided" id="settingsActionsTab" type="button" role="tab" data-settings-section="actions" aria-controls="settingsActionsPanel" aria-selected="false" tabindex="-1">Actions</button>
      <button class="settings-tab" id="settingsDiagnosticsTab" type="button" role="tab" data-settings-section="diagnostics" aria-controls="settingsDiagnosticsPanel" aria-selected="false" tabindex="-1">Diagnostics</button>
      <button class="settings-tab" id="settingsLogsTab" type="button" role="tab" data-settings-section="logs" aria-controls="settingsLogsPanel" aria-selected="false" tabindex="-1">Logs</button>
    </nav>

    <div class="settings-panels">
      <section class="settings-panel" id="settingsGlobalPanel" role="tabpanel" aria-labelledby="settingsGlobalTab" data-settings-section-panel="global">
        <div class="settings-group">
          <h3>Connection</h3>
          <div class="form-grid">
            <div class="field"><label for="cfgLocalHostname">Local address <span>1 to 31 characters</span></label><input id="cfgLocalHostname" type="text" minlength="1" maxlength="31" pattern="[A-Za-z0-9](?:[A-Za-z0-9\-]{0,29}[A-Za-z0-9])?" placeholder="camera-watchdog" required><small>Use letters, numbers, and hyphens. The address will be http://chosen-name.local/.</small></div>
            <div class="field"><label>Wi-Fi network</label><a class="btn" id="settingsWifiLink" href="/wifi">Choose another Wi-Fi network</a><small>Lists available networks and checks the connection before saving it.</small></div>
          </div>
        </div>
        <div class="settings-group">
          <h3>Dashboard and status light</h3>
          <div class="form-grid">
            <div class="field"><label for="cfgStatusRefresh">Dashboard refresh <span>1 to 30 s</span></label><input id="cfgStatusRefresh" type="number" min="1" max="30" step="0.5" required><small>Refreshes the dashboard data. Default: 3 s.</small></div>
            <div class="field"><div class="checkline"><input id="cfgLedEnabled" type="checkbox" role="switch"><label for="cfgLedEnabled"><b>Enable status LED</b><span>Blinks when Wi-Fi is disconnected, a camera is offline or recovering, or an incident is active.</span></label></div></div>
          </div>
        </div>
        <details class="settings-advanced">
          <summary>Discovery settings</summary>
          <div class="dev-body">
            <p class="group-note">Routine discovery follows the scan interval. The minimum rediscovery interval limits additional discovery attempts.</p>
            <div class="form-grid">
              <div class="field"><div class="checkline"><input id="cfgAutoScan" type="checkbox" role="switch"><label for="cfgAutoScan"><b>Allow automatic subnet scan</b><span>After confirmed failures, scans up to 254 hosts on RTSP port 554 to find cameras missed by discovery.</span></label></div></div>
              <div class="field"><label for="cfgDiscoveryTimeout">Discovery timeout <span>1 to 15 s</span></label><input id="cfgDiscoveryTimeout" type="number" min="1" max="15" step="0.1" required><small>Time to collect WS-Discovery replies. Default: 3.5 s.</small></div>
              <div class="field"><label for="cfgRediscoveryInterval">Minimum rediscovery interval <span>30 to 3600 s</span></label><input id="cfgRediscoveryInterval" type="number" min="30" max="3600" step="1" required><small>Minimum spacing between discovery attempts. Default: 30 s.</small></div>
              <div class="field"><label for="cfgSubnetScanInterval">Scan interval <span>1 to 1440 min</span></label><input id="cfgSubnetScanInterval" type="number" min="1" max="1440" step="1" required><small>Also controls routine discovery scheduling. Default: 5 min.</small></div>
              <div class="field"><label for="cfgScanTimeout">Scan connection timeout <span>20 to 1000 ms</span></label><input id="cfgScanTimeout" type="number" min="20" max="1000" step="10" required><small>Time allowed for a connection during a subnet scan. Default: 1000 ms.</small></div>
            </div>
          </div>
        </details>
      </section>

      <section class="settings-panel" id="settingsCameraPanel" role="tabpanel" aria-labelledby="settingsCameraTab" data-settings-section-panel="camera" hidden>
        <div class="settings-group">
          <h3>Identity and credentials</h3>
          <div class="form-grid">
            <div class="field"><label for="cfgCameraName">Camera name <span>1 to 48 characters</span></label><input id="cfgCameraName" type="text" minlength="1" maxlength="48" required><small>Shown in the dashboard and email subjects.</small></div>
            <div class="field"><label for="cfgCameraUsername">Camera username</label><input id="cfgCameraUsername" type="text" maxlength="64" autocomplete="off" autocapitalize="none" spellcheck="false"><small>For access without authentication, choose Remove username and password.</small></div>
            <div class="field"><label for="cfgCameraPasswordAction">Camera password</label><select id="cfgCameraPasswordAction"><option value="keep">Keep the current password</option><option value="replace">Set or replace the password</option><option value="clear">Remove username and password</option></select><small id="cfgCameraPasswordStatus" aria-live="polite">Waiting for the query.</small></div>
            <div class="field"><label for="cfgCameraPassword">New password</label><input id="cfgCameraPassword" type="password" maxlength="128" autocomplete="new-password" disabled><small>Choose Set or replace to enter a password. An empty password is allowed with a username. Spaces are part of the password.</small></div>
            <p class="form-note">These credentials let the watchdog access the camera. Saving them renews the connection and starts a check. The saved password never returns to the browser. Restoring defaults keeps both username and password.</p>
          </div>
        </div>
        <details class="settings-advanced">
          <summary>Snapshot preview settings</summary>
          <div class="dev-body">
            <p class="group-note">Timing and profile selection for JPEG previews in Snapshot mode.</p>
            <div class="form-grid">
              <div class="field"><label for="cfgSnapshotProfileMatch">JPEG profile name match <span>up to 32 characters</span></label><input id="cfgSnapshotProfileMatch" type="text" maxlength="32" placeholder="snap"><small>Used when choosing a JPEG-compatible ONVIF profile. Default: snap.</small></div>
              <div class="field"><label for="cfgFeedMinimum">Minimum snapshot pause <span>0.25 to 10 s</span></label><input id="cfgFeedMinimum" type="number" min="0.25" max="10" step="0.05" required><small>Minimum pause after an image finishes loading. Default: 5 s.</small></div>
              <div class="field"><label for="cfgFeedTarget">Target snapshot cycle <span>0.5 to 30 s</span></label><input id="cfgFeedTarget" type="number" min="0.5" max="30" step="0.1" required><small>Must be at least the minimum pause. Image transfer can make the cycle longer. Default: 5 s.</small></div>
            </div>
          </div>
        </details>
        <details class="settings-advanced">
          <summary>Connection settings</summary>
          <div class="dev-body">
            <div class="form-grid">
              <div class="field"><label for="cfgCameraRtspPort">RTSP port <span>1 to 65535</span></label><input id="cfgCameraRtspPort" type="number" min="1" max="65535" step="1" required><small>Default: 554.</small></div>
              <div class="field"><label for="cfgTcpTimeout">TCP connection timeout <span>200 to 5000 ms</span></label><input id="cfgTcpTimeout" type="number" min="200" max="5000" step="50" required><small>Default: 900 ms.</small></div>
              <div class="field"><label for="cfgServiceTimeout">ONVIF/RTSP response timeout <span>500 to 10000 ms</span></label><input id="cfgServiceTimeout" type="number" min="500" max="10000" step="100" required><small>Default: 3000 ms.</small></div>
              <div class="field"><label for="cfgSnapshotTimeout">JPEG timeout <span>1 to 30 s</span></label><input id="cfgSnapshotTimeout" type="number" min="1" max="30" step="0.5" required><small>Default: 5 s.</small></div>
            </div>
          </div>
        </details>
      </section>

      <section class="settings-panel" id="settingsMonitoringPanel" role="tabpanel" aria-labelledby="settingsMonitoringTab" data-settings-section-panel="monitoring" hidden>
        <div class="settings-group">
          <h3>Checks and thresholds</h3>
          <p class="group-note">How the watchdog confirms a failure and a return to normal operation.</p>
          <div class="form-grid">
            <div class="field"><label for="cfgHealthCheckInterval">Check interval <span>10 to 3600 s</span></label><input id="cfgHealthCheckInterval" type="number" min="10" max="3600" step="1" required><small>Default: 15 s.</small></div>
            <div class="field"><label for="cfgFailuresBeforeOffline">Control failures before offline <span>1 to 20</span></label><input id="cfgFailuresBeforeOffline" type="number" min="1" max="20" step="1" required><small>Must be at least the rediscovery threshold below. Default: 3.</small></div>
            <div class="field"><label for="cfgVideoFailuresBeforeAlert">Image failures before alert <span>1 to 20</span></label><input id="cfgVideoFailuresBeforeAlert" type="number" min="1" max="20" step="1" required><small>Default: 2.</small></div>
            <div class="field"><label for="cfgVideoFailuresBeforeReboot">Image failures before restart <span>1 to 20</span></label><input id="cfgVideoFailuresBeforeReboot" type="number" min="1" max="20" step="1" required><small>Must be at least the image alert threshold. Also confirms a media failure when automatic restart is off. Default: 3.</small></div>
            <div class="field"><label for="cfgSuccessesBeforeRecovery">Successful checks before recovery <span>1 to 10</span></label><input id="cfgSuccessesBeforeRecovery" type="number" min="1" max="10" step="1" required><small>Default: 2.</small></div>
            <p class="form-note">At this check interval: image alert after about <b id="estimateAlert">—</b>, restart after about <b id="estimateReboot">—</b>, and offline after about <b id="estimateOffline">—</b>. Startup grace, check duration, and restart limits can add time.</p>
          </div>
        </div>
        <div class="settings-group">
          <h3>Email alerts</h3>
          <div class="form-grid">
            <div class="field"><div class="checkline"><input id="cfgEmailAlerts" type="checkbox" role="switch"><label for="cfgEmailAlerts"><b>Enable email alerts</b><span>Turning this off cancels pending notifications and keeps the Resend credentials.</span></label></div></div>
            <div class="field"><div class="checkline"><input id="cfgEmailIpChange" type="checkbox" role="switch"><label for="cfgEmailIpChange"><b>Notify when the camera changes IP</b><span>Used when email alerts are enabled.</span></label></div></div>
          </div>
        </div>
        <details class="settings-advanced">
          <summary>Startup and rediscovery</summary>
          <div class="dev-body">
            <div class="form-grid">
              <div class="field"><label for="cfgStartupGrace">Startup grace <span>0 to 600 s</span></label><input id="cfgStartupGrace" type="number" min="0" max="600" step="1" required><small>Time allowed for the camera to settle when monitoring starts. Default: 90 s.</small></div>
              <div class="field"><label for="cfgFailuresBeforeRediscovery">Control failures before rediscovery <span>1 to 20</span></label><input id="cfgFailuresBeforeRediscovery" type="number" min="1" max="20" step="1" required><small>Must not exceed the offline threshold. Default: 2.</small></div>
            </div>
          </div>
        </details>
      </section>

      <section class="settings-panel" id="settingsRecoveryPanel" role="tabpanel" aria-labelledby="settingsRecoveryTab" data-settings-section-panel="recovery" hidden>
        <div class="settings-group">
          <h3>Restart policy</h3>
          <div class="form-grid">
            <div class="field"><div class="checkline"><input id="cfgAutoReboot" type="checkbox" role="switch"><label for="cfgAutoReboot"><b>Enable automatic camera restart</b><span>Sends ONVIF SystemReboot after the image failure threshold set in Monitoring.</span></label></div></div>
            <div class="field"><label for="cfgRebootGrace">Grace after restart <span>30 to 600 s</span></label><input id="cfgRebootGrace" type="number" min="30" max="600" step="1" required><small>Pauses checks while the camera restarts. Default: 60 s.</small></div>
          </div>
        </div>
        <div class="settings-group">
          <h3>Limits against restart loops</h3>
          <p class="group-note">These limits also apply to manual camera restarts.</p>
          <div class="form-grid">
            <div class="field"><label for="cfgRebootCooldown">Minimum time between restarts <span>2 to 1440 min</span></label><input id="cfgRebootCooldown" type="number" min="2" max="1440" step="1" required><small>Default: 5 min.</small></div>
            <div class="field"><label for="cfgRebootWindow">Counting window <span>1 to 168 h</span></label><input id="cfgRebootWindow" type="number" min="1" max="168" step="1" required><small>Must cover the minimum time between restarts. Default: 1 h.</small></div>
            <div class="field"><label for="cfgMaxReboots">Maximum restarts in the window <span>1 to 20</span></label><input id="cfgMaxReboots" type="number" min="1" max="20" step="1" required><small>Only accepted ONVIF restart commands use the quota. Default: 3.</small></div>
          </div>
        </div>
      </section>

      <section class="settings-panel settings-tool" id="settingsActionsPanel" role="tabpanel" aria-labelledby="settingsActionsTab" data-settings-section-panel="actions" hidden>
        <div class="settings-group">
          <h3 id="actionCameraScope">Selected camera: waiting for registration</h3>
          <div class="action-grid">
            <div class="action-item"><b>Check now</b><span>Checks ONVIF, RTSP and JPEG on the selected camera.</span><button class="btn" type="button" data-admin-action="health-check" disabled>Run</button></div>
            <div class="action-item"><b>Restart camera</b><span>Restarts the selected camera, respecting the cooldown and the window limit.</span><button class="btn warn" type="button" data-admin-action="reboot-camera" disabled>Restart camera</button></div>
          </div>
        </div>
        <div class="settings-group">
          <h3>All cameras</h3>
          <div class="action-grid">
            <div class="action-item"><b>Rediscover cameras</b><span>Updates the registered cameras and automatically registers any new ones found.</span><button class="btn" type="button" data-admin-action="rediscover" disabled>Rediscover</button></div>
            <div class="action-item"><b>Restart all cameras</b><span>Sends SystemReboot to every camera in sequence, respecting the limits of each camera.</span><button class="btn warn" type="button" data-admin-action="reboot-all-cameras" disabled>Restart all</button></div>
            <div class="action-item" id="mergeAction" hidden><b>Merge registrations</b><span>Merges confirmed records of the same camera, choosing which ID and which settings stay.</span><button class="btn" type="button" id="mergeOpen">Review registrations</button></div>
          </div>
        </div>
        <details class="settings-advanced">
          <summary>Camera maintenance</summary>
          <div class="dev-body">
            <div class="action-grid">
              <div class="action-item"><b>Reset limiter</b><span>Resets the cooldown and attempts of the selected camera.</span><button class="btn warn" type="button" data-admin-action="reset-reboot-limiter" disabled>Reset limit</button></div>
              <div class="action-item"><b>Forget registration</b><span>Permanently removes only the selected camera from monitoring and from NVS. An active camera can be discovered again.</span><button class="btn bad" type="button" id="forgetCameraOpen" disabled>Forget camera</button></div>
            </div>
          </div>
        </details>
        <details class="settings-advanced">
          <summary>Device maintenance</summary>
          <div class="dev-body">
            <div class="action-grid">
              <div class="action-item"><b>Restart ESP32</b><span>Restarts only the watchdog, without cutting power to the camera.</span><button class="btn bad" type="button" data-admin-action="reboot-esp32" disabled>Restart ESP32</button></div>
              <div class="action-item"><b>Clear logs</b><span>Removes the previous history and keeps only the clearing marker.</span><button class="btn bad" type="button" data-admin-action="clear-logs" disabled>Clear logs</button></div>
              <div class="action-item"><b>Erase ESP32 data</b><span>Erases registrations, saved settings, Wi-Fi networks saved from the dashboard, and the log history, then restarts the device. The firmware and the built-in credentials stay. Active cameras can be rediscovered.</span><button class="btn bad" type="button" id="factoryResetOpen" disabled>Erase all data</button></div>
            </div>
          </div>
        </details>
        <div class="form-result" id="adminActionStatus">No action requested in this session.</div>
      </section>

      <section class="settings-panel settings-tool" id="settingsDiagnosticsPanel" role="tabpanel" aria-labelledby="settingsDiagnosticsTab" data-settings-section-panel="diagnostics" hidden>
        <div class="diagnostic-output">
          <button class="btn diagnostic-copy" id="copyDiagnostics" type="button" aria-label="Copy diagnostics JSON" disabled><span id="diagnosticCopyStatus" role="status" aria-live="polite">Copy JSON</span></button>
          <pre id="raw">Loading…</pre>
        </div>
      </section>

      <section class="settings-panel settings-tool" id="settingsLogsPanel" role="tabpanel" aria-labelledby="settingsLogsTab" data-settings-section-panel="logs" hidden>
        <div class="grid">
          <article class="card">
            <div class="card-h">Stored records</div>
            <div class="card-v info" id="logsCount">—</div>
            <div class="card-m" id="logsCountMeta">circular limit of 7,500 records</div>
            <div class="meter"><i id="logsCountMeter"></i></div>
          </article>
          <article class="card">
            <div class="card-h">Logs partition</div>
            <div class="card-v info" id="logsStorage">—</div>
            <div class="card-m" id="logsStorageMeta">waiting for LittleFS data</div>
            <div class="meter"><i id="logsStorageMeter"></i></div>
          </article>
          <article class="card">
            <div class="card-h">Integrity</div>
            <div class="card-v" id="logsIntegrity">—</div>
            <div class="card-m" id="logsIntegrityMeta">dropped records and write errors</div>
          </article>
        </div>

        <section class="logs-card" aria-labelledby="logsTableTitle">
          <div class="logs-toolbar">
            <div class="field">
              <label for="logsSearch">Filter records <span>in the loaded items</span></label>
              <input id="logsSearch" type="text" maxlength="80" placeholder="Event, target, detail or status">
            </div>
            <div class="field">
              <label for="logsLevel">Level</label>
              <select id="logsLevel">
                <option value="">All</option>
                <option value="warning">Warning</option>
                <option value="error">Error</option>
              </select>
            </div>
            <div class="field">
              <label for="logsCategory">Category</label>
              <select id="logsCategory">
                <option value="">All</option>
                <option value="system">System</option>
                <option value="wifi">Wi-Fi</option>
                <option value="camera">Camera</option>
                <option value="discovery">Discovery</option>
                <option value="onvif">ONVIF</option>
                <option value="snapshot">Snapshot</option>
                <option value="rtsp">RTSP</option>
                <option value="health">Health check</option>
                <option value="action">Actions</option>
                <option value="notification">Notifications</option>
                <option value="settings">Settings</option>
              </select>
            </div>
            <div class="field">
              <label for="logsScope">View</label>
              <select id="logsScope">
                <option value="diagnostic">Diagnostic</option>
                <option value="all">All attempts</option>
              </select>
            </div>
            <div class="button-row">
              <button class="btn" id="logsRefresh" type="button">Refresh</button>
              <a class="btn primary" id="logsExport" href="/api/logs/export" download="camera-watchdog-logs.csv">Export full CSV</a>
            </div>
          </div>
          <div class="logs-table-wrap">
            <table class="logs-table">
              <thead>
                <tr><th id="logsTableTitle">Date and time</th><th>Level</th><th>Camera</th><th>Event</th><th>Target</th><th>Result</th><th>Duration</th><th>Details</th></tr>
              </thead>
              <tbody id="logsTableBody"></tbody>
            </table>
            <div class="logs-empty" id="logsEmpty">No records loaded.</div>
          </div>
          <div class="logs-foot">
            <span class="badge logs-badge"><i></i><span id="logsState">loading history</span></span>
            <div class="form-result" id="logsStatus">Open this section to read the persistent history.</div>
            <button class="btn" id="logsLoadMore" type="button" hidden>Load 100 more</button>
          </div>
        </section>
      </section>
    </div>

    <div class="settings-savebar" id="settingsSavebar">
      <div class="settings-save-info">
        <p class="settings-save-scope" id="settingsSaveScope">Load the device or camera settings before saving.</p>
        <div class="form-result" id="settingsFormStatus">Load the tab to read the current values.</div>
      </div>
      <div class="button-row">
        <button class="btn warn" id="settingsReset" type="button" disabled>Restore device defaults</button>
        <button class="btn primary" id="settingsSave" type="submit" disabled>Save changes</button>
      </div>
    </div>
  </form>
</div>

<div class="modal-overlay" id="ptzPresetModal" hidden>
  <div class="modal-box ptz-modal-box" role="dialog" aria-modal="true" aria-labelledby="ptzModalTitle" aria-describedby="ptzMeta">
    <div class="ptz-modal-head">
      <div>
        <h3 id="ptzModalTitle">PTZ presets</h3>
        <p class="modal-desc" id="ptzMeta">Waiting for the camera's ONVIF catalog.</p>
      </div>
      <button class="btn" type="button" id="ptzModalClose">Close</button>
    </div>
    <div class="field ptz-filter">
      <label for="ptzPresetFilter">Filter presets <span>name or token</span></label>
      <input id="ptzPresetFilter" type="text" maxlength="64" autocomplete="off" placeholder="E.g.: Daemon, gate or 12">
    </div>
    <div class="ptz-list" id="ptzPresetList" role="list">
      <div class="ptz-empty">The ESP32 will query the presets directly from the camera.</div>
    </div>
    <div class="ptz-modal-foot"><span id="ptzVisibleCount">No preset loaded</span><span id="ptzPauseNote">Preview is paused while this control is open.</span></div>
  </div>
</div>

<div class="modal-overlay" id="mergeModal" hidden>
  <div class="modal-box merge-modal-box" role="dialog" aria-modal="true" aria-labelledby="mergeTitle" aria-describedby="mergeDescription">
    <h3 id="mergeTitle">Merge registrations of the same camera</h3>
    <p class="modal-desc" id="mergeDescription">Compare the records and keep as primary the one whose ID, name, and settings should prevail.</p>
    <div class="merge-fields">
      <div class="field"><label for="mergePrimary">Primary registration</label><select id="mergePrimary"></select></div>
      <div class="field"><label for="mergeSecondary">Registration to merge</label><select id="mergeSecondary"></select></div>
    </div>
    <div class="merge-review" id="mergeSummary" aria-live="polite"><div class="merge-review-empty">Select two registrations to compare.</div></div>
    <div class="form-result bad" id="mergeStatus" role="status" aria-live="polite" hidden></div>
    <div class="button-row modal-actions">
      <button class="btn" type="button" id="mergeCancel">Cancel</button>
      <button class="btn primary" type="button" id="mergeSubmit" disabled>Continue</button>
    </div>
  </div>
</div>

<div class="modal-overlay" id="adminConfirmModal" hidden>
  <div class="modal-box" role="dialog" aria-modal="true" aria-labelledby="adminConfirmTitle">
    <h3 id="adminConfirmTitle">Confirm admin action</h3>
    <p class="modal-desc" id="adminConfirmDesc"></p>
    <div class="field" id="adminConfirmPhraseField" hidden><label for="adminConfirmPhrase">Type ERASE EVERYTHING to confirm</label><input id="adminConfirmPhrase" type="text" autocomplete="off" spellcheck="false"></div>
    <div class="field" id="adminConfirmPasswordField" hidden><label for="adminConfirmPassword">Admin password</label><input id="adminConfirmPassword" type="password" maxlength="96" autocomplete="current-password"></div>
    <div class="form-result bad" id="adminConfirmError" hidden></div>
    <div class="button-row modal-actions">
      <button class="btn" type="button" id="adminConfirmCancel">Cancel</button>
      <button class="btn primary" type="button" id="adminConfirmSubmit">Confirm</button>
    </div>
  </div>
</div>

</main>
</div>

<script>
const $ = id => document.getElementById(id);
const PALETTE = {
  online:    ['Online',      'var(--ok)',   'rgba(16,185,129,.14)', 'rgba(16,185,129,.32)'],
  degraded:  ['Unstable',    'var(--warn)', 'rgba(245,158,11,.14)', 'rgba(245,158,11,.32)'],
  recovering:['Recovering', 'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)'],
  offline:   ['Offline',     'var(--bad)',  'rgba(244,63,94,.14)', 'rgba(244,63,94,.32)'],
  discovering:['Searching', 'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)'],
  starting:  ['Starting',   'var(--info)', 'rgba(14,165,233,.14)', 'rgba(14,165,233,.32)']
};
const HISTORY_LIMIT = 60;
const resourceHistory = { heap: [], rssi: [] };

function cameraUrl(path, id = selectedCameraId) {
  return path + (path.includes('?') ? '&' : '?') + 'cameraId=' + encodeURIComponent(id);
}

function currentSettingsScope() {
  return (activeView === 'settings' && settingsFormSection === 'global') || !selectedCameraId ? 'global' : 'camera';
}

function selectedCameraName() {
  const selected = cameraRegistry.find(camera => camera.id === selectedCameraId);
  return selected ? selected.name : 'No camera registered';
}

// Reconciles a list by key: creates missing nodes, updates existing ones in place
// and moves a node only when its position differs, so images and focus survive polling.
function syncKeyedList(container, items, keyOf, create, update) {
  const keys = new Set(items.map(item => String(keyOf(item))));
  [...container.children].forEach(node => { if (!keys.has(node.dataset.key)) node.remove(); });
  const existing = new Map([...container.children].map(node => [node.dataset.key, node]));
  let cursor = container.firstElementChild;
  items.forEach(item => {
    const key = String(keyOf(item));
    let node = existing.get(key);
    if (!node) {
      node = create(item);
      node.dataset.key = key;
    }
    update(node, item);
    if (node === cursor) cursor = cursor.nextElementSibling;
    else container.insertBefore(node, cursor);
  });
}

function createCameraListItem(camera) {
  const item = document.createElement('li');
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'cam-item';
  const dot = document.createElement('i');
  dot.className = 'state-dot';
  const name = document.createElement('span');
  name.className = 'cam-item-name';
  const state = document.createElement('span');
  state.className = 'cam-item-state';
  button.append(dot, name, state);
  button.addEventListener('click', () => {
    selectCamera(camera.id);
    if (activeView === 'overview' && selectedCameraId === camera.id) activateView('camera', true);
  });
  item.append(button);
  return item;
}

function updateCameraListItem(item, camera) {
  const palette = PALETTE[camera.state] || ['Unknown', 'var(--dim)'];
  const button = item.firstElementChild;
  button.style.setProperty('--sc', palette[1]);
  button.title = camera.name;
  button.querySelector('.cam-item-name').textContent = camera.name;
  button.querySelector('.cam-item-state').textContent = palette[0];
  if (camera.id === selectedCameraId) button.setAttribute('aria-current', 'true');
  else button.removeAttribute('aria-current');
}

function updateCameraSelector() {
  const selected = cameraRegistry.find(camera => camera.id === selectedCameraId);
  $('cameraCount').textContent = cameraRegistry.length;
  $('cameraListEmpty').textContent = 'No camera registered';
  $('cameraListEmpty').hidden = cameraRegistry.length > 0;
  syncKeyedList($('cameraList'), cameraRegistry, camera => camera.id, createCameraListItem, updateCameraListItem);
  $('actionCameraScope').textContent = selected ? 'Selected camera: ' + selected.name : 'No camera selected';
  updateSettingsHero();
  document.querySelectorAll('.settings-tab[data-settings-section]').forEach(tab => {
    const section = tab.dataset.settingsSection;
    tab.disabled = section !== 'global' && !SETTINGS_TOOL_SECTIONS.has(section) && !selected;
  });
  if (activeView === 'settings' && !selected && !settingsDirty
    && activeSettingsSection !== 'global' && !SETTINGS_TOOL_SECTIONS.has(activeSettingsSection)) {
    activateSettingsSection('global');
  }
}

function updateSettingsScope() {
  const globalScope = currentSettingsScope() === 'global';
  const blocked = !settingsLoaded || settingsBusy || factoryResetPending;
  Object.entries(SETTINGS_FIELDS).forEach(([key, descriptor]) => {
    $(descriptor.id).disabled = blocked || GLOBAL_SETTING_FIELDS.has(key) !== globalScope;
  });
  $('cfgCameraPasswordAction').disabled = blocked || globalScope;
  updateCameraCredentialFields();
  updateSettingsHero();
}

function updateSettingsHero() {
  const selected = cameraRegistry.find(camera => camera.id === selectedCameraId);
  const tool = SETTINGS_TOOL_SECTIONS.get(activeSettingsSection);
  const globalScope = currentSettingsScope() === 'global';
  if (tool) {
    $('settingsTitle').textContent = tool.perCamera ? tool.title + ' · ' + (selected ? selected.name : 'no camera') : tool.title;
    $('settingsMeta').textContent = tool.meta;
  } else {
    $('settingsTitle').textContent = globalScope ? 'Device settings' : 'Settings · ' + (selected ? selected.name : 'camera');
    $('settingsMeta').textContent = settingsFormMetaText;
  }
  $('settingsSource').textContent = settingsFormSourceText;
  $('settingsSource').closest('.settings-summary').hidden = Boolean(tool);
  $('settingsSaveScope').textContent = globalScope
    ? 'Saves all device settings, including advanced values.'
    : 'Saves all settings for ' + selectedCameraName() + ', including advanced values.';
  $('settingsReset').textContent = globalScope ? 'Restore device defaults' : 'Restore camera defaults';
}

function selectCamera(id) {
  if (!cameraRegistry.some(camera => camera.id === id)) return;
  if (id === selectedCameraId) { cameraSelectionManual = true; return; }
  if (settingsBusy || !$('adminConfirmModal').hidden) return;
  if (settingsDirty && !window.confirm('Discard the unsaved changes before switching cameras?')) return;
  settingsDirty = false;
  selectedCameraId = id;
  cameraSelectionManual = true;
  settingsRequestGeneration++;
  settingsLoaded = false;
  settingsValues = null;
  settingsCameraPasswordConfigured = false;
  settingsLoadedKey = '';
  settingsLoading = false;
  settingsLoadPromise = null;
  statusRequestGeneration++;
  if (statusAbortController) statusAbortController.abort();
  statusLoading = false;
  cancelFeedLoad(true);
  clearTimeout(feedTimer);
  feedTimer = null;
  lastOk = 0;
  cameraKnown = false;
  cameraRecovering = false;
  ptzControlsEnabled = false;
  ptzCatalog = { available: false, presets: [], command: {} };
  $('ptzPresetModal').hidden = true;
  if (liveModeActive()) stopLive();
  $('cameraFeed').removeAttribute('src');
  $('cameraFeed').classList.remove('ready');
  $('feedState').classList.remove('hidden');
  $('feedState').textContent = 'Loading the selected camera…';
  $('camName').textContent = cameraRegistry.find(camera => camera.id === id).name;
  $('cameraMeta').textContent = 'Loading camera information…';
  $('stateText').textContent = 'Loading';
  ['v-onvif','m-onvif','v-rtsp','m-rtsp','v-image','m-image','v-checks','m-checks','availValue','l-event','l-url','l-image','l-reboot','l-disc','l-scan','l-mail'].forEach(key => { $(key).textContent = '—'; });
  updateCameraSelector();
  setMutationControlsEnabled(false);
  if (activeView === 'settings' || activeView === 'camera') loadSettings(true);
  refresh(true);
  if (liveModeActive()) startLive();
}

const PREVIEW_PLACEHOLDERS = {
  offline: 'Camera offline',
  recovering: 'Camera restarting',
  starting: 'Camera starting',
  discovering: 'Searching for the camera'
};
const PREVIEW_BUSY_RETRY_DELAYS_MS = [1200, 2400, 4800];
// One JPEG request at a time: the ESP32 web server serves a single client.
const overviewPreviews = { pass: 0, queue: [], cards: new Map(), inFlight: null, pumpTimer: null };

function previewEligible(state) {
  return state === 'online' || state === 'degraded';
}

function snapshotWatchdogMs() {
  return Math.max(12000, Math.min(65000, snapshotHttpTimeoutMs * 2 + 5000));
}

function setCardPreview(card, mode, text, showRetry) {
  card.media.dataset.preview = mode;
  card.media.setAttribute('aria-busy', mode === 'loading' || mode === 'refreshing' ? 'true' : 'false');
  card.text.textContent = text || '';
  card.retry.hidden = !showRetry;
}

function showCardLoading(card) {
  setCardPreview(card, card.hasFrame ? 'refreshing' : 'loading', card.hasFrame ? 'Updating' : 'Loading preview…');
}

function dropPendingFrame(card) {
  if (!card.pendingUrl) return;
  card.image.onload = card.image.onerror = null;
  URL.revokeObjectURL(card.pendingUrl);
  card.pendingUrl = '';
  if (card.objectUrl) card.image.src = card.objectUrl;
  else card.image.removeAttribute('src');
}

function clearCardFrame(card) {
  dropPendingFrame(card);
  card.image.removeAttribute('src');
  card.image.alt = '';
  if (card.objectUrl) URL.revokeObjectURL(card.objectUrl);
  card.objectUrl = '';
  card.hasFrame = false;
  card.stamp.textContent = '';
}

function cancelCardPreview(card) {
  const job = overviewPreviews.inFlight;
  if (job && job.card === card) {
    overviewPreviews.inFlight = null;
    clearTimeout(job.timer);
    job.controller.abort();
    schedulePreviewPump(300);
  }
  overviewPreviews.queue = overviewPreviews.queue.filter(id => id !== card.id);
  clearTimeout(card.retryTimer);
  card.retryTimer = null;
  dropPendingFrame(card);
  if (card.pending) {
    card.pending = false;
    card.requestedPass = -1;
    if (card.hasFrame) setCardPreview(card, 'ready', '');
  }
}

function cancelOverviewPreviews() {
  overviewPreviews.cards.forEach(cancelCardPreview);
  clearTimeout(overviewPreviews.pumpTimer);
  overviewPreviews.pumpTimer = null;
  overviewPreviews.queue = [];
}

function failCardPreview(card, message) {
  card.pending = false;
  clearCardFrame(card);
  setCardPreview(card, 'failed', message, true);
}

function requestCardPreview(card) {
  if (card.pending || activeView !== 'overview' || document.hidden || !previewEligible(card.state)) return;
  card.requestedPass = overviewPreviews.pass;
  card.pending = true;
  card.attempts = 0;
  overviewPreviews.queue.push(card.id);
  showCardLoading(card);
  if (!overviewPreviews.pumpTimer) pumpOverviewPreviews();
}

function enqueueOverviewPreviews() {
  if (activeView !== 'overview' || document.hidden) return;
  cameraRegistry.forEach(camera => {
    const card = overviewPreviews.cards.get(camera.id);
    if (card && card.requestedPass !== overviewPreviews.pass) requestCardPreview(card);
  });
}

function schedulePreviewPump(delay) {
  clearTimeout(overviewPreviews.pumpTimer);
  overviewPreviews.pumpTimer = setTimeout(pumpOverviewPreviews, delay);
}

function pumpOverviewPreviews() {
  clearTimeout(overviewPreviews.pumpTimer);
  overviewPreviews.pumpTimer = null;
  if (overviewPreviews.inFlight || activeView !== 'overview' || document.hidden) return;
  if (statusLoading) {
    if (overviewPreviews.queue.length) schedulePreviewPump(150);
    return;
  }
  while (overviewPreviews.queue.length) {
    const card = overviewPreviews.cards.get(overviewPreviews.queue.shift());
    if (card && card.pending) {
      loadOverviewPreview(card);
      return;
    }
  }
}

function finishOverviewPreview(job) {
  clearTimeout(job.timer);
  if (overviewPreviews.inFlight === job) overviewPreviews.inFlight = null;
  schedulePreviewPump(300);
}

function retryBusyPreview(card) {
  if (card.attempts >= PREVIEW_BUSY_RETRY_DELAYS_MS.length) {
    failCardPreview(card, 'ESP32 busy checking cameras');
    return;
  }
  card.retryTimer = setTimeout(() => {
    card.retryTimer = null;
    if (!card.pending || activeView !== 'overview' || document.hidden) return;
    overviewPreviews.queue.push(card.id);
    if (!overviewPreviews.pumpTimer) pumpOverviewPreviews();
  }, PREVIEW_BUSY_RETRY_DELAYS_MS[card.attempts++]);
}

function showCardFrame(job, blob) {
  const card = job.card;
  const url = URL.createObjectURL(blob);
  card.pendingUrl = url;
  card.image.onload = () => {
    if (card.pendingUrl !== url) return;
    card.image.onload = card.image.onerror = null;
    if (card.objectUrl) URL.revokeObjectURL(card.objectUrl);
    card.objectUrl = url;
    card.pendingUrl = '';
    card.hasFrame = true;
    card.pending = false;
    card.image.alt = 'Latest image from ' + card.open.textContent;
    card.stamp.textContent = 'updated ' + new Date().toLocaleTimeString('en-US');
    setCardPreview(card, 'ready', '');
    finishOverviewPreview(job);
  };
  card.image.onerror = () => {
    if (card.pendingUrl !== url) return;
    finishOverviewPreview(job);
    failCardPreview(card, 'Preview unavailable');
  };
  card.image.src = url;
}

async function loadOverviewPreview(card) {
  const controller = new AbortController();
  const job = { card, controller, timedOut: false, timer: null };
  overviewPreviews.inFlight = job;
  job.timer = setTimeout(() => {
    if (overviewPreviews.inFlight !== job) return;
    job.timedOut = true;
    controller.abort();
    if (card.pendingUrl) {
      finishOverviewPreview(job);
      failCardPreview(card, 'Preview took too long');
    }
  }, snapshotWatchdogMs());
  try {
    const response = await fetch(cameraUrl('/api/camera/snapshot?t=' + Date.now(), card.id), { cache: 'no-store', signal: controller.signal });
    if (overviewPreviews.inFlight !== job) return;
    if (!response.ok) {
      let code = '';
      try { code = (await response.json()).error || ''; } catch (_) {}
      if (overviewPreviews.inFlight !== job) return;
      finishOverviewPreview(job);
      if (response.status === 503 && (code === 'health_check_in_progress' || code === 'camera_io_busy')) retryBusyPreview(card);
      else failCardPreview(card, 'Preview unavailable');
      return;
    }
    const blob = await response.blob();
    if (overviewPreviews.inFlight !== job) return;
    showCardFrame(job, blob);
  } catch (error) {
    if (overviewPreviews.inFlight !== job) return;
    finishOverviewPreview(job);
    failCardPreview(card, job.timedOut ? 'Preview took too long' : 'Preview unavailable');
  }
}

function createOverviewCard(camera) {
  const item = document.createElement('li');
  item.className = 'camera-card';
  const head = document.createElement('div');
  head.className = 'camera-card-head';
  const title = document.createElement('h2');
  const open = document.createElement('button');
  open.type = 'button';
  open.className = 'camera-card-open';
  open.addEventListener('click', () => {
    selectCamera(camera.id);
    if (selectedCameraId === camera.id) activateView('camera', true);
  });
  title.append(open);
  const badge = document.createElement('span');
  badge.className = 'badge';
  const badgeText = document.createElement('span');
  badge.append(document.createElement('i'), badgeText);
  head.append(title, badge);

  const media = document.createElement('div');
  media.className = 'camera-card-media';
  const image = document.createElement('img');
  image.decoding = 'async';
  image.alt = '';
  const overlay = document.createElement('div');
  overlay.className = 'card-overlay';
  const spinner = document.createElement('span');
  spinner.className = 'spinner';
  spinner.setAttribute('aria-hidden', 'true');
  const text = document.createElement('span');
  const retry = document.createElement('button');
  retry.type = 'button';
  retry.className = 'btn';
  retry.textContent = 'Try again';
  retry.hidden = true;
  overlay.append(spinner, text, retry);
  const stamp = document.createElement('span');
  stamp.className = 'card-stamp';
  media.append(image, overlay, stamp);

  const foot = document.createElement('div');
  foot.className = 'camera-card-foot';
  const meta = document.createElement('p');
  meta.className = 'camera-card-meta';
  const ip = document.createElement('span');
  ip.className = 'mono';
  const check = document.createElement('span');
  meta.append(ip, check);
  const event = document.createElement('p');
  event.className = 'camera-card-event';
  foot.append(meta, event);
  item.append(head, media, foot);

  const card = {
    id: camera.id, open, badgeText, media, image, text, retry, stamp, ip, check, event,
    state: '', requestedPass: -1, pending: false, attempts: 0, retryTimer: null,
    objectUrl: '', pendingUrl: '', hasFrame: false
  };
  retry.addEventListener('click', () => requestCardPreview(card));
  overviewPreviews.cards.set(camera.id, card);
  return item;
}

function updateOverviewCard(item, camera) {
  const card = overviewPreviews.cards.get(camera.id);
  const palette = PALETTE[camera.state] || ['Unknown', 'var(--dim)', '', ''];
  item.style.setProperty('--sc', palette[1]);
  item.style.setProperty('--sbg', palette[2]);
  item.style.setProperty('--sbd', palette[3]);
  card.open.textContent = camera.name;
  card.open.title = camera.name;
  card.badgeText.textContent = palette[0];
  card.ip.textContent = camera.ip;
  const stale = camera.lastCheckAgeSeconds !== null && camera.lastCheckAgeSeconds * 1000 > camera.healthCheckIntervalMs * 2;
  card.check.textContent = camera.lastCheckAgeSeconds === null
    ? 'Not checked yet'
    : 'Last check ' + duration(camera.lastCheckAgeSeconds) + ' ago' + (stale ? ' · overdue' : '');
  card.check.className = stale ? 'check-stale' : '';
  card.event.textContent = localizeTimestampText(camera.lastEvent || '—');
  card.event.title = card.event.textContent;
  card.retry.setAttribute('aria-label', 'Load the preview of ' + camera.name + ' again');
  if (card.state === camera.state) return;
  const wasEligible = previewEligible(card.state);
  card.state = camera.state;
  if (!previewEligible(camera.state)) {
    cancelCardPreview(card);
    clearCardFrame(card);
    card.requestedPass = -1;
    setCardPreview(card, 'placeholder', PREVIEW_PLACEHOLDERS[camera.state] || 'No preview in this state');
  } else if (!wasEligible) {
    showCardLoading(card);
  }
}

function renderOverview(payload) {
  const counts = {};
  const others = new Map();
  cameraRegistry.forEach(camera => {
    counts[camera.state] = (counts[camera.state] || 0) + 1;
    if (['online', 'offline', 'recovering'].includes(camera.state)) return;
    const label = PALETTE[camera.state] ? PALETTE[camera.state][0] : 'Other';
    others.set(label, (others.get(label) || 0) + 1);
  });
  $('overviewTotal').textContent = cameraRegistry.length;
  $('overviewOnline').textContent = counts.online || 0;
  $('overviewOffline').textContent = counts.offline || 0;
  $('overviewRecovering').textContent = counts.recovering || 0;
  $('overviewOthers').hidden = others.size === 0;
  $('overviewOthers').textContent = others.size
    ? 'Not counted above: ' + [...others].map(([label, count]) => count + ' ' + label).join(', ')
    : '';
  $('registryResult').textContent = payload.lastResult || '';
  updateMergeControls();
  $('overviewEmpty').hidden = cameraRegistry.length > 0;
  const registeredIds = new Set(cameraRegistry.map(camera => camera.id));
  overviewPreviews.cards.forEach((card, id) => {
    if (registeredIds.has(id)) return;
    cancelCardPreview(card);
    clearCardFrame(card);
    overviewPreviews.cards.delete(id);
  });
  syncKeyedList($('overviewCameras'), cameraRegistry, camera => camera.id, createOverviewCard, updateOverviewCard);
  enqueueOverviewPreviews();
  if (actionsSectionActive() && payload.adminAction && !factoryResetResult) setAdminActionStatus(payload.adminAction.lastResult, payload.adminAction.pending === 'none' ? '' : 'warn');
  $('foot').textContent = 'ESP32 Camera Watchdog · ' + payload.firmwareVersion;
  $('deviceUptime').textContent = 'uptime ' + duration(payload.uptimeSeconds);
}

let mergeOptionsKey = '';

function updateMergeControls() {
  const mergeAvailable = cameraRegistry.length > 1;
  $('mergeAction').hidden = !mergeAvailable;
  if (!mergeAvailable) $('mergeModal').hidden = true;
  const primarySelect = $('mergePrimary');
  const secondarySelect = $('mergeSecondary');
  const optionsKey = JSON.stringify(cameraRegistry.map(camera => [camera.id, camera.name, camera.ip]));
  if (optionsKey !== mergeOptionsKey) {
    const primaryBefore = Number(primarySelect.value);
    const secondaryBefore = Number(secondarySelect.value);
    for (const select of [primarySelect, secondarySelect]) {
      select.replaceChildren(...cameraRegistry.map(camera => {
        const option = document.createElement('option');
        option.value = String(camera.id);
        option.textContent = '#' + camera.id + ' · ' + camera.name;
        return option;
      }));
    }
    if (cameraRegistry.some(camera => camera.id === primaryBefore)) primarySelect.value = String(primaryBefore);
    if (cameraRegistry.some(camera => camera.id === secondaryBefore)) secondarySelect.value = String(secondaryBefore);
    else if (mergeAvailable) secondarySelect.value = String(cameraRegistry.find(camera => camera.id !== Number(primarySelect.value)).id);
    mergeOptionsKey = optionsKey;
  }
  const primary = cameraRegistry.find(camera => camera.id === Number(primarySelect.value));
  const secondary = cameraRegistry.find(camera => camera.id === Number(secondarySelect.value));
  const valid = Boolean(primary && secondary && primary.id !== secondary.id);
  $('mergeSubmit').disabled = !valid || settingsBusy;
  const summary = $('mergeSummary');
  if (!valid) {
    const empty = document.createElement('div');
    empty.className = 'merge-review-empty';
    empty.textContent = 'Select two different registrations.';
    summary.replaceChildren(empty);
    return;
  }
  const records = document.createElement('div');
  records.className = 'merge-records';
  for (const [label, camera] of [['Primary', primary], ['To merge', secondary]]) {
    const record = document.createElement('div');
    record.className = 'merge-record';
    const caption = document.createElement('span');
    caption.textContent = label + ' · #' + camera.id;
    const name = document.createElement('b');
    name.textContent = camera.name;
    const endpoint = document.createElement('small');
    endpoint.textContent = camera.preferredUrl;
    const settings = document.createElement('small');
    settings.textContent = 'Preview ' + Math.round(1000 / camera.feedTargetCycleMs * 10) / 10 + ' FPS · credentials ' + (camera.credentialsConfigured ? 'configured' : 'empty');
    record.append(caption, name, endpoint, settings);
    records.append(record);
  }
  const note = document.createElement('p');
  note.className = 'merge-note';
  note.textContent = 'The name, credentials, and settings of the primary registration prevail. The merged record stays in NVS for auditing.';
  summary.replaceChildren(records, note);
}

$('mergePrimary').addEventListener('change', updateMergeControls);
$('mergeSecondary').addEventListener('change', updateMergeControls);
$('mergeOpen').addEventListener('click', () => {
  if (settingsBusy || cameraRegistry.length < 2) return;
  if (settingsDirty) {
    setAdminActionStatus('Save or discard the pending changes before merging registrations.', 'bad');
    return;
  }
  if (!settingsAuthorizationReady('actions')) return;
  updateMergeControls();
  $('mergeStatus').hidden = true;
  $('mergeModal').hidden = false;
  $('mergePrimary').focus();
});
$('mergeCancel').addEventListener('click', () => { $('mergeModal').hidden = true; $('mergeOpen').focus(); });
$('mergeModal').addEventListener('click', event => {
  if (event.target === $('mergeModal')) $('mergeModal').hidden = true;
});
$('mergeSubmit').addEventListener('click', () => {
  if (settingsBusy) return;
  if (settingsDirty) {
    $('mergeStatus').textContent = 'Save or discard the pending changes before merging registrations.';
    $('mergeStatus').className = 'form-result bad';
    $('mergeStatus').hidden = false;
    return;
  }
  if (!settingsAuthorizationReady('actions')) return;
  const primaryId = Number($('mergePrimary').value);
  const secondaryId = Number($('mergeSecondary').value);
  if (!primaryId || !secondaryId || primaryId === secondaryId) return;
  const primary = cameraRegistry.find(camera => camera.id === primaryId);
  const secondary = cameraRegistry.find(camera => camera.id === secondaryId);
  if (!primary || !secondary) return;
  openConfirmModal('Merge #' + secondaryId + ' (' + secondary.name + ') into #' + primaryId + ' (' + primary.name + '). The settings and credentials of the primary registration prevail; confirm the conflicts before continuing.', async password => {
    settingsBusy = true;
    setMutationControlsEnabled(false);
    updateMergeControls();
    try {
      const params = collectMutationParams(false, password);
      params.set('primaryId', String(primaryId));
      params.set('secondaryId', String(secondaryId));
      params.set('confirmSettings', '1');
      await postForm('/api/cameras/merge', params);
      if (selectedCameraId === secondaryId) {
        cancelFeedLoad(true);
        $('cameraFeed').removeAttribute('src');
        selectedCameraId = primaryId;
        statusRequestGeneration++;
        if (statusAbortController) statusAbortController.abort();
        statusLoading = false;
        if (currentSettingsScope() === 'camera') {
          settingsRequestGeneration++;
          settingsLoading = false;
          settingsLoadPromise = null;
          settingsLoaded = false;
          settingsValues = null;
          settingsCameraPasswordConfigured = false;
          settingsLoadedKey = '';
        }
      }
      $('mergeStatus').textContent = 'Registrations merged. ID #' + secondaryId + ' is now history only.';
      $('mergeStatus').className = 'form-result ok';
      $('mergeStatus').hidden = false;
      $('mergeModal').hidden = true;
      setAdminActionStatus($('mergeStatus').textContent, 'ok');
      await refresh(true);
    } catch (error) {
      $('mergeStatus').textContent = error.message;
      $('mergeStatus').className = 'form-result bad';
      $('mergeStatus').hidden = false;
    } finally {
      settingsBusy = false;
      setMutationControlsEnabled(Boolean(settingsWriteToken));
      updateMergeControls();
    }
  });
});

$('forgetCameraOpen').addEventListener('click', () => {
  if (settingsBusy || !selectedCameraId || !settingsAuthorizationReady('actions')) return;
  if (settingsDirty) {
    setAdminActionStatus('Save or discard the pending changes before removing the registration.', 'bad');
    return;
  }
  const camera = cameraRegistry.find(item => item.id === selectedCameraId);
  if (!camera) return;
  openConfirmModal('Permanently remove registration #' + camera.id + ' (' + camera.name + ', ' + camera.ip + ') from NVS and from monitoring. The settings and credentials of this registration will be erased; the other registrations stay.', async password => {
    settingsBusy = true;
    setMutationControlsEnabled(false);
    setAdminActionStatus('Removing the selected registration…', 'warn');
    let payload;
    try {
      const params = collectMutationParams(false, password);
      params.set('cameraId', String(camera.id));
      params.set('expectedIp', camera.ip);
      params.set('confirmForget', '1');
      payload = await postForm('/api/cameras/forget', params);
      if (selectedCameraId === camera.id) {
        cancelFeedLoad(true);
        $('cameraFeed').removeAttribute('src');
        selectedCameraId = 0;
        cameraSelectionManual = false;
        statusRequestGeneration++;
        if (statusAbortController) statusAbortController.abort();
        statusLoading = false;
        settingsRequestGeneration++;
        settingsLoaded = false;
        settingsValues = null;
        settingsCameraPasswordConfigured = false;
        settingsLoadedKey = '';
      }
    } catch (error) {
      setAdminActionStatus('Removal refused: ' + error.message, 'bad');
      return;
    } finally {
      settingsBusy = false;
      setMutationControlsEnabled(Boolean(settingsWriteToken));
    }
    await refresh(true);
    await loadSettings(false);
    const updated = !cameraRegistry.some(item => item.id === camera.id);
    setAdminActionStatus(updated ? (payload.message || 'Registration removed.') : 'Registration removed; refresh the dashboard to check the list.', updated ? 'ok' : 'warn');
  });
});

async function loadCameraRegistry(signal) {
  const resetEpoch = factoryResetEpoch;
  const response = await fetch('/api/cameras', { cache: 'no-store', signal });
  if (!response.ok) throw new Error('HTTP ' + response.status);
  const payload = await response.json();
  updateFactoryResetState(payload, resetEpoch);
  cameraRegistry = payload.cameras || [];
  const selected = cameraRegistry.find(camera => camera.id === selectedCameraId);
  const preferredCamera = cameraRegistry.find(camera => camera.state === 'online')
    || cameraRegistry.find(camera => camera.state === 'degraded')
    || cameraRegistry.find(camera => camera.identityConfirmed)
    || cameraRegistry[0];
  const shouldFollowActiveCamera = !cameraSelectionManual && !settingsDirty && !settingsBusy
    && $('adminConfirmModal').hidden && selected && preferredCamera && preferredCamera.id !== selected.id
    && (selected.state === 'offline' || preferredCamera.state === 'online');
  const nextCameraId = preferredCamera ? preferredCamera.id : 0;
  if ((!selected || shouldFollowActiveCamera) && nextCameraId !== selectedCameraId) {
    const previousScope = currentSettingsScope();
    const previousSettingsId = previousScope === 'global' ? 0 : selectedCameraId;
    cancelFeedLoad(true);
    $('cameraFeed').removeAttribute('src');
    selectedCameraId = nextCameraId;
    cameraSelectionManual = false;
    const scope = currentSettingsScope();
    const settingsId = scope === 'global' ? 0 : selectedCameraId;
    if (scope !== previousScope || settingsId !== previousSettingsId) {
      settingsRequestGeneration++;
      settingsLoading = false;
      settingsLoadPromise = null;
      settingsLoaded = false;
      settingsValues = null;
      settingsCameraPasswordConfigured = false;
      settingsLoadedKey = '';
      setMutationControlsEnabled(false);
      if (settingsDirty) setSettingsStatus('The camera selection changed. Open Device or choose another camera to discard the pending edits and reload its settings.', 'warn');
    }
  }
  statusRefreshIntervalMs = Number(payload.statusRefreshIntervalMs) || statusRefreshIntervalMs;
  updateCameraSelector();
  renderOverview(payload);
  return payload;
}

function viewFromHash() {
  if (location.hash === '#settings') return 'settings';
  if (location.hash === '#actions' || location.hash === '#logs' || location.hash === '#diagnostics') return 'settings';
  if (location.hash === '#resources') return 'resources';
  if (location.hash === '#camera') return 'camera';
  return 'overview';
}

const SETTINGS_TOOL_SECTIONS = new Map([
  ['actions', { title: 'Actions', perCamera: false, meta: 'Manual checks, restarts, and data maintenance for the cameras and the ESP32.' }],
  ['diagnostics', { title: 'Diagnostics', perCamera: true, meta: 'Full state of the selected camera and the ESP32, in JSON.' }],
  ['logs', { title: 'Logs', perCamera: false, meta: 'Warnings and errors from all cameras and the ESP32, kept across restarts.' }]
]);

function settingsSectionFromHash() {
  const name = location.hash.slice(1);
  return SETTINGS_TOOL_SECTIONS.has(name) ? name : '';
}

function settingsHashFor(section) {
  return SETTINGS_TOOL_SECTIONS.has(section) ? '#' + section : '#settings';
}

let activeView = viewFromHash();
let activeSettingsSection = settingsSectionFromHash() || 'global';
let settingsFormSection = 'global';
let settingsFormSourceText = 'loading settings';
let settingsFormMetaText = 'Values stored in NVS, also validated by the firmware.';

let cameraMode = 'live';

function logsSectionActive() {
  return activeView === 'settings' && activeSettingsSection === 'logs';
}

function actionsSectionActive() {
  return activeView === 'settings' && activeSettingsSection === 'actions';
}

function liveModeActive() {
  return activeView === 'camera' && cameraMode === 'live';
}

function snapshotModeActive() {
  return activeView === 'camera' && cameraMode === 'snapshot';
}

function snapshotRequestActive() {
  return (snapshotModeActive() && feedLoading) || Boolean(overviewPreviews.inFlight);
}

function settingsDocumentTitle() {
  const tool = SETTINGS_TOOL_SECTIONS.get(activeSettingsSection);
  return (tool ? tool.title : 'Settings') + ' · Camera Watchdog';
}
let selectedCameraId = 0;
let cameraSelectionManual = false;
let cameraRegistry = [];
let registryLoading = null;
let settingsLoadedKey = '';
let settingsValues = null;
let settingsCameraPasswordConfigured = false;
let settingsRequestGeneration = 0;
let settingsDirty = false;
let persistentLogsNextOffset = 0;
const GLOBAL_SETTING_FIELDS = new Set(["cameraAlertLedBlinkIntervalMs", "discoveryTimeoutMs", "emailRetryIntervalMs", "enableAutomaticSubnetScan", "enableCameraAlertLed", "localHostname", "rediscoveryIntervalMs", "scanConnectTimeoutMs", "statusRefreshIntervalMs", "subnetScanIntervalMs", "wifiConnectTimeoutMs"]);
const PRESERVED_SETTING_FIELDS = ['wifiConnectTimeoutMs', 'emailRetryIntervalMs', 'cameraAlertLedBlinkIntervalMs', 'snapshotResolveRetryIntervalMs', 'snapshotFailuresBeforeReresolve'];
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
let factoryResetPending = false;
let factoryResetResult = '';
let factoryResetEpoch = 0;
let factoryResetAcceptedAt = 0;
let factoryResetUptimeAtAcceptance = null;
let factoryResetSawScheduled = false;
let lastDeviceUptimeSeconds = null;
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
    : timestamp.toLocaleString('en-US') + match[2];
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
    ? ['Resources stable', 'var(--ok)', 'rgba(16,185,129,.14)', 'rgba(16,185,129,.32)']
    : heapTone === 'warn'
      ? ['Heap needs attention', 'var(--warn)', 'rgba(245,158,11,.14)', 'rgba(245,158,11,.32)']
      : ['Heap low', 'var(--bad)', 'rgba(244,63,94,.14)', 'rgba(244,63,94,.32)'];
  const resourceHero = $('resourcesHero');
  resourceHero.style.setProperty('--sc', toneValues[1]);
  resourceHero.style.setProperty('--sbg', toneValues[2]);
  resourceHero.style.setProperty('--sbd', toneValues[3]);
  $('resourcesState').textContent = toneValues[0];
  $('resourcesChip').textContent = resources.chip.model + ' rev. ' + resources.chip.revision;
  $('resourcesMeta').textContent = 'Last reset: ' + resources.chip.resetReason.replace(/_/g, ' ');
  $('resourcesArc').style.setProperty('--p', heapPercent);
  $('resourcesHeapPercent').textContent = Math.round(heapPercent) + '%';

  $('r-heap').textContent = formatBytes(heap.freeBytes);
  $('r-heap').className = 'card-v ' + heapTone;
  $('r-heap-meta').textContent = formatBytes(heap.totalBytes - heap.freeBytes)
    + ' used of ' + formatBytes(heap.totalBytes);
  $('r-heap-meter').style.width = Math.max(0, Math.min(100, heapPercent)) + '%';
  $('r-heap-meter').className = heapTone;
  pip('p-rheap', heapTone);

  const fragmentation = heap.freeBytes
    ? Math.max(0, 100 - heap.largestBlockBytes / heap.freeBytes * 100)
    : 100;
  const blockTone = fragmentation <= 25 ? 'ok' : fragmentation <= 50 ? 'warn' : 'bad';
  $('r-block').textContent = formatBytes(heap.largestBlockBytes);
  $('r-block').className = 'card-v ' + blockTone;
  $('r-block-meta').textContent = 'estimated fragmentation at ' + Math.round(fragmentation) + '%';
  pip('p-rblock', blockTone);

  const minimumPercent = heap.totalBytes ? heap.minimumFreeBytes / heap.totalBytes * 100 : 0;
  const minimumTone = resourceTone(minimumPercent);
  $('r-min').textContent = formatBytes(heap.minimumFreeBytes);
  $('r-min').className = 'card-v ' + minimumTone;
  $('r-min-meta').textContent = Math.round(minimumPercent) + '% of total heap';
  pip('p-rmin', minimumTone);

  const flash = resources.flash;
  $('r-flash').textContent = formatBytes(flash.sketchBytes);
  $('r-flash-meta').textContent = formatBytes(flash.freeSketchBytes)
    + ' free in the partition · flash ' + formatBytes(flash.chipBytes);

  const psram = resources.psram;
  if (psram.totalBytes) {
    const psramPercent = psram.freeBytes / psram.totalBytes * 100;
    const psramTone = resourceTone(psramPercent);
    $('r-psram').textContent = formatBytes(psram.freeBytes);
    $('r-psram').className = 'card-v ' + psramTone;
    $('r-psram-meta').textContent = 'free of ' + formatBytes(psram.totalBytes)
      + ' · largest block ' + formatBytes(psram.largestBlockBytes);
    pip('p-rpsram', psramTone);
  } else {
    $('r-psram').textContent = 'not available';
    $('r-psram').className = 'card-v info';
    $('r-psram-meta').textContent = 'this board does not expose PSRAM';
    pip('p-rpsram', 'info');
  }

  const onvif = d.onvif || {};
  const onvifReady = Boolean(onvif.catalogResolved && !onvif.usedMediaFallback);
  $('r-onvif').textContent = (onvif.serviceCount || 0) + ' services';
  const onvifTone = !onvif.catalogResolved ? 'info' : onvifReady ? 'ok' : 'warn';
  $('r-onvif').className = 'card-v ' + onvifTone;
  $('r-onvif-meta').textContent = !onvif.catalogResolved
    ? 'waiting for the GetServices query'
    : onvif.usedMediaFallback
      ? 'Conventional Media1 in use · ' + (onvif.lastResult || 'catalog unavailable')
      : (onvif.lastLatencyMs || 0) + ' ms · endpoints validated on the camera IP';
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
    ? 'waiting for a sample'
    : !cameraClock.referenceSynchronized
      ? 'NTP reference unavailable'
      : !cameraClock.utcValid
      ? 'inconclusive sample'
      : absoluteClockDriftMs < 1000
        ? 'aligned'
        : (absoluteClockDriftMs / 1000).toFixed(1) + ' s '
          + (clockDriftMs > 0 ? 'ahead' : 'behind');
  $('r-clock').className = 'card-v ' + clockTone;
  const cameraClockResult = cameraClock.referenceSynchronized
    ? (cameraClock.lastResult || 'waiting for a clock observation')
    : 'ESP32 NTP reference unavailable; drift not evaluated';
  $('r-clock-meta').textContent = cameraClockResult
    + ' · ' + (cameraClock.dateTimeType || 'mode not reported')
    + ' · ' + (cameraClock.reportedTimeZone || 'timezone not reported')
    + (cameraClock.referenceSynchronized ? ' · ESP32 NTP reference' : '');
  pip('p-rclock', clockTone);

  const events = onvif.events || {};
  const eventsServiceAvailable = Boolean(
    onvif.services && onvif.services.events && onvif.services.events.available
  );
  const eventKinds = [
    events.motion && 'motion',
    events.tamper && 'tamper',
    events.signalLoss && 'signal loss',
    events.imageQuality && 'image quality'
  ].filter(Boolean);
  const eventsTone = events.propertiesLoaded
    ? 'ok'
    : !onvif.catalogResolved || !eventsServiceAvailable ? 'info' : 'warn';
  $('r-events').textContent = events.propertiesLoaded
    ? (events.topicCount || 0) + ' topics'
    : !onvif.catalogResolved
      ? 'waiting for catalog'
      : eventsServiceAvailable ? 'not queried' : 'not advertised';
  $('r-events').className = 'card-v ' + eventsTone;
  $('r-events-meta').textContent = events.propertiesLoaded
    ? (eventKinds.join(' · ') || 'no monitorable topics identified')
    : !eventsServiceAvailable && onvif.catalogResolved
      ? 'the camera did not advertise the Events service'
      : 'GetEventProperties has not returned data yet';
  pip('p-revents', eventsTone);

  resourceHistory.heap.push(heapPercent);
  resourceHistory.rssi.push(d.wifi.connected ? d.wifi.rssi : -100);
  if (resourceHistory.heap.length > HISTORY_LIMIT) resourceHistory.heap.shift();
  if (resourceHistory.rssi.length > HISTORY_LIMIT) resourceHistory.rssi.shift();
  $('heapHistory').setAttribute('points', historyPoints(resourceHistory.heap, 0, 100));
  $('rssiHistory').setAttribute('points', historyPoints(resourceHistory.rssi, -100, -30));
  $('historyHeap').textContent = Math.round(heapPercent) + '%';
  $('historyRssi').textContent = d.wifi.connected ? d.wifi.rssi + ' dBm' : 'disconnected';
}

const LOG_LEVEL_LABELS = { info: 'Info', warning: 'Warning', error: 'Error' };
const LOG_OUTCOME_LABELS = {
  info: 'information', started: 'started', success: 'success', failure: 'failure', skipped: 'skipped'
};
const LOG_EVENT_LABELS = {
  system_boot: 'ESP32 startup', system_restart: 'ESP32 restart',
  wifi_connect: 'Wi-Fi connection', wifi_disconnect: 'Wi-Fi disconnection', wifi_reconnect: 'Wi-Fi reconnection',
  camera_state: 'Camera state', incident: 'Incident', health_check: 'Health check',
  discovery_probe: 'WS-Discovery send', discovery_response: 'WS-Discovery response',
  discovery_result: 'Discovery result', subnet_scan: 'Network scan',
  tcp_scan: 'TCP attempt', onvif_probe: 'ONVIF probe', onvif_soap: 'ONVIF/SOAP request',
  snapshot_http: 'Snapshot request', snapshot_transfer: 'JPEG transfer', rtsp_probe: 'RTSP probe',
  email_http: 'Email sending', settings_saved: 'Settings saved', settings_reset: 'Defaults restored',
  admin_action: 'Admin action', camera_reboot: 'Camera reboot', ptz_command: 'PTZ command',
  sntp_sync: 'NTP synchronization', runtime_context: 'Firmware context',
  settings_context: 'Settings context', camera_target_load: 'Camera target loaded',
  camera_target_save: 'Camera target saved', logs_cleared: 'Log history cleared'
};

function persistentLogFlags(entry) {
  const flags = entry.flags || {};
  return [
    flags.outbound && 'outbound',
    flags.connectionReused && 'connection reused',
    flags.authenticationRetry && 'authentication',
    flags.manual && 'manual',
    flags.scheduled && 'automatic',
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
    return entry.status > 0 ? 'errno ' + entry.status : 'socket error';
  }
  return String(entry.status);
}

function persistentLogValueLabel(entry) {
  if (!entry.value || entry.timeoutMs) return '';
  if (entry.event === 'runtime_context') return 'defaults rev. ' + entry.value;
  if (entry.event === 'logs_cleared') return entry.value + ' records removed';
  if (entry.event === 'camera_target_load' || entry.event === 'camera_target_save') {
    return 'RTSP ' + entry.value;
  }
  if (entry.event === 'discovery_response') return entry.value + ' bytes';
  return 'value ' + entry.value;
}

function persistentLogMatches(entry) {
  const level = $('logsLevel').value;
  const category = $('logsCategory').value;
  const scope = $('logsScope').value;
  const query = $('logsSearch').value.trim().toLocaleLowerCase('en-US');
  if (level && entry.level !== level) return false;
  if (category && entry.category !== category) return false;
  const routineTcpFailure = entry.event === 'tcp_scan' && entry.outcome === 'info' &&
    ['port_closed', 'connection_refused', 'connect_timeout'].includes(entry.detail);
  if (scope === 'diagnostic' && routineTcpFailure) return false;
  if (!query) return true;
  const searchable = [
    entry.id, entry.bootId, entry.contextId, entry.cameraId, entry.cameraName, entry.timestamp, entry.timestampUtc,
    entry.level, entry.category, entry.event, entry.outcome, entry.targetIp, entry.targetPort,
    entry.status, entry.timeoutMs, entry.detail, persistentLogFlags(entry),
    LOG_EVENT_LABELS[entry.event]
  ].join(' ').toLocaleLowerCase('en-US');
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
      entry.contextId ? 'context ' + entry.contextId : '',
      entry.timeSynchronized ? '' : 'no NTP time',
      'uptime ' + entry.uptimeMs + ' ms'
    ].filter(Boolean).join(' · ');
    timestamp.append(document.createElement('br'), timestampMeta);

    const levelCell = document.createElement('td');
    const level = document.createElement('span');
    level.className = 'logs-level ' + entry.level;
    level.textContent = LOG_LEVEL_LABELS[entry.level] || entry.level;
    levelCell.append(level);
    row.append(levelCell);
    appendPersistentLogCell(row, entry.cameraName || (entry.cameraId ? 'Camera ' + entry.cameraId : 'System'));

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
        : entry.targetPort ? 'external:' + entry.targetPort : 'local',
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
    ? 'None of the loaded records match the filters.'
    : 'No persistent records available.';
  $('logsLoadMore').hidden = !persistentLogsHasMore;
  $('logsStatus').className = 'form-result';
  $('logsStatus').textContent = persistentLogsLoaded
    ? entries.length + ' shown · ' + persistentLogs.length + ' loaded of ' + persistentLogsTotal
    : 'Loading the persistent history…';
}

function renderPersistentLogStats(payload) {
  const total = Number(payload.total) || 0;
  const capacity = Number(payload.capacity) || 0;
  const storage = payload.storage || {};
  const countPercent = capacity ? total / capacity * 100 : 0;
  const storagePercent = storage.totalBytes ? storage.usedBytes / storage.totalBytes * 100 : 0;
  const integrityErrors = (Number(storage.dropped) || 0) + (Number(storage.writeErrors) || 0);

  $('logsCount').textContent = total.toLocaleString('en-US');
  $('logsCountMeta').textContent = capacity
    ? Math.round(countPercent) + '% of the circular limit of ' + capacity.toLocaleString('en-US')
    : 'storage unavailable';
  $('logsCountMeter').style.width = Math.min(100, countPercent) + '%';
  $('logsCountMeter').className = countPercent >= 90 ? 'warn' : '';

  $('logsStorage').textContent = formatBytes(Number(storage.usedBytes) || 0);
  $('logsStorageMeta').textContent = formatBytes(Number(storage.fileBytes) || 0)
    + ' in the file · ' + formatBytes(Number(storage.totalBytes) || 0) + ' available in the partition';
  $('logsStorageMeter').style.width = Math.min(100, storagePercent) + '%';
  $('logsStorageMeter').className = storagePercent >= 90 ? 'warn' : '';

  $('logsIntegrity').textContent = integrityErrors ? integrityErrors + ' occurrences' : 'intact';
  $('logsIntegrity').className = 'card-v ' + (integrityErrors ? 'bad' : 'ok');
  $('logsIntegrityMeta').textContent = (Number(storage.dropped) || 0) + ' dropped · '
    + (Number(storage.writeErrors) || 0) + ' write errors · '
    + (Number(storage.pending) || 0) + ' pending';
  $('logsState').textContent = payload.ready ? 'persistent history active' : 'logs partition unavailable';
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
    const offset = reset ? 0 : persistentLogsNextOffset;
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
    persistentLogsNextOffset = Number(payload.nextOffset);
    persistentLogsTotal = Number(payload.total) || 0;
    persistentLogsHasMore = Boolean(payload.hasMore) && persistentLogs.length < persistentLogsTotal;
    persistentLogsLoaded = true;
    renderPersistentLogStats(payload);
    renderPersistentLogs();
  } catch (error) {
    $('logsState').textContent = 'failed to read the logs';
    $('logsStatus').textContent = 'Could not load the logs: ' + error.message;
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
    meta.textContent = 'response in ' + latency + ' ms';
  } else if (tcp) {
    value.textContent = 'port open';
    value.className = 'card-v warn';
    pip('p-' + prefix, 'warn');
    meta.textContent = 'connected in ' + latency + ' ms, no protocol response';
  } else {
    value.textContent = 'unavailable';
    value.className = 'card-v bad';
    pip('p-' + prefix, 'bad');
    meta.textContent = 'no TCP connection';
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
  value.textContent = probes.onvifHttp ? 'probe skipped' : 'waiting';
  value.className = 'card-v info';
  pip('p-rtsp', 'info');
  meta.textContent = probes.onvifHttp
    ? 'ONVIF responded; the raw RTSP probe was not needed'
    : 'the RTSP probe has not run yet';
}

function setImageProbe(snapshot, recovery) {
  const value = $('v-image'), meta = $('m-image');

  if (recovery && recovery.graceActive) {
    value.textContent = 'restarting';
    value.className = 'card-v warn';
    pip('p-image', 'warn');
    meta.textContent = 'waiting for the camera for another ' + duration(recovery.graceRemainingSeconds);
    return;
  }

  if (!snapshot || !snapshot.attempted) {
    value.textContent = 'waiting';
    value.className = 'card-v info';
    pip('p-image', 'info');
    meta.textContent = 'the first JPEG has not been tested yet';
    return;
  }

  if (!snapshot.conclusive) {
    value.textContent = 'test deferred';
    value.className = 'card-v info';
    pip('p-image', 'info');
    meta.textContent = snapshot.detail || 'waiting for a conclusive JPEG sample';
    return;
  }

  if (snapshot.healthy) {
    value.textContent = 'valid JPEG';
    value.className = 'card-v ok';
    pip('p-image', 'ok');
    meta.textContent = formatBytes(snapshot.bytesRead) + ' in ' + snapshot.latencyMs
      + ' ms' + (snapshot.profile ? ' · profile ' + snapshot.profile : '');
    return;
  }

  value.textContent = snapshot.httpResponded ? 'no image' : 'unavailable';
  value.className = 'card-v bad';
  pip('p-image', 'bad');
  meta.textContent = snapshot.detail || 'no complete JPEG was received';
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
// The preview intervals are updated from the firmware JSON.

function updateFeedHealthyMeta() {
  if (!feedHealthyMetaPrefix) return;
  $('feedMeta').textContent = feedHealthyMetaPrefix + ' · '
    + feedDeliveredFrames + ' frames delivered';
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
  $('feedStamp').textContent = 'preview paused for PTZ control';
}

function resumeFeedAfterPtz(delay) {
  feedPausedForPtz = false;
  if (!document.hidden && snapshotModeActive()) scheduleFeed(delay || 0);
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
  if (document.hidden || !snapshotModeActive() || feedPausedForPtz || feedActivationPending) return;
  feedTimer = setTimeout(() => {
    feedTimer = null;
    loadFeed();
  }, delay);
}

function loadFeed() {
  if (document.hidden || !snapshotModeActive() || feedLoading || feedPausedForPtz || feedActivationPending) return;
  if (statusLoading) {
    scheduleFeed(250);
    return;
  }
  if (cameraRecovering) {
    $('cameraFeed').classList.remove('ready');
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'The camera is restarting; the preview will resume automatically…';
    $('feedStamp').textContent = 'automatic recovery in progress';
    scheduleFeed(5000);
    return;
  }
  if (!cameraKnown) {
    $('feedState').classList.remove('hidden');
    $('feedState').textContent = 'Waiting for the camera to be found…';
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
    $('feedState').textContent = 'The image took too long; trying again…';
    $('feedStamp').textContent = 'preview temporarily unavailable';
    scheduleFeed(Math.min(8000, 1000 * Math.pow(2, Math.min(feedFailures, 3))));
  }, snapshotWatchdogMs());
  frame.onload = () => {
    if (requestGeneration !== feedRequestGeneration) return;
    clearTimeout(feedWatchdogTimer);
    feedWatchdogTimer = null;
    feedLoading = false;
    feedFailures = 0;
    frame.classList.add('ready');
    $('feedState').classList.add('hidden');
    $('feedStamp').textContent = feedPausedForPtz
      ? 'preview paused for PTZ control'
      : 'updated ' + new Date().toLocaleTimeString('en-US');
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
    $('feedState').textContent = 'Could not update the image; trying again…';
    $('feedStamp').textContent = 'preview temporarily unavailable';
    scheduleFeed(Math.min(8000, 1000 * Math.pow(2, Math.min(feedFailures, 3))));
  };
  frame.src = cameraUrl('/api/camera/snapshot?t=' + Date.now());
}

const cameraStage = $('cameraStage');
const feedFsBtn = $('feedFsBtn');

function isNativeFullscreen() {
  return document.fullscreenElement === cameraStage || document.webkitFullscreenElement === cameraStage;
}

function updateFsButton() {
  const active = isNativeFullscreen() || cameraStage.classList.contains('fs-fallback');
  feedFsBtn.setAttribute('aria-pressed', active ? 'true' : 'false');
  feedFsBtn.setAttribute('aria-label', active ? 'Exit full screen' : 'View full screen');
  feedFsBtn.title = active ? 'Exit full screen' : 'Full screen';
  feedFsBtn.querySelector('.ic-expand').hidden = active;
  feedFsBtn.querySelector('.ic-collapse').hidden = !active;
}

function exitFallbackFullscreen() {
  cameraStage.classList.remove('fs-fallback');
  document.body.classList.remove('fs-locked');
  updateFsButton();
}

function toggleFullscreen() {
  if (isNativeFullscreen()) {
    (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    return;
  }
  if (cameraStage.classList.contains('fs-fallback')) {
    exitFallbackFullscreen();
    return;
  }
  const request = cameraStage.requestFullscreen || cameraStage.webkitRequestFullscreen;
  if (!request) {
    cameraStage.classList.add('fs-fallback');
    document.body.classList.add('fs-locked');
    updateFsButton();
    return;
  }
  Promise.resolve(request.call(cameraStage)).catch(() => {
    cameraStage.classList.add('fs-fallback');
    document.body.classList.add('fs-locked');
    updateFsButton();
  });
}

feedFsBtn.addEventListener('click', toggleFullscreen);
$('cameraFeed').addEventListener('click', () => { if (cameraKnown) toggleFullscreen(); });
$('liveVideo').addEventListener('dblclick', toggleFullscreen);
document.addEventListener('fullscreenchange', updateFsButton);
document.addEventListener('webkitfullscreenchange', updateFsButton);
document.addEventListener('keydown', e => {
  if (e.key === 'Escape' && cameraStage.classList.contains('fs-fallback')) exitFallbackFullscreen();
});

function showCameraSource() {
  cameraStage.dataset.mode = cameraMode;
  document.querySelectorAll('#cameraView [data-source]').forEach(element => {
    element.hidden = element.dataset.source !== cameraMode;
  });
  document.querySelectorAll('[data-camera-mode]').forEach(button => {
    button.setAttribute('aria-pressed', button.dataset.cameraMode === cameraMode ? 'true' : 'false');
  });
  $('stageTitle').textContent = cameraMode === 'live' ? 'Live video' : 'Snapshot preview';
}

function leaveSnapshotMode() {
  feedPausedForPtz = false;
  feedActivationPending = false;
  clearTimeout(feedTimer);
  feedTimer = null;
  cancelFeedLoad(true);
  $('cameraFeed').classList.remove('ready');
}

// Starts the selected video source of the Camera view and stops the other one.
function applyCameraMode() {
  showCameraSource();
  if (activeView !== 'camera') return;
  if (cameraMode === 'snapshot') {
    stopLive();
    feedPausedForPtz = false;
    feedActivationPending = true;
    $('feedState').textContent = cameraRecovering
      ? 'The camera is restarting…'
      : cameraKnown ? 'Updating the image…' : 'Waiting for the camera to be found…';
    $('feedState').classList.remove('hidden');
    $('feedStamp').textContent = cameraRecovering
      ? 'automatic recovery in progress'
      : 'waiting for image';
  } else {
    leaveSnapshotMode();
    startLive();
  }
  loadSettings(false)
    .then(() => refresh(true))
    .finally(() => {
      feedActivationPending = false;
      if (snapshotModeActive()) scheduleFeed(0);
    });
}

document.querySelectorAll('[data-camera-mode]').forEach(button => {
  button.addEventListener('click', () => {
    const mode = button.dataset.cameraMode === 'snapshot' ? 'snapshot' : 'live';
    if (mode === cameraMode) return;
    cameraMode = mode;
    applyCameraMode();
  });
});

function activateSettingsSection(name) {
  if (settingsBusy || !$('adminConfirmModal').hidden) {
    if (activeView === 'settings') history.replaceState(null, '', settingsHashFor(activeSettingsSection));
    return;
  }
  const available = [...document.querySelectorAll('[data-settings-section-panel]')];
  const target = available.find(panel => panel.dataset.settingsSectionPanel === name);
  if (!target) return;

  const tool = SETTINGS_TOOL_SECTIONS.has(name);
  const nextFormSection = tool ? settingsFormSection : name;
  const scopeChanged = (nextFormSection === 'global') !== (settingsFormSection === 'global');
  if (scopeChanged && settingsDirty && !window.confirm('Discard the unsaved changes before changing the scope?')) return;
  if (scopeChanged) { settingsDirty = false; settingsLoaded = false; settingsLoadedKey = ''; settingsValues = null; settingsCameraPasswordConfigured = false; }
  activeSettingsSection = name;
  settingsFormSection = nextFormSection;
  available.forEach(panel => {
    panel.hidden = panel !== target;
  });
  document.querySelectorAll('.settings-tab[data-settings-section]').forEach(tab => {
    const selected = tab.dataset.settingsSection === name;
    tab.setAttribute('aria-selected', selected ? 'true' : 'false');
    tab.tabIndex = selected ? 0 : -1;
  });
  $('settingsSavebar').hidden = tool;
  updateSettingsScope();
  if (activeView !== 'settings') return;
  document.title = settingsDocumentTitle();
  const hash = settingsHashFor(name);
  if (location.hash !== hash) history.replaceState(null, '', hash);
  if (name === 'logs') {
    loadPersistentLogs(true);
  } else if (tool) {
    scheduleStatusRefresh(0);
  } else if (scopeChanged || !settingsLoaded) {
    loadSettings(true);
  }
}

document.querySelectorAll('.settings-tab[data-settings-section]').forEach(tab => {
  tab.addEventListener('click', () => activateSettingsSection(tab.dataset.settingsSection));
});

function validateSettingsForm() {
  Object.values(SETTINGS_FIELDS).forEach(descriptor => $(descriptor.id).setCustomValidity(''));
  $('cfgCameraPassword').setCustomValidity('');
  [['cfgCameraUsername', 64], ['cfgCameraPassword', 128]].forEach(([id, limit]) => {
    const input = $(id);
    let error = '';
    if (!input.disabled) {
      if (new TextEncoder().encode(input.value).length > limit) {
        error = 'The limit is ' + limit + ' bytes; accented characters can take more than one byte.';
      } else if (/[\u0000-\u001F\u007F]/.test(input.value)) {
        error = 'Do not use control characters.';
      } else if (id === 'cfgCameraUsername' && input.value.includes(':')) {
        error = 'The username cannot contain a colon.';
      }
    }
    input.setCustomValidity(error);
  });
  const relationships = [
    ['cfgFailuresBeforeOffline', 'cfgFailuresBeforeRediscovery', 1, 'The offline threshold must be at least the rediscovery threshold.'],
    ['cfgVideoFailuresBeforeReboot', 'cfgVideoFailuresBeforeAlert', 1, 'The restart threshold must be at least the image alert threshold.'],
    ['cfgRebootWindow', 'cfgRebootCooldown', 60, 'The counting window must cover the minimum time between restarts.'],
    ['cfgFeedTarget', 'cfgFeedMinimum', 1, 'The snapshot cycle must be at least the minimum snapshot pause.']
  ];
  relationships.forEach(([id, minimumId, multiplier, message]) => {
    const input = $(id);
    const minimum = $(minimumId);
    if (!input.disabled && input.value !== '' && minimum.value !== ''
      && Number(input.value) * multiplier < Number(minimum.value)) input.setCustomValidity(message);
  });
  const username = $('cfgCameraUsername');
  const passwordAction = $('cfgCameraPasswordAction').value;
  const passwordNeedsUsername = passwordAction === 'keep' ? settingsCameraPasswordConfigured
    : passwordAction === 'replace' && $('cfgCameraPassword').value.length > 0;
  if (!username.disabled && username.validity.valid && username.value === '' && passwordNeedsUsername) {
    username.setCustomValidity('Enter the camera username or choose Remove username and password.');
  }
  const invalid = $('settingsForm').querySelector(':invalid');
  if (!invalid) return true;
  const panel = invalid.closest('[data-settings-section-panel]');
  if (panel) activateSettingsSection(panel.dataset.settingsSectionPanel);
  let disclosure = invalid.closest('details');
  while (disclosure) {
    disclosure.open = true;
    disclosure = disclosure.parentElement.closest('details');
  }
  invalid.focus();
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
  enableAutomaticSubnetScan: { id: 'cfgAutoScan', kind: 'bool' },
  discoveryTimeoutMs: { id: 'cfgDiscoveryTimeout', scale: 1000 },
  rediscoveryIntervalMs: { id: 'cfgRediscoveryInterval', scale: 1000 },
  subnetScanIntervalMs: { id: 'cfgSubnetScanInterval', scale: 60000 },
  enableEmailAlerts: { id: 'cfgEmailAlerts', kind: 'bool' },
  emailOnIpChange: { id: 'cfgEmailIpChange', kind: 'bool' },
  tcpConnectTimeoutMs: { id: 'cfgTcpTimeout' },
  serviceResponseTimeoutMs: { id: 'cfgServiceTimeout' },
  scanConnectTimeoutMs: { id: 'cfgScanTimeout' },
  snapshotHttpTimeoutMs: { id: 'cfgSnapshotTimeout', scale: 1000 },
  enableCameraAlertLed: { id: 'cfgLedEnabled', kind: 'bool' },
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

function updateFactoryResetState(payload, responseEpoch) {
  const uptime = Number(payload.uptimeSeconds);
  if (factoryResetPending && factoryResetResult !== 'completed' && responseEpoch === factoryResetEpoch) {
    if (payload.factoryResetState === 'scheduled') {
      factoryResetSawScheduled = true;
    } else if (payload.factoryResetState === 'failed') {
      factoryResetPending = false;
      factoryResetResult = 'failed';
      settingsBusy = false;
      setMutationControlsEnabled(Boolean(settingsWriteToken));
      setAdminActionStatus(payload.adminAction && payload.adminAction.lastResult
        ? payload.adminAction.lastResult : 'Could not erase the ESP32 data.', 'bad');
    } else if (payload.factoryResetState === 'idle') {
      const elapsedSeconds = (Date.now() - factoryResetAcceptedAt) / 1000;
      const rebootObserved = factoryResetSawScheduled || (factoryResetUptimeAtAcceptance !== null
        && Number.isFinite(uptime) && uptime + 2 < factoryResetUptimeAtAcceptance + elapsedSeconds);
      if (rebootObserved) {
        factoryResetResult = 'completed';
        setAdminActionStatus('The ESP32 restarted after the request. Reload the dashboard and check the registrations and settings.', 'ok');
      }
    }
  }
  if (Number.isFinite(uptime)) lastDeviceUptimeSeconds = uptime;
}

function setPtzActionStatus(message, tone) {
  const target = $('ptzActionStatus');
  const isProblem = tone === 'bad';
  target.hidden = !isProblem;
  target.textContent = isProblem ? localizeTimestampText(message) : '';
  target.className = 'form-result' + (isProblem ? ' bad' : '');
}

function setMutationControlsEnabled(enabled) {
  updateSettingsScope();
  enabled = enabled && !settingsBusy && !factoryResetPending;
  $('settingsWifiLink').setAttribute('aria-disabled', settingsBusy || factoryResetPending ? 'true' : 'false');
  const scope = currentSettingsScope();
  const settingsReady = enabled && settingsLoaded && settingsValues
    && settingsLoadedKey === scope + '/' + (scope === 'global' ? 0 : selectedCameraId);
  $('settingsSave').disabled = !settingsReady;
  $('settingsReset').disabled = !settingsReady;
  $('forgetCameraOpen').disabled = !enabled || !selectedCameraId;
  $('factoryResetOpen').disabled = !enabled;
  document.querySelectorAll('[data-admin-action]').forEach(button => {
    const needsCamera = ['health-check','reboot-camera','reset-reboot-limiter','reboot-all-cameras'].includes(button.dataset.adminAction);
    button.disabled = !enabled || (needsCamera && !selectedCameraId);
  });
  document.querySelectorAll('[data-ptz-preset-token]').forEach(button => {
    button.disabled = !enabled || !ptzControlsEnabled;
  });
}

function settingsAuthorizationReady(source) {
  const scope = currentSettingsScope();
  if (source === 'settings' && (!settingsLoaded || !settingsValues
    || settingsLoadedKey !== scope + '/' + (scope === 'global' ? 0 : selectedCameraId))) {
    if (settingsDirty) {
      setSettingsStatus('These settings are no longer loaded for this selection. Open Device or choose another camera to discard the pending edits and reload.', 'bad');
      return false;
    }
    setSettingsStatus('Load the settings for this device or camera before saving or restoring defaults.', 'bad');
    loadSettings(true);
    return false;
  }
  if (!settingsWriteToken) {
    if (source === 'actions') {
      setAdminActionStatus('Actions stay blocked until the authorization is loaded.', 'bad');
    } else if (source === 'ptz') {
      setPtzActionStatus('PTZ control is waiting for authorization from the ESP32.', 'bad');
    } else {
      setSettingsStatus('The settings have not been loaded yet. Wait and try again.', 'bad');
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
}

function updateCameraCredentialFields() {
  const action = $('cfgCameraPasswordAction').value;
  const blocked = !settingsLoaded || settingsBusy || factoryResetPending || currentSettingsScope() === 'global';
  $('cfgCameraUsername').disabled = blocked || action === 'clear';
  $('cfgCameraUsername').setCustomValidity('');
  $('cfgCameraPassword').disabled = blocked || action !== 'replace';
  $('cfgCameraPassword').setCustomValidity('');
  if (action !== 'replace') $('cfgCameraPassword').value = '';
}

$('cfgCameraPasswordAction').addEventListener('change', updateCameraCredentialFields);

function populateSettings(payload) {
  const values = payload.values || {};
  settingsValues = { ...values };
  settingsCameraPasswordConfigured = Boolean(payload.security && payload.security.cameraPasswordConfigured);
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
  $('cfgCameraPasswordStatus').textContent = settingsCameraPasswordConfigured
    ? 'Password configured. Saving other settings keeps the current password.'
    : 'No password configured.';
  updateCameraCredentialFields();
  settingsFormSourceText = payload.source === 'nvs'
    ? 'settings saved in NVS'
    : 'factory default values';
  settingsFormMetaText = localizeTimestampText(payload.lastResult || 'settings loaded');

  feedMinimumIntervalMs = Number(values.feedMinIntervalMs) || feedMinimumIntervalMs;
  feedTargetCycleMs = Number(values.feedTargetCycleMs) || feedTargetCycleMs;
  statusRefreshIntervalMs = Number(values.statusRefreshIntervalMs) || statusRefreshIntervalMs;
  settingsLoaded = true;
  settingsDirty = false;
  settingsLoadedKey = payload.scope + '/' + payload.cameraId;
  updateSettingsEstimates();
  setMutationControlsEnabled(true);
  if (actionsSectionActive()) {
    if (!factoryResetResult) setAdminActionStatus('Authorization loaded. Choose an admin action.', 'ok');
  } else if (activeView === 'camera') {
    if (!ptzCatalog.command || Number(ptzCatalog.command.attempts) === 0) {
      setPtzActionStatus('PTZ control authorized. Open the list to choose a preset.', 'ok');
    }
  } else {
    setSettingsStatus('Settings loaded. Changes are only saved when you click Save.', 'ok');
  }
}

function loadSettings(force) {
  if (settingsDirty) return Promise.resolve();
  const scope = currentSettingsScope();
  const id = scope === 'global' ? 0 : selectedCameraId;
  const key = scope + '/' + id;
  if (settingsLoading && settingsLoadPromise && settingsLoadPromise.scopeKey === key) return settingsLoadPromise;
  if (settingsLoaded && settingsLoadedKey === key && !force) return Promise.resolve();
  const generation = ++settingsRequestGeneration;
  settingsLoading = true;
  settingsLoaded = false;
  settingsLoadedKey = '';
  settingsValues = null;
  settingsCameraPasswordConfigured = false;
  setMutationControlsEnabled(false);
  settingsLoadPromise = (async () => {
    try {
      const response = await fetch('/api/settings?scope=' + scope + (id ? '&cameraId=' + id : ''), { cache: 'no-store' });
      const payload = await response.json();
      if (generation !== settingsRequestGeneration) return;
      if (!response.ok || !payload.ok) throw new Error(payload.detail || 'HTTP ' + response.status);
      if (payload.scope !== currentSettingsScope() || (payload.scope === 'camera' && payload.cameraId !== selectedCameraId)) return;
      populateSettings(payload);
    } catch (error) {
      if (generation !== settingsRequestGeneration) return;
      setMutationControlsEnabled(false);
      setSettingsStatus('Failed to load: ' + error.message, 'bad');
      if (actionsSectionActive()) setAdminActionStatus('Failed to load authorization: ' + error.message, 'bad');
    } finally {
      if (generation === settingsRequestGeneration) { settingsLoading = false; settingsLoadPromise = null; }
    }
  })();
  settingsLoadPromise.scopeKey = key;
  return settingsLoadPromise;
}

function collectMutationParams(includeSettings, password) {
  const params = new URLSearchParams();
  params.set('writeToken', settingsWriteToken);
  const scope = currentSettingsScope();
  params.set('scope', scope);
  if (selectedCameraId) params.set('cameraId', String(selectedCameraId));
  if (settingsPasswordRequired) {
    params.set('adminPassword', password || '');
  }

  if (includeSettings) {
    if (!settingsLoaded || !settingsValues
      || settingsLoadedKey !== scope + '/' + (scope === 'global' ? 0 : selectedCameraId)) {
      throw new Error('Reload the settings for this device or camera before saving.');
    }
    PRESERVED_SETTING_FIELDS.forEach(key => {
      if (GLOBAL_SETTING_FIELDS.has(key) !== (scope === 'global')) return;
      if (!(key in settingsValues)) throw new Error('The settings response is incomplete. Reload before saving.');
      params.set(key, String(settingsValues[key]));
    });
    Object.entries(SETTINGS_FIELDS).forEach(([key, descriptor]) => {
      if (GLOBAL_SETTING_FIELDS.has(key) !== (scope === 'global')) return;
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
    if (scope === 'camera') {
      const passwordAction = $('cfgCameraPasswordAction').value;
      params.set('cameraPasswordAction', passwordAction);
      params.set('cameraPassword', passwordAction === 'replace' ? $('cfgCameraPassword').value : '');
      if (passwordAction === 'clear') params.set('cameraUsername', '');
    }
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

$('settingsWifiLink').addEventListener('click', event => {
  if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
  if (settingsBusy || factoryResetPending || !$('adminConfirmModal').hidden) {
    event.preventDefault();
    setSettingsStatus('Wait for the current operation to finish before opening Wi-Fi setup.', 'warn');
  } else if (settingsDirty && !window.confirm('Discard the unsaved changes before opening Wi-Fi setup?')) {
    event.preventDefault();
  }
});

$('settingsForm').addEventListener('input', event => {
  if (event.target.closest('.settings-tool')) return;
  settingsDirty = true;
  updateSettingsEstimates();
});
$('settingsForm').addEventListener('change', event => {
  if (event.target.closest('.settings-tool')) return;
  settingsDirty = true;
});
$('settingsForm').addEventListener('submit', event => {
  event.preventDefault();
  if (SETTINGS_TOOL_SECTIONS.has(activeSettingsSection)) return;
  if (settingsBusy || !validateSettingsForm() || !settingsAuthorizationReady('settings')) return;
  openConfirmModal(currentSettingsScope() === 'global' ? 'Save all device settings, including advanced values.' : 'Save all settings of ' + selectedCameraName() + ', including advanced values.', password => saveSettings(password));
});

async function saveSettings(password) {
  settingsBusy = true;
  setMutationControlsEnabled(false);
  setSettingsStatus('Validating and saving to NVS…', 'warn');
  try {
    const payload = await postForm('/api/settings', collectMutationParams(true, password));
    populateSettings(payload);
    setSettingsStatus(
      payload.scope === 'global' ? 'Global settings saved. Address: http://' + payload.values.localHostname + '.local/' : 'Camera settings saved and applied.',
      'ok'
    );
    refresh();
  } catch (error) {
    setSettingsStatus('Could not save: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

$('settingsReset').addEventListener('click', () => {
  if (settingsBusy || !settingsAuthorizationReady('settings')) return;
  openConfirmModal(currentSettingsScope() === 'global'
    ? 'Restore all device settings, including advanced values and the local address. The hostname returns to its firmware default. Camera settings and saved Wi-Fi networks are kept.'
    : 'Restore all settings of ' + selectedCameraName() + ', including advanced values, while keeping its username and password.', password => resetSettings(password));
});

async function resetSettings(password) {
  settingsBusy = true;
  setMutationControlsEnabled(false);
  setSettingsStatus('Restoring the factory values…', 'warn');
  try {
    const payload = await postForm('/api/settings/reset', collectMutationParams(false, password));
    populateSettings(payload);
    setSettingsStatus(
      payload.scope === 'global' ? 'Device defaults restored. Address: http://' + payload.values.localHostname + '.local/' : 'Camera defaults restored; credentials preserved.',
      'ok'
    );
    refresh();
  } catch (error) {
    setSettingsStatus('Could not restore: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

const ADMIN_ACTIONS = {
  'health-check': ['/api/actions/health-check', 'Manual check requested.', false],
  rediscover: ['/api/actions/rediscover', 'Rediscover all cameras and register any new ones found.', false],
  'reboot-all-cameras': ['/api/actions/reboot-all-cameras', 'Restart all registered cameras, respecting the individual limits.', true],
  'reboot-camera': ['/api/actions/reboot-camera', 'ONVIF reboot requested.', true],
  'reset-reboot-limiter': ['/api/actions/reset-reboot-limiter', 'The reboot limiter will be reset.', true],
  'clear-logs': ['/api/actions/clear-logs', 'All current records will be permanently removed and replaced by an audit marker.', true],
  'reboot-esp32': ['/api/actions/reboot-esp32', 'The ESP32 will restart.', true]
};

let pendingConfirm = null;
let pendingConfirmTrigger = null;
let pendingConfirmResumesFeed = false;
let pendingConfirmPhrase = '';

function openConfirmModal(description, onConfirm, resumeFeedOnCancel, requiredPhrase) {
  pendingConfirmTrigger = document.activeElement;
  pendingConfirm = onConfirm;
  pendingConfirmResumesFeed = Boolean(resumeFeedOnCancel);
  pendingConfirmPhrase = requiredPhrase || '';
  $('adminConfirmDesc').textContent = description + ' Confirm to continue.';
  $('adminConfirmPhraseField').hidden = !pendingConfirmPhrase;
  $('adminConfirmPhrase').value = '';
  $('adminConfirmPasswordField').hidden = !settingsPasswordRequired;
  $('adminConfirmPassword').value = '';
  $('adminConfirmError').hidden = true;
  $('adminConfirmModal').hidden = false;
  (pendingConfirmPhrase ? $('adminConfirmPhrase') : settingsPasswordRequired ? $('adminConfirmPassword') : $('adminConfirmSubmit')).focus();
}

function closeConfirmModal(resumePausedFeed) {
  const shouldResumeFeed = Boolean(resumePausedFeed && pendingConfirmResumesFeed);
  const trigger = pendingConfirmTrigger;
  $('adminConfirmModal').hidden = true;
  pendingConfirm = null;
  pendingConfirmTrigger = null;
  pendingConfirmResumesFeed = false;
  pendingConfirmPhrase = '';
  if (shouldResumeFeed) resumeFeedAfterPtz(0);
  if (trigger && trigger.isConnected && !trigger.disabled && trigger.getClientRects().length) trigger.focus();
}

async function executeAdminAction(action, password) {
  factoryResetResult = '';
  settingsBusy = true;
  setMutationControlsEnabled(false);
  setAdminActionStatus('Sending the action to the ESP32…', 'warn');
  try {
    const payload = await postForm(action[0], collectMutationParams(false, password));
    setAdminActionStatus(payload.message || action[1], 'ok');
    setTimeout(refresh, 700);
  } catch (error) {
    setAdminActionStatus('Action refused: ' + error.message, 'bad');
  } finally {
    settingsBusy = false;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

document.querySelectorAll('[data-admin-action]').forEach(button => {
  button.addEventListener('click', () => {
    const action = ADMIN_ACTIONS[button.dataset.adminAction];
    if (!action || settingsBusy || !settingsAuthorizationReady('actions')) return;
    const individual = ['health-check','reboot-camera','reset-reboot-limiter'].includes(button.dataset.adminAction);
    openConfirmModal(action[1] + (individual ? '\nCamera: ' + selectedCameraName() : '\nScope: global.'), password => executeAdminAction(action, password));
  });
});

async function executeFactoryReset(password) {
  factoryResetResult = 'requesting';
  settingsBusy = true;
  setMutationControlsEnabled(false);
  setAdminActionStatus('Requesting a full erase of the ESP32 data…', 'warn');
  try {
    const params = collectMutationParams(false, password);
    params.set('confirmReset', 'ERASE EVERYTHING');
    const payload = await postForm('/api/actions/factory-reset', params);
    factoryResetEpoch++;
    factoryResetAcceptedAt = Date.now();
    factoryResetUptimeAtAcceptance = lastDeviceUptimeSeconds;
    factoryResetSawScheduled = false;
    factoryResetPending = true;
    factoryResetResult = 'scheduled';
    setAdminActionStatus((payload.message || 'Erase scheduled.') + ' Wait for the restart and reload the dashboard.', 'warn');
  } catch (error) {
    factoryResetResult = 'failed';
    setAdminActionStatus('Erase refused: ' + error.message, 'bad');
  } finally {
    settingsBusy = factoryResetPending;
    setMutationControlsEnabled(Boolean(settingsWriteToken));
  }
}

$('factoryResetOpen').addEventListener('click', () => {
  if (settingsBusy || factoryResetPending || !settingsAuthorizationReady('actions')) return;
  if (settingsDirty) {
    setAdminActionStatus('Save or discard the pending changes before erasing the data.', 'bad');
    return;
  }
  openConfirmModal(
    'The ESP32 will permanently erase all camera registrations, saved settings, Wi-Fi networks saved from the dashboard, and logs, and then restart. The firmware and the built-in credentials stay. Active cameras can be discovered again after the restart.',
    password => executeFactoryReset(password),
    false,
    'ERASE EVERYTHING'
  );
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
  setPtzActionStatus('Sending preset ' + presetToken + ' to the camera…', 'warn');
  try {
    await waitForFeedIdle();
    const params = collectMutationParams(false, password);
    params.set('presetToken', presetToken);
    const payload = await postForm('/api/actions/goto-ptz-preset', params);
    commandQueued = true;
    setPtzActionStatus(payload.message || 'PTZ movement queued.', 'ok');
  } catch (error) {
    setPtzActionStatus('Movement refused: ' + error.message, 'bad');
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
    ? 'Daemon preset'
    : publishedName && publishedName !== token
      ? publishedName
      : 'Preset ' + token;
}

function filterPtzPresets(presets, query) {
  const normalizedQuery = String(query || '').trim().toLocaleLowerCase('en-US');
  if (!normalizedQuery) return presets;
  return presets.filter(preset => {
    const searchable = ptzPresetTitle(preset) + ' ' + String(preset.token || '');
    return searchable.toLocaleLowerCase('en-US').includes(normalizedQuery);
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
      ? (ptzCatalog.unavailableReason || 'PTZ discovery inconclusive.')
      : !ptzCatalog.presetsLoaded
        ? localizeTimestampText(ptzCatalog.lastResult || 'Querying the camera presets…')
        : 'The camera did not publish presets for this profile.';
    fragment.append(empty);
  } else if (!visiblePresets.length) {
    const empty = document.createElement('div');
    empty.className = 'ptz-empty';
    empty.textContent = 'No preset matches the given filter.';
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
        ? 'ONVIF token ' + token + ' · Daemon return confirmed'
        : 'ONVIF token ' + token;
      const button = document.createElement('button');
      button.className = 'btn' + (preset.daemon ? ' primary' : '');
      button.type = 'button';
      button.dataset.ptzPresetToken = token;
      button.dataset.ptzPresetLabel = titleText;
      button.disabled = !ptzControlsEnabled;
      button.textContent = 'Go';
      copy.append(title, detail);
      item.append(copy, button);
      fragment.append(item);
    });
  }

  list.replaceChildren(fragment);
  $('ptzVisibleCount').textContent = ptzPresets.length
    ? visiblePresets.length === ptzPresets.length
      ? plural(ptzPresets.length, 'preset available', 'presets available')
      : visiblePresets.length + ' of ' + ptzPresets.length + ' presets shown'
    : 'No preset loaded';
}

function openPtzPresetModal() {
  if ($('ptzMenuButton').disabled) return;
  pauseFeedForPtz();
  $('ptzPauseNote').hidden = cameraMode === 'live';
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
  const presetLabel = button.dataset.ptzPresetLabel || 'preset ' + presetToken;
  closePtzPresetModal(false);
  openConfirmModal(
    'Move the camera to ' + presetLabel + '.',
    password => executePtzPreset(presetToken, password),
    true
  );
});

$('adminConfirmCancel').addEventListener('click', () => closeConfirmModal(true));
$('adminConfirmModal').addEventListener('click', event => {
  if (event.target === $('adminConfirmModal')) closeConfirmModal(true);
});
document.addEventListener('keydown', event => {
  const confirmation = $('adminConfirmModal');
  if (event.key === 'Tab' && !confirmation.hidden) {
    const focusable = [...confirmation.querySelectorAll('button,input,select,textarea,a[href],[tabindex]')]
      .filter(element => !element.disabled && element.tabIndex >= 0 && element.getClientRects().length);
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (!first || !confirmation.contains(document.activeElement)
      || (event.shiftKey ? document.activeElement === first : document.activeElement === last)) {
      event.preventDefault();
      if (first) (event.shiftKey ? last : first).focus();
    }
    return;
  }
  if (event.key !== 'Escape') return;
  if (!$('adminConfirmModal').hidden) {
    closeConfirmModal(true);
  } else if (!$('ptzPresetModal').hidden) {
    closePtzPresetModal(true);
  } else if (!$('mergeModal').hidden) {
    $('mergeModal').hidden = true;
  }
});
$('adminConfirmPassword').addEventListener('keydown', event => {
  if (event.key === 'Enter') {
    event.preventDefault();
    $('adminConfirmSubmit').click();
  }
});
$('adminConfirmPhrase').addEventListener('keydown', event => {
  if (event.key === 'Enter') {
    event.preventDefault();
    $('adminConfirmSubmit').click();
  }
});
$('adminConfirmSubmit').addEventListener('click', () => {
  if (!pendingConfirm) return;
  if (pendingConfirmPhrase && $('adminConfirmPhrase').value !== pendingConfirmPhrase) {
    $('adminConfirmError').textContent = 'Type ERASE EVERYTHING exactly as shown to continue.';
    $('adminConfirmError').hidden = false;
    $('adminConfirmPhrase').focus();
    return;
  }
  const password = $('adminConfirmPassword').value;
  if (settingsPasswordRequired && !password) {
    $('adminConfirmError').textContent = 'Enter the admin password before continuing.';
    $('adminConfirmError').hidden = false;
    $('adminConfirmPassword').focus();
    return;
  }
  const action = pendingConfirm;
  closeConfirmModal(false);
  action(password);
});

function ptzUnavailableReason(onvif) {
  if (onvif && (onvif.lastStatus === 401 || onvif.lastStatus === 403 || onvif.rejectedAuthenticationEndpoints)) return 'The service query requires different authorization.';
  if (onvif && onvif.catalogValid && !onvif.rejectedEndpoints && !onvif.catalogStale) return 'The PTZ service was not advertised in a valid query.';
  if (onvif && onvif.inaccessibleEndpoints) return 'Advertised ONVIF services were unreachable at the validated address.';
  if (onvif && onvif.invalidResponseEndpoints) return 'ONVIF services responded with invalid data.';
  if (onvif && onvif.rejectedEndpoints) return 'PTZ discovery inconclusive: service addresses were rejected.';
  return 'PTZ discovery inconclusive: catalog unavailable or service unreachable.';
}

function renderPtz(d) {
  ptzCatalog = d.onvif && d.onvif.ptz ? d.onvif.ptz : {};
  if (!ptzCatalog.available) ptzCatalog.unavailableReason = ptzUnavailableReason(d.onvif);
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
      ? 'Open ' + plural(usableCount, 'PTZ preset', 'PTZ presets')
      : 'PTZ presets unavailable'
  );

  $('ptzMeta').textContent = !ptzCatalog.available
    ? ptzCatalog.unavailableReason
    : !ptzCatalog.presetsLoaded
      ? localizeTimestampText(ptzCatalog.lastResult || 'Querying the camera…')
      : (ptzCatalog.reportedPresetCount || usableCount) + ' presets published · profile '
        + (ptzCatalog.profileName || ptzCatalog.profileToken || 'ONVIF') + ' · updated '
        + duration(ptzCatalog.ageSeconds || 0) + ' ago'
        + (ptzCatalog.truncated ? ' · list limited by the ESP32' : '');

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
    setPtzActionStatus(ptzCatalog.unavailableReason, 'warn');
  } else if (!ptzCatalog.presetsLoaded) {
    setPtzActionStatus('Querying the PTZ presets of the camera…', 'warn');
  } else if (!usableCount) {
    setPtzActionStatus('The camera did not publish PTZ presets.', 'bad');
  } else if (!settingsWriteToken) {
    setPtzActionStatus('Loading authorization for PTZ control…', 'warn');
  } else {
    setPtzActionStatus('', '');
  }
}

function render(d) {
  const [label, color, bg, border] = PALETTE[d.state] || ['Unknown', 'var(--dim)', '', ''];
  const hero = $('hero');
  hero.style.setProperty('--sc', color);
  hero.style.setProperty('--sbg', bg);
  hero.style.setProperty('--sbd', border);
  $('stateText').textContent = label + ' · for ' + duration(d.stateSinceSeconds);

  $('camName').textContent = d.camera.name;
  const cameraInfo = d.onvif.deviceInfo;
  $('cameraMeta').textContent = [cameraInfo.manufacturer, cameraInfo.model].filter(Boolean).join(' · ')
    || (cameraInfo.loaded ? 'Manufacturer and model not reported' : 'Waiting for camera identification');
  const esp32Address = d.wifi.mdnsAvailable
    ? d.wifi.localUrl
    : 'http://' + d.wifi.ip + '/';
  cameraKnown = d.camera.known;
  cameraRecovering = Boolean(d.recovery && d.recovery.graceActive);

  if (d.ui) {
    feedMinimumIntervalMs = Number(d.ui.feedMinimumIntervalMs) || feedMinimumIntervalMs;
    feedTargetCycleMs = Number(d.ui.feedTargetCycleMs) || feedTargetCycleMs;
    statusRefreshIntervalMs = Number(d.ui.statusRefreshIntervalMs) || statusRefreshIntervalMs;
    snapshotHttpTimeoutMs = Number(d.ui.snapshotHttpTimeoutMs) || snapshotHttpTimeoutMs;
  }

  if (d.adminAction && d.adminAction.lastResult && actionsSectionActive() && !factoryResetResult) {
    setAdminActionStatus(d.adminAction.lastResult, d.adminAction.pending === 'none' ? '' : 'warn');
  }
  renderPtz(d);

  if (d.snapshot) {
    const previewFps = d.ui && d.ui.previewMaximumFps ? d.ui.previewMaximumFps : 1;
    if (d.snapshot.healthy) {
      feedDeliveredFrames = Number(d.snapshot.served) || 0;
      feedHealthyMetaPrefix = 'Maximum ' + previewFps + ' FPS · profile '
        + (d.snapshot.profile || 'ONVIF') + ' · last test with '
        + formatBytes(d.snapshot.bytesRead);
      updateFeedHealthyMeta();
    } else {
      feedHealthyMetaPrefix = '';
      $('feedMeta').textContent = d.recovery && d.recovery.graceActive
        ? 'Maximum ' + previewFps + ' FPS · the camera is restarting; the preview will resume automatically'
        : !d.snapshot.conclusive
          ? 'Maximum ' + previewFps + ' FPS · JPEG test waiting for a new conclusive sample'
        : 'Maximum ' + previewFps + ' FPS · image unavailable · '
          + (d.snapshot.detail || 'waiting for a new test');
    }
  }

  if (snapshotModeActive() && !feedLoading && !feedTimer) scheduleFeed(0);

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
  $('rssi').textContent = d.wifi.connected ? rssi + ' dBm' : 'disconnected';
  $('m-wifi').textContent = d.wifi.connected
    ? d.wifi.ssid + ' · ' + esp32Address + ' · gateway ' + d.wifi.gateway
    : 'trying to reconnect';
  pip('p-wifi', level >= 3 ? 'ok' : level === 2 ? 'warn' : 'bad');

  const currentSequence = c.consecutiveInconclusiveChecks
    ? plural(c.consecutiveInconclusiveChecks, 'inconclusive test in a row', 'inconclusive tests in a row')
    : c.consecutiveFailures
      ? plural(c.consecutiveFailures, 'consecutive failure', 'consecutive failures')
      : plural(c.consecutiveSuccesses, 'success in a row', 'successes in a row');
  $('v-checks').textContent = c.totalChecks;
  $('m-checks').textContent = plural(c.failedChecks, 'failure in total', 'failures in total') + ' · '
    + (c.inconclusiveChecks ? plural(c.inconclusiveChecks, 'inconclusive', 'inconclusive') + ' · ' : '')
    + currentSequence;

  $('l-event').textContent = localizeTimestampText(d.lastEvent);
  $('l-url').textContent = d.camera.onvifUrl || 'unknown';
  $('l-image').textContent = localizeTimestampText(
    d.snapshot.lastResult || d.snapshot.detail || 'not tested yet'
  );
  $('l-reboot').textContent = localizeTimestampText(
    d.recovery.lastResult
      + (d.recovery.graceActive ? ' · waiting ' + duration(d.recovery.graceRemainingSeconds) : '')
  );
  $('l-disc').textContent = localizeTimestampText(d.lastDiscovery);
  $('l-scan').textContent = localizeTimestampText(d.lastSubnetScan);
  const mailResult = $('l-mail');
  const mailTag = document.createElement('span');
  mailTag.className = 'tag ' + (d.resendConfigured ? 'ok' : 'bad');
  mailTag.textContent = d.resendConfigured ? 'Resend active' : 'Resend not configured';
  mailResult.replaceChildren(
    mailTag,
    document.createTextNode(
      ' ' + c.emailsSent + ' sent, ' + c.emailErrors + ' errors · '
        + localizeTimestampText(d.lastEmailResult)
    )
  );

  $('raw').textContent = JSON.stringify(d, null, 2);
  $('copyDiagnostics').disabled = false;
  $('foot').textContent = 'ESP32 Camera Watchdog · ' + d.firmwareVersion;
  $('deviceUptime').textContent = 'uptime ' + duration(d.uptimeSeconds);
  renderResources(d);

  lastOk = Date.now();
}

function scheduleStatusRefresh(delay) {
  clearTimeout(statusRefreshTimer);
  statusRefreshTimer = null;
  if (document.hidden) return;
  statusRefreshTimer = setTimeout(refresh, delay == null ? statusRefreshIntervalMs : delay);
}

async function refresh(force) {
  clearTimeout(statusRefreshTimer);
  statusRefreshTimer = null;
  if (statusLoading) return;
  if (snapshotRequestActive()) { scheduleStatusRefresh(250); return; }
  statusLoading = true;
  const generation = ++statusRequestGeneration;
  const controller = typeof AbortController === 'function' ? new AbortController() : null;
  statusAbortController = controller;
  const timeout = setTimeout(() => { if (controller) controller.abort(); }, 20000);
  try {
    await loadCameraRegistry(controller ? controller.signal : undefined);
    if (generation !== statusRequestGeneration) return;
    if (selectedCameraId && activeView !== 'overview' && !logsSectionActive()) {
      const id = selectedCameraId;
      const resetEpoch = factoryResetEpoch;
      const response = await fetch(cameraUrl('/api/status', id), { cache: 'no-store', signal: controller ? controller.signal : undefined });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const payload = await response.json();
      if (generation !== statusRequestGeneration || payload.cameraId !== selectedCameraId) return;
      updateFactoryResetState(payload, resetEpoch);
      render(payload);
    }
    if ((activeView === 'settings' || activeView === 'camera') && !settingsDirty) loadSettings(false);
    if (liveModeActive()) startLive();
    lastOk = Date.now();
    $('deviceStatus').classList.remove('stale');
    $('deviceStatusText').textContent = 'updated just now';
  } catch (error) {
    if (generation === statusRequestGeneration && !document.hidden) {
      $('deviceStatus').classList.add('stale');
      $('deviceStatusText').textContent = 'no response from the device';
    }
  } finally {
    clearTimeout(timeout);
    if (generation !== statusRequestGeneration) return;
    statusAbortController = null;
    statusLoading = false;
    scheduleStatusRefresh();
  }
}

setInterval(() => {
  if (!lastOk || $('deviceStatus').classList.contains('stale')) return;
  const ago = Math.round((Date.now() - lastOk) / 1000);
  $('deviceStatusText').textContent = ago < 2 ? 'updated just now' : 'updated ' + ago + 's ago';
}, 1000);

const LivePlayer = (() => {
  'use strict';

  const CLOCK = 90000;
  const MIN_DELTA = 1500;
  const MAX_DELTA = 22500;
  const ERROR_TEXT = {
    live_in_use: 'Another viewer is watching a different camera or quality. Close that stream and try again.',
    busy: 'The ESP32 is busy checking the camera. Trying again…',
    camera_unavailable: 'The camera is not available right now.',
    onvif_unavailable: 'The camera did not respond to the ONVIF service.',
    onvif_media_unavailable: 'The camera did not respond to the ONVIF media service.',
    onvif_stream_uri_failed: 'The camera did not provide the video address.',
    no_h264_profile: 'The camera does not offer an H.264 video profile.',
    invalid_stream_uri: 'The camera announced an invalid video address.',
    relay_unavailable: 'The ESP32 could not start the video relay.',
    camera_unreachable: 'Could not connect to the RTSP video of the camera.',
    rtsp_describe_failed: 'The camera refused the RTSP video description.',
    rtsp_setup_failed: 'The camera refused the RTSP video setup.',
    rtsp_play_failed: 'The camera refused to start the RTSP video.',
    rtsp_auth_required: 'The camera requires a username and password for the video. Set up the camera credentials.',
    rtsp_auth_rejected: 'The camera rejected the credentials used for the video.',
    rtsp_transport_unsupported: 'The camera does not accept RTSP video over TCP.',
    camera_codec_not_h264: 'The camera is not sending H.264 on this profile.',
    camera_stream_closed: 'The camera ended the video.',
    camera_stream_stalled: 'The camera stopped sending video.',
    low_memory: 'The ESP32 has little free memory for the video.',
    device_unreachable: 'No response from the ESP32.',
    unsupported: 'This browser does not support Media Source Extensions with H.264.',
    stalled: 'The video stopped arriving. Reconnecting…'
  };
  const NO_RETRY = new Set(['unsupported', 'no_h264_profile', 'camera_codec_not_h264', 'rtsp_auth_required', 'rtsp_auth_rejected', 'invalid_stream_uri']);

  const concat = chunks => {
    let size = 0;
    for (const chunk of chunks) size += chunk.length;
    const out = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) { out.set(chunk, offset); offset += chunk.length; }
    return out;
  };
  const u32 = value => Uint8Array.of((value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255);
  const u16 = value => Uint8Array.of((value >>> 8) & 255, value & 255);
  const ascii = text => Uint8Array.from(text, ch => ch.charCodeAt(0));
  const zeros = count => new Uint8Array(count);
  const box = (type, ...parts) => {
    const body = concat(parts);
    return concat([u32(body.length + 8), ascii(type), body]);
  };
  const fullBox = (type, version, flags, ...parts) => box(type, Uint8Array.of(version, (flags >> 16) & 255, (flags >> 8) & 255, flags & 255), ...parts);
  const MATRIX = concat([u32(0x10000), u32(0), u32(0), u32(0), u32(0x10000), u32(0), u32(0), u32(0), u32(0x40000000)]);

  class BitReader {
    constructor(bytes) { this.bytes = bytes; this.position = 0; }
    bit() {
      const byte = this.bytes[this.position >> 3];
      if (byte === undefined) throw new Error('sps_truncated');
      const value = (byte >> (7 - (this.position & 7))) & 1;
      this.position++;
      return value;
    }
    bits(count) { let value = 0; for (let i = 0; i < count; i++) value = (value << 1) | this.bit(); return value >>> 0; }
    ue() {
      let zerosCount = 0;
      while (this.bit() === 0) { if (++zerosCount > 31) throw new Error('sps_invalid'); }
      return zerosCount === 0 ? 0 : (((1 << zerosCount) >>> 0) - 1 + this.bits(zerosCount)) >>> 0;
    }
    se() { const value = this.ue(); return value & 1 ? (value + 1) / 2 : -(value / 2); }
  }

  const unescapeRbsp = nal => {
    const out = [];
    for (let i = 0; i < nal.length; i++) {
      if (i >= 2 && nal[i] === 3 && nal[i - 1] === 0 && nal[i - 2] === 0) continue;
      out.push(nal[i]);
    }
    return Uint8Array.from(out);
  };

  const parseSps = nal => {
    const reader = new BitReader(unescapeRbsp(nal.subarray(1)));
    const profile = reader.bits(8);
    reader.bits(8);
    reader.bits(8);
    reader.ue();
    let chroma = 1;
    let lumaDepth = 8;
    let chromaDepth = 8;
    if ([100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135].includes(profile)) {
      chroma = reader.ue();
      if (chroma === 3) reader.bit();
      lumaDepth = 8 + reader.ue();
      chromaDepth = 8 + reader.ue();
      reader.bit();
      if (reader.bit()) {
        const lists = chroma !== 3 ? 8 : 12;
        for (let list = 0; list < lists; list++) {
          if (!reader.bit()) continue;
          const size = list < 6 ? 16 : 64;
          let last = 8;
          let next = 8;
          for (let i = 0; i < size; i++) {
            if (next !== 0) { next = (last + reader.se() + 256) % 256; }
            last = next === 0 ? last : next;
          }
        }
      }
    }
    reader.ue();
    const pocType = reader.ue();
    if (pocType === 0) {
      reader.ue();
    } else if (pocType === 1) {
      reader.bit(); reader.se(); reader.se();
      const cycle = reader.ue();
      for (let i = 0; i < cycle; i++) reader.se();
    }
    reader.ue();
    reader.bit();
    const widthMbs = reader.ue() + 1;
    const heightUnits = reader.ue() + 1;
    const frameMbsOnly = reader.bit();
    if (!frameMbsOnly) reader.bit();
    reader.bit();
    let cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
    if (reader.bit()) { cropLeft = reader.ue(); cropRight = reader.ue(); cropTop = reader.ue(); cropBottom = reader.ue(); }
    const unitX = chroma === 0 || chroma === 3 ? 1 : 2;
    const unitY = (chroma === 1 ? 2 : 1) * (2 - frameMbsOnly);
    return {
      width: widthMbs * 16 - (cropLeft + cropRight) * unitX,
      height: (2 - frameMbsOnly) * heightUnits * 16 - (cropTop + cropBottom) * unitY,
      profile, chroma, lumaDepth, chromaDepth
    };
  };

  const hex2 = value => value.toString(16).padStart(2, '0');

  const buildInit = (sps, pps, info) => {
    const high = [100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135].includes(info.profile);
    const avcC = box('avcC',
      Uint8Array.of(1, sps[1], sps[2], sps[3], 0xFF, 0xE1), u16(sps.length), sps,
      Uint8Array.of(1), u16(pps.length), pps,
      high ? Uint8Array.of(0xFC | info.chroma, 0xF8 | (info.lumaDepth - 8), 0xF8 | (info.chromaDepth - 8), 0) : new Uint8Array(0));
    const avc1 = box('avc1', zeros(6), u16(1), zeros(16), u16(info.width), u16(info.height),
      u32(0x00480000), u32(0x00480000), u32(0), u16(1), zeros(32), u16(0x18), u16(0xFFFF), avcC);
    const track = box('trak',
      fullBox('tkhd', 0, 7, u32(0), u32(0), u32(1), u32(0), u32(0), zeros(8), u16(0), u16(0), u16(0), u16(0), MATRIX, u32(info.width << 16), u32(info.height << 16)),
      box('mdia',
        fullBox('mdhd', 0, 0, u32(0), u32(0), u32(CLOCK), u32(0), u16(0x55C4), u16(0)),
        fullBox('hdlr', 0, 0, u32(0), ascii('vide'), zeros(12), ascii('VideoHandler\0')),
        box('minf',
          fullBox('vmhd', 0, 1, u16(0), zeros(6)),
          box('dinf', fullBox('dref', 0, 0, u32(1), fullBox('url ', 0, 1))),
          box('stbl',
            fullBox('stsd', 0, 0, u32(1), avc1),
            fullBox('stts', 0, 0, u32(0)), fullBox('stsc', 0, 0, u32(0)),
            fullBox('stsz', 0, 0, u32(0), u32(0)), fullBox('stco', 0, 0, u32(0))))));
    return concat([
      box('ftyp', ascii('isom'), u32(512), ascii('isom'), ascii('iso6'), ascii('avc1'), ascii('mp41')),
      box('moov',
        fullBox('mvhd', 0, 0, u32(0), u32(0), u32(1000), u32(0), u32(0x10000), u16(0x100), u16(0), zeros(8), MATRIX, zeros(24), u32(2)),
        track,
        box('mvex', fullBox('trex', 0, 0, u32(1), u32(1), u32(0), u32(0), u32(0))))
    ]);
  };

  const buildFragment = (sequence, decodeTime, duration, sample, key) => {
    const high = Math.floor(decodeTime / 4294967296);
    const low = decodeTime % 4294967296;
    const mfhd = fullBox('mfhd', 0, 0, u32(sequence));
    const tfhd = fullBox('tfhd', 0, 0x020000, u32(1));
    const tfdt = fullBox('tfdt', 1, 0, u32(high), u32(low));
    const trun = offset => fullBox('trun', 0, 0x000701, u32(1), u32(offset), u32(duration), u32(sample.length), u32(key ? 0x02000000 : 0x01010000));
    const sizeProbe = box('moof', mfhd, box('traf', tfhd, tfdt, trun(0)));
    const moof = box('moof', mfhd, box('traf', tfhd, tfdt, trun(sizeProbe.length + 8)));
    return concat([moof, box('mdat', sample)]);
  };

  class Player {
    constructor(video, hooks) {
      this.video = video;
      this.hooks = hooks;
      this.generation = 0;
      this.retryTimer = null;
      this.watchTimer = null;
      this.retryDelay = 1000;
      this.reset();
      video.addEventListener('playing', () => {
        if (!this.initialized) return;
        this.playing = true;
        this.retryDelay = 1000;
        hooks.state('live', '');
      });
    }

    reset() {
      this.socket = null;
      this.source = null;
      this.buffer = null;
      this.objectUrl = null;
      this.queue = [];
      this.initialized = false;
      this.mediaReady = false;
      this.sps = null;
      this.pps = null;
      this.spsKey = '';
      this.info = null;
      this.units = [];
      this.fragment = null;
      this.unitTimestamp = null;
      this.waitingForKey = true;
      this.sequence = 0;
      this.decodeTime = 0;
      this.lastTimestamp = null;
      this.lastDelta = 7500;
      this.frames = 0;
      this.bytes = 0;
      this.lastDataAt = 0;
      this.statFrames = 0;
      this.statBytes = 0;
      this.statAt = performance.now();
      this.fps = 0;
      this.kbps = 0;
      this.catchingUp = false;
      this.lastTime = -1;
      this.lastTimeAt = performance.now();
      this.playing = false;
    }

    supported() {
      return typeof window.MediaSource === 'function' && typeof window.WebSocket === 'function';
    }

    start(cameraId, quality, host) {
      this.cameraId = cameraId;
      this.quality = quality || 'fluid';
      this.host = host || location.hostname;
      this.retryDelay = 1000;
      this.begin();
    }

    begin() {
      this.stopInternal();
      const generation = ++this.generation;
      if (!this.supported()) { this.hooks.error('unsupported', ERROR_TEXT.unsupported, true); return; }
      this.connect(generation);
    }

    stop() {
      this.generation++;
      this.stopInternal();
      this.hooks.state('stopped', '');
    }

    stopInternal() {
      clearTimeout(this.retryTimer);
      clearInterval(this.watchTimer);
      this.retryTimer = null;
      this.watchTimer = null;
      const socket = this.socket;
      if (socket) {
        socket.onopen = socket.onmessage = socket.onerror = socket.onclose = null;
        try { socket.close(); } catch (error) {}
      }
      const video = this.video;
      if (this.objectUrl) URL.revokeObjectURL(this.objectUrl);
      video.pause();
      video.removeAttribute('src');
      video.load();
      this.reset();
    }

    async prepare(generation) {
      const url = '/api/live/start?cameraId=' + encodeURIComponent(this.cameraId) + '&quality=' + encodeURIComponent(this.quality);
      let last = { code: 'busy' };
      for (let attempt = 0; attempt < 12; attempt++) {
        if (generation !== this.generation) return null;
        let response;
        let data;
        try {
          response = await fetch(url, { cache: 'no-store' });
          data = await response.json();
        } catch (error) {
          last = { code: 'device_unreachable' };
          await new Promise(resolve => setTimeout(resolve, 1000));
          continue;
        }
        if (response.ok && data.ok) return data;
        const code = (data && data.error) || ('http_' + response.status);
        last = { code, detail: data && data.detail };
        if (code === 'busy' || code === 'live_in_use') {
          this.hooks.state('connecting', ERROR_TEXT[code]);
          await new Promise(resolve => setTimeout(resolve, code === 'busy' ? 700 : 900));
          continue;
        }
        break;
      }
      throw last;
    }

    async connect(generation) {
      this.hooks.state('connecting', 'Connecting to the camera…');
      let ready;
      try {
        ready = await this.prepare(generation);
      } catch (error) {
        if (generation === this.generation) this.fail(error.code, error.detail);
        return;
      }
      if (generation !== this.generation || !ready) return;
      this.hooks.state('connecting', 'Opening the video channel…');
      let socket;
      try {
        socket = new WebSocket('ws://' + this.host + ':' + ready.port + '/live?camera=' + encodeURIComponent(this.cameraId));
      } catch (error) {
        this.fail('device_unreachable');
        return;
      }
      socket.binaryType = 'arraybuffer';
      this.socket = socket;
      this.lastDataAt = performance.now();
      socket.onmessage = event => {
        if (generation !== this.generation) return;
        if (typeof event.data === 'string') {
          try { this.hello(JSON.parse(event.data)); } catch (error) {}
          return;
        }
        this.lastDataAt = performance.now();
        this.bytes += event.data.byteLength;
        this.statBytes += event.data.byteLength;
        try {
          this.packet(new Uint8Array(event.data));
        } catch (error) {
          this.restart('stalled');
        }
      };
      socket.onclose = event => {
        if (generation !== this.generation) return;
        this.socket = null;
        this.fail(event.reason || (this.frames ? 'camera_stream_closed' : 'device_unreachable'));
      };
      socket.onerror = () => {};
      this.watchTimer = setInterval(() => this.watch(generation), 250);
    }

    hello(message) {
      if (message && message.type === 'hello') {
        this.profile = message;
        this.hooks.info(message);
      }
    }

    fail(code, detail) {
      const known = ERROR_TEXT[code] || detail || 'Failed to stream the video.';
      const fatal = NO_RETRY.has(code);
      this.stopInternal();
      const generation = this.generation;
      this.hooks.error(code, known, fatal);
      if (fatal) return;
      const delay = this.retryDelay;
      this.retryDelay = Math.min(8000, this.retryDelay * 2);
      this.retryTimer = setTimeout(() => { if (generation === this.generation) this.begin(); }, delay);
    }

    restart(code) {
      this.fail(code);
    }

    packet(data) {
      if (data.length < 6) return;
      const flags = data[0];
      const timestamp = ((data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]) >>> 0;
      const payload = data.subarray(5);
      if (flags & 2) { this.fragment = null; this.units = []; this.unitTimestamp = null; this.waitingForKey = true; }
      if (this.unitTimestamp !== null && timestamp !== this.unitTimestamp && this.units.length) this.finishUnit();
      this.unitTimestamp = timestamp;
      const type = payload[0] & 31;
      if (type >= 1 && type <= 23) {
        this.units.push(payload);
      } else if (type === 24) {
        let offset = 1;
        while (offset + 2 <= payload.length) {
          const size = (payload[offset] << 8) | payload[offset + 1];
          offset += 2;
          if (offset + size > payload.length) break;
          this.units.push(payload.subarray(offset, offset + size));
          offset += size;
        }
      } else if (type === 28 && payload.length > 2) {
        const header = payload[1];
        if (header & 0x80) this.fragment = [Uint8Array.of((payload[0] & 0xE0) | (header & 0x1F)), payload.subarray(2)];
        else if (this.fragment) this.fragment.push(payload.subarray(2));
        if ((header & 0x40) && this.fragment) { this.units.push(concat(this.fragment)); this.fragment = null; }
      }
      if (flags & 1) this.finishUnit();
    }

    finishUnit() {
      const units = this.units;
      const timestamp = this.unitTimestamp;
      this.units = [];
      this.unitTimestamp = null;
      let idr = false;
      const slices = [];
      for (const unit of units) {
        const type = unit[0] & 31;
        if (type === 7) this.setParameter('sps', unit);
        else if (type === 8) this.setParameter('pps', unit);
        else if (type === 5) { idr = true; slices.push(unit); }
        else if (type === 1) slices.push(unit);
      }
      if (!slices.length || timestamp === null) return;
      if (!this.initialized) {
        if (!idr || !this.sps || !this.pps) return;
        this.initialize();
      }
      if (idr) this.waitingForKey = false;
      if (this.waitingForKey) return;

      let size = 0;
      for (const slice of slices) size += slice.length + 4;
      const sample = new Uint8Array(size);
      let offset = 0;
      for (const slice of slices) {
        sample[offset] = slice.length >>> 24; sample[offset + 1] = slice.length >>> 16; sample[offset + 2] = slice.length >>> 8; sample[offset + 3] = slice.length;
        sample.set(slice, offset + 4);
        offset += slice.length + 4;
      }

      let delta = this.lastDelta;
      if (this.lastTimestamp !== null) {
        delta = (timestamp - this.lastTimestamp) >>> 0;
        delta = delta < MIN_DELTA ? MIN_DELTA : delta > MAX_DELTA ? MAX_DELTA : delta;
      }
      this.lastTimestamp = timestamp;
      const duration = this.lastDelta;
      this.lastDelta = delta;
      this.queue.push(buildFragment(++this.sequence, this.decodeTime, duration, sample, idr));
      this.decodeTime += duration;
      this.frames++;
      this.statFrames++;
      this.pump();
    }

    setParameter(kind, unit) {
      const copy = unit.slice();
      const key = Array.from(copy).join(',');
      if (kind === 'pps') { this.pps = copy; return; }
      if (this.spsKey && this.spsKey !== key && this.initialized) {
        this.sps = copy; this.spsKey = key;
        this.restart('stalled');
        return;
      }
      this.sps = copy;
      this.spsKey = key;
    }

    initialize() {
      let info;
      try { info = parseSps(this.sps); } catch (error) { info = { width: (this.profile && this.profile.width) || 704, height: (this.profile && this.profile.height) || 576, profile: this.sps[1], chroma: 1, lumaDepth: 8, chromaDepth: 8 }; }
      this.dimensions = info;
      const codec = 'avc1.' + hex2(this.sps[1]) + hex2(this.sps[2]) + hex2(this.sps[3]);
      const mime = 'video/mp4; codecs="' + codec + '"';
      if (!MediaSource.isTypeSupported(mime)) { this.hooks.error('unsupported', ERROR_TEXT.unsupported, true); this.generation++; this.stopInternal(); return; }
      this.initialized = true;
      const source = new MediaSource();
      this.source = source;
      this.objectUrl = URL.createObjectURL(source);
      this.video.src = this.objectUrl;
      const init = buildInit(this.sps, this.pps, info);
      const generation = this.generation;
      source.addEventListener('sourceopen', () => {
        if (generation !== this.generation || this.buffer) return;
        try {
          this.buffer = source.addSourceBuffer(mime);
          this.buffer.mode = 'segments';
          this.buffer.addEventListener('updateend', () => this.pump());
          this.buffer.addEventListener('error', () => { if (generation === this.generation) this.restart('stalled'); });
          this.buffer.appendBuffer(init);
          this.mediaReady = true;
        } catch (error) {
          this.restart('stalled');
        }
      }, { once: true });
      this.hooks.dimensions(info);
    }

    pump() {
      const buffer = this.buffer;
      if (!buffer || !this.mediaReady || buffer.updating || !this.queue.length || this.source.readyState !== 'open') return;
      const batch = this.queue.length > 1 ? concat(this.queue) : this.queue[0];
      this.queue = [];
      try {
        buffer.appendBuffer(batch);
      } catch (error) {
        if (error && error.name === 'QuotaExceededError') {
          this.trim(true);
          this.queue.unshift(batch);
        } else {
          this.restart('stalled');
        }
      }
    }

    trim(force) {
      const buffer = this.buffer;
      if (!buffer || buffer.updating || !buffer.buffered.length) return;
      const start = buffer.buffered.start(0);
      const keepFrom = this.video.currentTime - (force ? 1 : 3);
      if (keepFrom - start > (force ? 0.5 : 3)) {
        try { buffer.remove(start, keepFrom); } catch (error) {}
      }
    }

    watch(generation) {
      if (generation !== this.generation) return;
      const now = performance.now();
      const video = this.video;

      if (now - this.statAt >= 1000) {
        const seconds = (now - this.statAt) / 1000;
        this.fps = this.statFrames / seconds;
        this.kbps = this.statBytes * 8 / seconds / 1000;
        this.statFrames = 0;
        this.statBytes = 0;
        this.statAt = now;
        this.trim(false);
      }

      if (this.socket && now - this.lastDataAt > 6000) { this.restart('stalled'); return; }

      const buffered = video.buffered;
      let lag = 0;
      if (buffered.length) {
        const end = buffered.end(buffered.length - 1);
        lag = Math.max(0, end - video.currentTime);
        if (video.paused && this.frames > 2 && !this.playing) video.play().catch(() => {});
        if (lag > 1.5) {
          video.currentTime = end - 0.12;
          this.catchingUp = false;
          video.playbackRate = 1;
        } else if (lag > 0.5) {
          this.catchingUp = true;
        } else if (lag < 0.22) {
          this.catchingUp = false;
        }
        const rate = this.catchingUp ? Math.min(1.5, 1 + (lag - 0.2) * 0.8) : 1;
        if (Math.abs(video.playbackRate - rate) > 0.02) video.playbackRate = rate;

        if (video.currentTime !== this.lastTime) {
          this.lastTime = video.currentTime;
          this.lastTimeAt = now;
        } else if (now - this.lastTimeAt > 2500 && lag > 0.3 && !video.paused) {
          video.currentTime = Math.max(buffered.start(buffered.length - 1), end - 0.12);
          this.lastTimeAt = now;
        }
      }
      this.hooks.stats({ fps: this.fps, kbps: this.kbps, lag, width: this.dimensions && this.dimensions.width, height: this.dimensions && this.dimensions.height, frames: this.frames });
    }
  }

  return { Player, ERROR_TEXT };
})();

const liveVideo = $('liveVideo');
let liveStartedFor = 0;
let liveStartedQuality = '';
let liveQuality = 'fluid';
let liveProfileText = '';

function setLiveOverlay(text, showRetry) {
  $('liveState').classList.toggle('hidden', !text);
  $('liveStateText').textContent = text || '';
  $('liveRetry').hidden = !showRetry;
}

function setLiveBadge(on) {
  $('liveBadge').classList.toggle('on', on);
  $('liveBadgeText').textContent = on ? 'LIVE' : 'NO SIGNAL';
}

const livePlayer = new LivePlayer.Player(liveVideo, {
  state(kind, text) {
    if (kind === 'live') {
      liveVideo.classList.add('ready');
      setLiveOverlay('', false);
      setLiveBadge(true);
      $('liveStamp').textContent = '';
    } else if (kind === 'connecting') {
      liveVideo.classList.remove('ready');
      setLiveOverlay(text || 'Connecting to the camera…', false);
      setLiveBadge(false);
      $('liveStamp').textContent = 'connecting';
    }
  },
  error(code, text, fatal) {
    liveVideo.classList.remove('ready');
    setLiveBadge(false);
    setLiveOverlay(fatal ? text : text + ' Reconnecting…', fatal);
    $('liveStamp').textContent = fatal ? 'stream interrupted' : 'reconnecting';
    if (fatal) liveStartedFor = 0;
  },
  info(message) {
    liveProfileText = message.profile || 'from the camera';
  },
  dimensions() {},
  stats(values) {
    if (!values.width || !liveVideo.classList.contains('ready')) return;
    $('liveMeta').textContent = 'H.264 · profile ' + liveProfileText
      + ' · ' + values.width + '×' + values.height
      + ' · ' + Math.round(values.fps) + ' fps · ' + Math.round(values.kbps) + ' kbps'
      + ' · lag ' + values.lag.toFixed(1) + ' s · ' + values.frames + ' frames';
  }
});

function startLive() {
  if (!liveModeActive() || document.hidden || !selectedCameraId) return;
  if (liveStartedFor === selectedCameraId && liveStartedQuality === liveQuality) return;
  liveStartedFor = selectedCameraId;
  liveStartedQuality = liveQuality;
  liveVideo.classList.remove('ready');
  setLiveBadge(false);
  setLiveOverlay('Connecting to the camera…', false);
  $('liveStamp').textContent = 'connecting';
  livePlayer.start(selectedCameraId, liveQuality);
}

function stopLive() {
  if (liveStartedFor) livePlayer.stop();
  liveStartedFor = 0;
  liveStartedQuality = '';
  liveVideo.classList.remove('ready');
}

$('liveQuality').addEventListener('change', () => {
  liveQuality = $('liveQuality').value === 'high' ? 'high' : 'fluid';
  stopLive();
  startLive();
});
$('liveRetry').addEventListener('click', () => { stopLive(); startLive(); });
window.addEventListener('pagehide', () => livePlayer.stop());

function activateView(name, updateHash) {
  const nextView = ['overview', 'camera', 'settings', 'resources'].includes(name)
    ? name
    : 'overview';
  if (nextView !== activeView && (settingsBusy || !$('adminConfirmModal').hidden)) {
    if (updateHash === false) history.replaceState(null, '', activeView === 'settings' ? settingsHashFor(activeSettingsSection) : '#' + activeView);
    return;
  }
  if (activeView === 'settings' && nextView !== activeView && settingsDirty) {
    if (!window.confirm('Discard the unsaved changes before leaving the settings?')) {
      if (updateHash === false) location.hash = settingsHashFor(activeSettingsSection);
      return;
    }
    settingsDirty = false;
    settingsLoaded = false;
    settingsValues = null;
    settingsCameraPasswordConfigured = false;
    settingsLoadedKey = '';
  }
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
  document.title = nextView === 'settings' ? settingsDocumentTitle() : selectedTab.textContent + ' · Camera Watchdog';

  if (nextView === 'settings') {
    updateSettingsScope();
    loadSettings(false);
  }

  if (logsSectionActive()) {
    loadPersistentLogs(true);
  }

  if (nextView === 'overview') {
    overviewPreviews.pass++;
    enqueueOverviewPreviews();
  } else {
    cancelOverviewPreviews();
  }

  if (nextView === 'camera') {
    applyCameraMode();
  } else {
    $('ptzPresetModal').hidden = true;
    if (isNativeFullscreen()) (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    if (cameraStage.classList.contains('fs-fallback')) exitFallbackFullscreen();
    leaveSnapshotMode();
    stopLive();
    scheduleStatusRefresh(0);
  }

  const nextHash = nextView === 'settings' ? settingsHashFor(activeSettingsSection) : '#' + nextView;
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
  status.textContent = 'Copying…';
  try {
    if (window.isSecureContext && navigator.clipboard) {
      await navigator.clipboard.writeText($('raw').textContent);
    } else {
      // Local HTTP access to the ESP32 does not provide the Clipboard API.
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
    status.textContent = 'JSON copied';
  } catch (error) {
    status.className = 'bad';
    status.textContent = 'Copy failed';
  } finally {
    button.removeAttribute('aria-busy');
    diagnosticCopyTimer = setTimeout(() => {
      status.className = '';
      status.textContent = 'Copy JSON';
    }, 3000);
  }
});

document.querySelectorAll('[role="tablist"]').forEach(tablist => {
  const tabs = [...tablist.querySelectorAll('[role="tab"]')];
  tabs.forEach(tab => {
    tab.addEventListener('keydown', event => {
      if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      const enabledTabs = tabs.filter(candidate => !candidate.disabled);
      if (!enabledTabs.length) return;
      const index = enabledTabs.indexOf(tab);
      let nextIndex = index;
      if (event.key === 'ArrowLeft') nextIndex = (index - 1 + enabledTabs.length) % enabledTabs.length;
      if (event.key === 'ArrowRight') nextIndex = (index + 1) % enabledTabs.length;
      if (event.key === 'Home') nextIndex = 0;
      if (event.key === 'End') nextIndex = enabledTabs.length - 1;
      enabledTabs[nextIndex].focus();
      enabledTabs[nextIndex].click();
    });
  });
});

window.addEventListener('hashchange', () => {
  const nextView = viewFromHash();
  if (nextView === 'settings') {
    const hashSection = settingsSectionFromHash();
    const wantedSection = hashSection || (SETTINGS_TOOL_SECTIONS.has(activeSettingsSection) ? settingsFormSection : activeSettingsSection);
    if (wantedSection !== activeSettingsSection) activateSettingsSection(wantedSection);
  }
  if (nextView !== activeView) activateView(nextView, false);
});

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    if (liveModeActive()) stopLive();
    cancelOverviewPreviews();
    clearTimeout(feedTimer);
    clearTimeout(statusRefreshTimer);
    feedTimer = null;
    cancelFeedLoad(false);
    if (snapshotModeActive()) {
      $('cameraFeed').classList.remove('ready');
      $('feedState').classList.remove('hidden');
      $('feedState').textContent = 'Preview paused while the tab is hidden.';
      $('feedStamp').textContent = 'waiting for the tab to return';
    }
    statusRequestGeneration++;
    if (statusAbortController) statusAbortController.abort();
    statusAbortController = null;
    statusLoading = false;
    return;
  }

  refresh();
  if (liveModeActive()) startLive();
  if (snapshotModeActive()) scheduleFeed(0);
  if (activeView === 'overview') enqueueOverviewPreviews();
  if (activeView === 'settings' && !settingsDirty && !settingsBusy && $('adminConfirmModal').hidden) loadSettings(false);
  if (logsSectionActive()) loadPersistentLogs(true);
});

activateSettingsSection(activeSettingsSection);
activateView(activeView, false);
if (!snapshotModeActive()) refresh();
</script>
</body>
</html>
)HTML";

}
