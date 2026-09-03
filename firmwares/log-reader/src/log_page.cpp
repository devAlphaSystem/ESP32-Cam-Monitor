#include "log_page.h"

namespace LogPage {

const char HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Leitor de logs do ESP32</title>
<style>
:root{color-scheme:dark;--bg:#070a0f;--panel:#0e1520;--panel2:#131c2a;--line:#1c2738;--text:#f1f6fc;--muted:#a3b3c7;--blue:#0ea5e9;--cyan:#38bdf8;--red:#f43f5e;--yellow:#f59e0b;--green:#10b981}*{box-sizing:border-box}:focus-visible{outline:2px solid var(--blue);outline-offset:2px}body{margin:0;background:radial-gradient(circle at 50% -20%,#131c2a 0,#070a0f 48%);color:var(--text);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}.wrap{max-width:1500px;margin:auto;padding:28px 20px 48px}.head{display:flex;gap:20px;justify-content:space-between;align-items:flex-start;margin-bottom:18px}.head h1{font-size:clamp(24px,3vw,36px);margin:0 0 5px}.head p{color:var(--muted);margin:0}.badge{border:1px solid var(--line);border-radius:999px;padding:7px 12px;white-space:nowrap;background:#0c1828}.badge.ok{border-color:#26745b;color:var(--green)}.badge.warn{border-color:#8a6427;color:var(--yellow)}.badge.bad{border-color:#883943;color:var(--red)}.notice{border:1px solid #315375;background:#0c1c2f;border-radius:12px;padding:12px 15px;color:#b9d9fa;margin-bottom:16px}.cards{display:grid;grid-template-columns:repeat(5,minmax(150px,1fr));gap:10px;margin-bottom:16px}.card,.panel{background:linear-gradient(145deg,var(--panel2),var(--panel));border:1px solid var(--line);border-radius:14px;box-shadow:0 12px 35px #0004}.card{padding:14px}.card span{display:block;color:var(--muted);font-size:12px;margin-bottom:3px}.card strong{font-size:20px}.panel{padding:16px;margin-bottom:16px}.filter-grid{display:grid;grid-template-columns:minmax(220px,2fr) repeat(4,minmax(130px,1fr)) repeat(2,minmax(110px,.75fr));gap:10px}.field label{display:block;color:var(--muted);font-size:12px;margin:0 0 5px}.field input,.field select{width:100%;height:38px;border-radius:8px;border:1px solid var(--line);background:#091525;color:var(--text);padding:0 10px;outline:none}.field input:focus,.field select:focus{border-color:var(--blue)}.actions{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:12px}.button{display:inline-flex;align-items:center;justify-content:center;min-height:38px;border:1px solid var(--line);border-radius:8px;background:#142944;color:var(--text);padding:8px 13px;text-decoration:none;cursor:pointer;font:inherit}.button:hover{border-color:var(--blue)}.button.primary{background:#1766a6;border-color:#2b8edb}.button.raw{margin-left:auto;border-color:#81512e;color:#ffd5ad}.hint{color:var(--muted);font-size:12px}.table-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}.table-head h2{font-size:18px;margin:0}.table-wrap{overflow:auto;border:1px solid var(--line);border-radius:10px}table{width:100%;border-collapse:collapse;min-width:1100px;background:#0b1728}th,td{text-align:left;padding:10px;border-bottom:1px solid #203751;vertical-align:top}th{position:sticky;top:0;background:#13243a;color:#a9c7e8;font-size:12px;z-index:1}tr:last-child td{border-bottom:0}.level{font-weight:700}.level.error{color:var(--red)}.level.warning{color:var(--yellow)}.level.info{color:var(--cyan)}.detail{max-width:460px;white-space:pre-wrap;overflow-wrap:anywhere}.sub{color:var(--muted);font-size:11px;margin-top:3px}.empty{text-align:center;color:var(--muted);padding:32px}.pager{display:flex;align-items:center;justify-content:flex-end;gap:10px;margin-top:12px}.pager button:disabled{opacity:.4;cursor:not-allowed}.error-box{color:#ffd0d5;border:1px solid #6f3540;background:#27151d;border-radius:8px;padding:10px;display:none;margin-bottom:10px}@media(max-width:1080px){.cards{grid-template-columns:repeat(3,1fr)}.filter-grid{grid-template-columns:repeat(3,1fr)}.field.search{grid-column:1/-1}}@media(max-width:650px){.wrap{padding:18px 12px 36px}.head{display:block}.badge{display:inline-block;margin-top:12px}.cards{grid-template-columns:repeat(2,1fr)}.filter-grid{grid-template-columns:1fr 1fr}.field.search{grid-column:1/-1}.actions .button{flex:1}.button.raw{margin-left:0}.card strong{font-size:16px}}
</style>
</head>
<body>
<main class="wrap">
  <header class="head">
    <div><h1>Leitor forense de logs</h1><p>Firmware independente para investigar o histórico persistente do watchdog.</p></div>
    <div id="mountBadge" class="badge">Verificando partição</div>
  </header>
  <section class="cards">
    <div class="card"><span>Registros válidos</span><strong id="validCount">--</strong></div>
    <div class="card"><span>Slots examinados</span><strong id="slotCount">--</strong></div>
    <div class="card"><span>Registros inválidos</span><strong id="invalidCount">--</strong></div>
    <div class="card"><span>Schema desconhecido</span><strong id="schemaCount">--</strong></div>
    <div class="card"><span>Acesso atual</span><strong id="accessIp">--</strong></div>
  </section>
  <section class="panel">
    <div class="filter-grid">
      <div class="field search"><label for="q">Busca global</label><input id="q" maxlength="80" placeholder="Detalhe, evento, IP, sequência ou status"></div>
      <div class="field"><label for="level">Nível</label><select id="level"><option value="">Todos</option><option value="error">Erro</option><option value="warning">Aviso</option><option value="info">Informação</option><option value="unknown">Desconhecido</option></select></div>
      <div class="field"><label for="category">Categoria</label><select id="category"><option value="">Todas</option><option value="system">Sistema</option><option value="wifi">Wi-Fi</option><option value="camera">Câmera</option><option value="discovery">Descoberta</option><option value="onvif">ONVIF</option><option value="snapshot">Snapshot</option><option value="rtsp">RTSP</option><option value="health">Saúde</option><option value="action">Ação</option><option value="notification">Notificação</option><option value="settings">Configurações</option><option value="unknown">Desconhecida</option></select></div>
      <div class="field"><label for="event">Evento</label><select id="event"><option value="">Todos</option><option>system_boot</option><option>system_restart</option><option>wifi_connect</option><option>wifi_disconnect</option><option>wifi_reconnect</option><option>camera_state</option><option>incident</option><option>health_check</option><option>discovery_probe</option><option>discovery_response</option><option>discovery_result</option><option>subnet_scan</option><option>tcp_scan</option><option>onvif_probe</option><option>onvif_soap</option><option>snapshot_http</option><option>snapshot_transfer</option><option>rtsp_probe</option><option>email_http</option><option>settings_saved</option><option>settings_reset</option><option>admin_action</option><option>camera_reboot</option><option>ptz_command</option><option>sntp_sync</option><option>runtime_context</option><option>settings_context</option><option>camera_target_load</option><option>camera_target_save</option><option>logs_cleared</option><option>unknown</option></select></div>
      <div class="field"><label for="outcome">Resultado</label><select id="outcome"><option value="">Todos</option><option value="failure">Falha</option><option value="success">Sucesso</option><option value="started">Iniciado</option><option value="skipped">Ignorado</option><option value="info">Informativo</option><option value="unknown">Desconhecido</option></select></div>
      <div class="field"><label for="bootId">Boot ID</label><input id="bootId" type="number" min="0" max="65535" placeholder="Todos"></div>
      <div class="field"><label for="contextId">Context ID</label><input id="contextId" type="number" min="0" max="65535" placeholder="Todos"></div>
    </div>
    <div class="actions">
      <button id="apply" class="button primary" type="button">Aplicar filtros</button>
      <button id="clear" class="button" type="button">Limpar filtros</button>
      <a class="button" href="/api/logs/export.csv">Exportar CSV completo</a>
      <a class="button" href="/api/logs/export.jsonl">Exportar JSONL completo</a>
      <a class="button raw" href="/api/logs/raw">Baixar partição RAW</a>
    </div>
  </section>
  <section class="panel">
    <div class="table-head"><h2>Eventos</h2><span id="resultSummary" class="hint">Carregando</span></div>
    <div id="errorBox" class="error-box"></div>
    <div class="table-wrap">
      <table><thead><tr><th>Data e sequência</th><th>Nível</th><th>Evento</th><th>Resultado</th><th>Boot e contexto</th><th>Alvo</th><th>Detalhe</th></tr></thead><tbody id="rows"></tbody></table>
      <div id="empty" class="empty" hidden>Nenhum registro corresponde aos filtros.</div>
    </div>
    <div class="pager"><button id="previous" class="button" type="button">Anterior</button><span id="pageLabel" class="hint"></span><button id="next" class="button" type="button">Próxima</button></div>
  </section>
</main>
<script>
const pageSize=100;let offset=0;let lastTotal=0;
const byId=id=>document.getElementById(id);
const filterIds=['q','level','category','event','outcome','bootId','contextId'];
function setText(id,value){byId(id).textContent=value}
function formatBytes(value){if(value>=1048576)return(value/1048576).toFixed(2)+' MB';if(value>=1024)return(value/1024).toFixed(1)+' KB';return value+' B'}
function statusBadge(text,type){const el=byId('mountBadge');el.textContent=text;el.className='badge '+type}
async function loadStatus(){try{const response=await fetch('/api/status',{cache:'no-store'});const data=await response.json();setText('validCount',data.validCount.toLocaleString('pt-BR'));setText('slotCount',data.slotCount.toLocaleString('pt-BR'));setText('invalidCount',data.invalidCount.toLocaleString('pt-BR'));setText('schemaCount',data.unknownSchemaCount.toLocaleString('pt-BR'));setText('accessIp',data.stationIp||data.accessPointIp||'Sem rede');if(data.fileFound){statusBadge('Logs montados: '+formatBytes(data.fileSize),'ok')}else if(data.rawAvailable){statusBadge(data.mountSkippedAfterReset?'Montagem ignorada após reinício':'Somente RAW disponível','warn')}else{statusBadge('Partição não encontrada','bad')}}catch(error){statusBadge('Falha ao consultar estado','bad')}}
function queryParams(){const params=new URLSearchParams({offset:String(offset),limit:String(pageSize)});for(const id of filterIds){const value=byId(id).value.trim();if(value)params.set(id==='q'?'q':id,value)}return params}
function cell(row,text,className=''){const td=document.createElement('td');td.textContent=text;if(className)td.className=className;row.appendChild(td);return td}
function addSub(cellElement,text){const sub=document.createElement('div');sub.className='sub';sub.textContent=text;cellElement.appendChild(sub)}
function formatTime(entry){if(!entry.epochSeconds)return'Sem horário sincronizado';return new Date(entry.epochSeconds*1000).toLocaleString('pt-BR')}
function renderEntry(entry){const row=document.createElement('tr');let current=cell(row,formatTime(entry));addSub(current,'seq '+entry.sequence+' | slot '+entry.slot+' | schema '+entry.schemaVersion);current=cell(row,entry.level,'level '+entry.level);addSub(current,'código '+entry.levelCode);current=cell(row,entry.event);addSub(current,'categoria '+entry.category+' | códigos '+entry.eventCode+'/'+entry.categoryCode);current=cell(row,entry.outcome);addSub(current,'código '+entry.outcomeCode+' | flags '+entry.flags);current=cell(row,'boot '+entry.bootId+' | contexto '+entry.contextId);addSub(current,'uptime '+entry.uptimeMs+' ms');const target=entry.targetIp?(entry.targetIp+(entry.targetPort?':'+entry.targetPort:'')):'Sem alvo';current=cell(row,target);addSub(current,'HTTP/status '+entry.statusCode+' | duração '+entry.durationMs+' ms | valor '+entry.value);cell(row,entry.detail||'Sem detalhe','detail');return row}
function showError(message){const box=byId('errorBox');box.textContent=message;box.style.display=message?'block':'none'}
async function loadLogs(){byId('apply').disabled=true;showError('');try{const response=await fetch('/api/logs?'+queryParams(),{cache:'no-store'});const data=await response.json();if(!response.ok)throw new Error(data.error||'Falha ao consultar os logs.');lastTotal=data.total;const tbody=byId('rows');tbody.replaceChildren(...data.entries.map(renderEntry));byId('empty').hidden=data.entries.length!==0;const first=data.total===0?0:data.offset+1;const last=Math.min(data.offset+data.returned,data.total);setText('resultSummary',first+'–'+last+' de '+data.total.toLocaleString('pt-BR')+' registros');setText('pageLabel','Página '+(Math.floor(data.offset/pageSize)+1));byId('previous').disabled=data.offset===0;byId('next').disabled=!data.hasMore}catch(error){byId('rows').replaceChildren();byId('empty').hidden=true;showError(error.message);setText('resultSummary','Consulta indisponível');byId('previous').disabled=true;byId('next').disabled=true}finally{byId('apply').disabled=false}}
function applyFilters(){offset=0;loadLogs()}
byId('apply').addEventListener('click',applyFilters);byId('clear').addEventListener('click',()=>{for(const id of filterIds)byId(id).value='';applyFilters()});byId('previous').addEventListener('click',()=>{offset=Math.max(0,offset-pageSize);loadLogs()});byId('next').addEventListener('click',()=>{if(offset+pageSize<lastTotal){offset+=pageSize;loadLogs()}});byId('q').addEventListener('keydown',event=>{if(event.key==='Enter')applyFilters()});
loadStatus();loadLogs();setInterval(loadStatus,10000);
</script>
</body>
</html>)HTML";

}
