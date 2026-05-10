#pragma once
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ARIA Hub</title>
  <style>
    :root{
      --bg:#f4f6f9;--white:#ffffff;--border:#e0e4ea;
      --ink:#181c22;--muted:#5a6475;
      --blue:#0058be;--blue-lt:#ddeaff;
      --red:#c0001a;--red-lt:#ffe0e3;
      --green:#006944;--green-lt:#d4f4e6;
      --orange:#d97706;--orange-lt:#fff3cd;
      --yellow:#ca9500;--yellow-lt:#fff9c4;
    }
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:"Segoe UI",Arial,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh}

    /* â”€â”€ HEADER â”€â”€ */
    .hdr{display:flex;align-items:center;gap:12px;padding:0 20px;height:62px;
      background:var(--white);border-bottom:1px solid var(--border);
      position:sticky;top:0;z-index:100;box-shadow:0 1px 4px rgba(0,0,0,.07)}
    .hdr-brand{font-size:19px;font-weight:900;letter-spacing:-.02em;flex:1;color:var(--ink)}
    .tabs{display:flex;gap:4px}
    .tab{padding:8px 18px;border-radius:8px;border:1px solid var(--border);
      background:var(--white);font-weight:700;cursor:pointer;font-size:13px;color:var(--muted)}
    .tab.active{background:var(--blue);color:#fff;border-color:var(--blue)}

    /* â”€â”€ PAGE LAYOUT â”€â”€ */
    .page{display:none;padding:20px;max-width:1200px;margin:0 auto}
    .page.active{display:block}

    /* â”€â”€ CARDS â”€â”€ */
    .card{background:var(--white);border:1px solid var(--border);border-radius:12px;
      padding:16px;margin-bottom:14px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
    .card-title{font-size:15px;font-weight:700;margin-bottom:12px;color:var(--ink)}

    /* â”€â”€ ZONE CARDS â”€â”€ */
    .grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:14px}
    .zcard{background:var(--white);border:1px solid var(--border);border-radius:12px;
      padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
    .zcard-top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:8px}
    .zicon{width:38px;height:38px;border-radius:9px;display:flex;align-items:center;
      justify-content:center;font-size:19px;flex-shrink:0}
    .zbadge{padding:3px 9px;border-radius:999px;font-size:10px;font-weight:700;text-transform:uppercase}
    .zbadge.safe{background:var(--green-lt);color:var(--green)}
    .zbadge.warning{background:var(--orange-lt);color:var(--orange)}
    .zbadge.fire{background:var(--red-lt);color:var(--red)}
    .zbadge.critical{background:var(--red);color:#fff}
    .zname{font-size:13px;font-weight:600;color:var(--muted);margin-bottom:3px}
    .zscore{font-size:28px;font-weight:900;line-height:1}
    .zscore-sub{font-size:13px;font-weight:600;color:#bbb}
    .safe{color:var(--green)}.warning{color:var(--orange)}.fire{color:var(--red)}.critical{color:#7a0010}

    /* â”€â”€ NODE CARDS â”€â”€ */
    .node-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:14px}
    .ncard{background:var(--white);border:1px solid var(--border);border-radius:10px;overflow:hidden}
    .ncard-hdr{display:flex;justify-content:space-between;align-items:center;
      padding:8px 12px;border-bottom:1px solid var(--border);font-size:13px;font-weight:700}
    .ncard-hdr.live{background:#f0fdf8}.ncard-hdr.forced{background:#fffbeb}.ncard-hdr.stale{background:#fefce8}.ncard-hdr.offline{background:#fff1f2}
    .nstatus{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.04em}
    .nstatus.live{color:var(--green)}.nstatus.forced{color:var(--orange)}.nstatus.stale{color:#ca8a04}.nstatus.offline{color:var(--red)}
    .ncard-body{display:grid;grid-template-columns:1fr 1fr;gap:5px;padding:10px 12px;font-size:11px}
    .nrow{display:flex;flex-direction:column;gap:1px}
    .nlabel{color:var(--muted);text-transform:uppercase;font-size:9px;font-weight:700;letter-spacing:.05em}
    .nval{font-weight:700;font-size:13px;color:var(--ink)}
    .nval.danger{color:var(--red)}.nval.warn{color:var(--orange)}.nval.ok{color:var(--green)}
    .ndivider{grid-column:1/-1;height:1px;background:var(--border);margin:2px 0}

    /* â”€â”€ ALERT PILLS â”€â”€ */
    .pills{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
    .pill{padding:6px 14px;border-radius:999px;background:var(--bg);border:1px solid var(--border);
      font-weight:700;font-size:12px;color:var(--muted)}
    .pill.on.F{background:var(--red-lt);color:var(--red);border-color:var(--red)}
    .pill.on.G{background:var(--yellow-lt);color:var(--yellow);border-color:var(--yellow)}
    .pill.on.S{background:#dbeafe;color:#1e40af;border-color:#93c5fd}

    /* â”€â”€ FLOOR MAP â”€â”€ */
    /* â”€â”€ MAP CANVAS â”€â”€ */
    #mapCanvas{width:100%;display:block;border-radius:10px;border:1px solid var(--border)}

    /* â”€â”€ ROUTE TEXT â”€â”€ */
    .routerow{padding:8px 12px;border-radius:8px;margin-bottom:6px;
      border:1px solid var(--border);background:var(--white);font-size:13px}

    /* â”€â”€ TERMINAL (system log) â”€â”€ */
    .terminal{background:#0f1117;border-radius:10px;font-family:monospace;font-size:11px;
      padding:12px;height:220px;overflow-y:auto;color:#94a3b8}
    .terminal div{padding:1px 0;border-bottom:1px solid rgba(255,255,255,.03)}

    /* â”€â”€ CHAT BOX â”€â”€ */
    .chatbox{background:var(--white);border:1px solid var(--border);border-radius:10px;
      font-size:12px;padding:10px 12px;height:160px;overflow-y:auto}
    .chatbox .chat{background:#dbeafe;border-radius:6px;padding:3px 8px;
      margin-bottom:3px;color:#1e3a8a;font-weight:600}

    /* â”€â”€ INPUTS â”€â”€ */
    .inrow{display:flex;gap:8px;margin-top:8px}
    input[type=text]{flex:1;padding:9px 12px;border-radius:8px;border:1px solid var(--border);
      background:var(--white);color:var(--ink);font-size:13px;outline:none}
    input[type=text]:focus{border-color:var(--blue);box-shadow:0 0 0 2px rgba(0,88,190,.15)}
    button{padding:9px 16px;border:0;border-radius:8px;background:var(--blue);
      color:#fff;font-weight:700;cursor:pointer;font-size:13px}
    button:hover{opacity:.88}
    select{padding:8px 12px;border-radius:8px;border:1px solid var(--border);
      font-size:13px;color:var(--ink);background:var(--white)}

    /* â”€â”€ CONN BAR â”€â”€ */
    .connbar{display:grid;grid-template-columns:repeat(6,1fr);gap:6px}
    .cbadge{display:block;text-align:center;padding:7px 4px;border-radius:8px;
      background:var(--bg);font-size:12px;border:1px solid var(--border);font-weight:700}
    .cbadge.live{background:var(--green-lt);color:var(--green)}
    .cbadge.forced{background:var(--orange-lt);color:var(--orange)}
    .cbadge.stale{background:#fef9c3;color:#a16207}
    .cbadge.off{background:var(--red-lt);color:var(--red)}

    /* â”€â”€ DEMO BUTTONS â”€â”€ */
    .demobar{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px}
    .dbtn{padding:8px 14px;border:1px solid var(--border);border-radius:8px;
      background:var(--white);cursor:pointer;font-size:13px;font-weight:600;color:var(--ink)}
    .dbtn:hover{background:var(--bg)}
    .dbtn.red{background:var(--red-lt);color:var(--red);border-color:rgba(192,0,26,.2)}
    .dbtn.clr{background:var(--green-lt);color:var(--green);border-color:rgba(0,105,68,.2)}

    /* â”€â”€ LEGEND â”€â”€ */
    .legend{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
    .lg{padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700}
    .lgN{background:var(--green-lt);color:var(--green)}
    .lgE{background:var(--red-lt);color:var(--red)}
    .lgO{background:var(--orange-lt);color:var(--orange)}
    .lgY{background:var(--yellow-lt);color:var(--yellow)}

    /* â”€â”€ SOS CARD â”€â”€ */
    #sosCard{border:2px solid #93c5fd}

    /* â”€â”€ RESPONSIVE â”€â”€ */
    @media(max-width:700px){.grid3,.node-grid{grid-template-columns:1fr 1fr}.connbar{grid-template-columns:repeat(3,1fr)}}
    @media(max-width:480px){.grid3{grid-template-columns:1fr}.tabs{gap:2px}.tab{padding:6px 10px;font-size:12px}}
  </style>
</head>
<body>

<div class="hdr">
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 108" width="42" height="42" aria-label="ARIA">
  <circle cx="50" cy="62" r="43" fill="#eef1f6"/>
  <g stroke="#3d7ec9" stroke-width="1.8" fill="none">
    <line x1="50" y1="22" x2="78" y2="38"/><line x1="78" y1="38" x2="78" y2="70"/>
    <line x1="78" y1="70" x2="50" y2="86"/><line x1="50" y1="86" x2="22" y2="70"/>
    <line x1="22" y1="70" x2="22" y2="38"/><line x1="22" y1="38" x2="50" y2="22"/>
    <line x1="50" y1="22" x2="50" y2="54"/><line x1="78" y1="38" x2="50" y2="54"/>
    <line x1="78" y1="70" x2="50" y2="54"/><line x1="50" y1="86" x2="50" y2="54"/>
    <line x1="22" y1="70" x2="50" y2="54"/><line x1="22" y1="38" x2="50" y2="54"/>
  </g>
  <circle cx="22" cy="38" r="5.5" fill="#3d7ec9"/>
  <circle cx="78" cy="70" r="5.5" fill="#3d7ec9"/>
  <circle cx="50" cy="86" r="5.5" fill="#3d7ec9"/>
  <circle cx="22" cy="70" r="5.5" fill="#3d7ec9"/>
  <circle cx="50" cy="22" r="5.5" fill="#f07828"/>
  <circle cx="78" cy="38" r="5.5" fill="#f07828"/>
  <circle cx="50" cy="54" r="7"   fill="#f07828"/>
  <text x="50" y="10" text-anchor="middle" font-family="Arial,sans-serif" font-weight="900" font-size="13" fill="#f07828" letter-spacing="4">ARIA</text>
</svg>
  <span class="hdr-brand">ARIA Hub</span>
  <div class="tabs">
    <div class="tab active" onclick="show('warden')">Warden</div>
    <div class="tab" onclick="show('tech')">Technical</div>
    <div class="tab" onclick="show('demo')">Demo / Test</div>
  </div>
</div>

<!-- â•â•â• WARDEN PAGE â•â•â• -->
<div class="page active" id="page-warden">

  <div class="legend">
    <span class="lg lgN">&#9679; Safe route</span>
    <span class="lg lgE">&#9679; Fire &mdash; route away</span>
    <span class="lg lgO">&#9679; Congested exit</span>
    <span class="lg lgY">&#9679; Gas leak</span>
  </div>

  <div class="grid3" id="wZones"></div>

  <div class="card">
    <div class="pills" id="wAlerts"></div>
  </div>

  <div class="card" id="sosCard">
    <div class="card-title">SOS Status</div>
    <b id="sosHead" style="font-size:15px">No SOS active</b>
    <div style="font-size:13px;color:var(--muted);margin-top:5px">Location: <span id="sosLoc">none</span></div>
  </div>

  <div class="card">
    <div class="card-title">Live Floor Map &amp; Escape Route</div>
    <canvas id="mapCanvas"></canvas>
    <div id="routeText" style="margin-top:10px"></div>
  </div>

  <div class="card">
    <div class="card-title">Warden Chat</div>
    <div class="chatbox" id="chatBox"></div>
    <div class="inrow">
      <input type="text" id="chatIn" placeholder="Message wardens...">
      <button onclick="sendChat()">Send</button>
    </div>
  </div>
</div>

<!-- â•â•â• TECHNICAL PAGE â•â•â• -->
<div class="page" id="page-tech">

  <div class="grid3" id="tZones"></div>

  <div class="card">
    <div class="card-title">Node Connections</div>
    <div id="connSummary" style="font-size:13px;color:var(--muted);margin-bottom:8px"></div>
    <div class="connbar" id="connBar"></div>
  </div>

  <div class="card">
    <div class="card-title">Node Detail</div>
    <div class="node-grid" id="nodeCards"></div>
  </div>

  <div class="card">
    <div class="card-title">System Log</div>
    <div class="terminal" id="sysLog"></div>
  </div>
</div>

<!-- â•â•â• DEMO / TEST PAGE â•â•â• -->
<div class="page" id="page-demo">

  <div class="card">
    <div class="card-title">Hub Node Mapping</div>
    <p style="font-size:13px;color:var(--muted);margin-bottom:10px">
      Map the next incoming unassigned packet to a specific node ID.
      Nodes default to Node&nbsp;1 until reassigned via serial or this control.
    </p>
    <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
      <select id="nodeMapSel">
        <option value="0">-- clear mapping --</option>
        <option value="1">Node 1 &mdash; Zone A / Exit A</option>
        <option value="2">Node 2 &mdash; Zone B / Exit B</option>
        <option value="3">Node 3 &mdash; Zone B / Exit B</option>
        <option value="4">Node 4 &mdash; Zone C / Exit C</option>
        <option value="5">Node 5 &mdash; Zone A / Exit A</option>
        <option value="6">Node 6 &mdash; Zone C / Exit C</option>
      </select>
      <button onclick="applyNodeMap()">Apply Mapping</button>
      <span id="mapResult" style="font-size:13px;color:var(--green)"></span>
    </div>
    <div style="margin-top:8px;font-size:13px;color:var(--muted)">Pending map: <b id="pendingMap">none</b></div>
  </div>

  <div class="card">
    <div class="card-title">Fire Scenarios</div>
    <div class="demobar">
      <button class="dbtn red" onclick="sc('NODE 1 7')">Node 1 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 2 7')">Node 2 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 3 7')">Node 3 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 4 7')">Node 4 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 5 7')">Node 5 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 6 7')">Node 6 Fire</button>
    </div>
    <div class="demobar">
      <button class="dbtn red" onclick="sc('NODE 1 9')">Node 1 Critical</button>
      <button class="dbtn red" onclick="sc('NODE 4 9')">Node 4 Critical</button>
      <button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 4 7'),200)">Zone A+C Fire</button>
      <button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 2 7'),200);setTimeout(()=>sc('NODE 4 7'),400)">All Zones Fire</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Exit Congestion</div>
    <div class="demobar">
      <button class="dbtn" onclick="sc('CONGEST A')">Congest A</button>
      <button class="dbtn" onclick="sc('CONGEST B')">Congest B</button>
      <button class="dbtn" onclick="sc('CONGEST C')">Congest C</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST A')">Clear A</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST B')">Clear B</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST C')">Clear C</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Gas / SOS</div>
    <div class="demobar">
      <button class="dbtn" onclick="sc('GAS 1 ON')">Gas Node 1</button>
      <button class="dbtn clr" onclick="sc('GAS 1 OFF')">Gas Node 1 Off</button>
      <button class="dbtn" onclick="sc('SOS 1 ON')">SOS Node 1</button>
      <button class="dbtn clr" onclick="sc('SOS 1 OFF')">SOS Node 1 Off</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Custom Command</div>
    <p style="font-size:12px;color:var(--muted);margin-bottom:8px">
      NODE &lt;id&gt; &lt;score&gt; | SETSCORE &lt;id&gt; &lt;0-10&gt; | GAS &lt;id&gt; ON/OFF |
      SOS &lt;id&gt; ON/OFF | FLAME &lt;id&gt; ON/OFF | NODE &lt;id&gt; CLEAR |
      NODECMD &lt;id&gt; &lt;cmd&gt; | SETIR A/B/C/ALL | CLEAR | STATUS
    </p>
    <div class="inrow">
      <input type="text" id="demoCmd" placeholder="e.g. SETSCORE 2 6">
      <button onclick="sendDemoCmd()">Send</button>
    </div>
    <div style="margin-top:8px">
      <button class="dbtn clr" onclick="sc('CLEAR')">Clear All State</button>
    </div>
  </div>
</div>

<script>
let ws, lastState = null;

function connect(){
  ws = new WebSocket('ws://'+location.host+'/ws');
  ws.onmessage = e => { lastState = JSON.parse(e.data); render(lastState); };
  ws.onclose   = () => setTimeout(connect, 1500);
}
function send(msg){ if(ws && ws.readyState===1) ws.send(msg); }
function sc(cmd){ send(cmd); }
function sendChat(){
  let v = document.getElementById('chatIn').value.trim();
  if(v){ send(v); document.getElementById('chatIn').value=''; }
}
function sendDemoCmd(){
  let v = document.getElementById('demoCmd').value.trim();
  if(v){ send(v); document.getElementById('demoCmd').value=''; }
}
function applyNodeMap(){
  let v = document.getElementById('nodeMapSel').value;
  send('SETNODE ' + v);
  let r = document.getElementById('mapResult');
  r.textContent = v === '0' ? 'Mapping cleared — unassigned nodes default to Node 1' : 'All unassigned packets → Node ' + v + ' (until next change)';
  setTimeout(()=>{ r.textContent=''; }, 4000);
}

function show(name){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  document.querySelectorAll('.tab').forEach(t => {
    if(t.textContent.trim().toLowerCase().startsWith(name.substring(0,4)))
      t.classList.add('active');
  });
}
document.addEventListener('keydown', e => {
  if(e.key === 'Enter'){
    if(document.activeElement.id === 'chatIn') sendChat();
    if(document.activeElement.id === 'demoCmd') sendDemoCmd();
  }
});

function cls(s){
  s = (s||'').toLowerCase();
  return s==='critical'?'critical':s==='fire'?'fire':s==='warning'?'warning':'safe';
}
function zBg(s){
  const c = cls(s);
  if(c==='critical') return ['#ffdad6','#7a0010'];
  if(c==='fire')     return ['#ffe0e3','#c0001a'];
  if(c==='warning')  return ['#fff3cd','#d97706'];
  return ['#d4f4e6','#006944'];
}
function zIconBg(s){
  const c = cls(s);
  if(c==='critical'||c==='fire') return '#ffcdd2';
  if(c==='warning') return '#fde68a';
  return '#bbf7d0';
}
function zIcon(s){
  const c = cls(s);
  if(c==='critical') return '&#128308;';
  if(c==='fire') return '&#128293;';
  if(c==='warning') return '&#9888;';
  return '&#9989;';
}

function zoneHtml(zones){
  return zones.map(z => {
    let [bg,col] = zBg(z.status);
    let st = cls(z.status);
    return '<div class="zcard" style="background:'+bg+';border-color:'+bg+'">'
      +'<div class="zcard-top">'
      +'<div class="zicon" style="background:'+zIconBg(z.status)+'">'+zIcon(z.status)+'</div>'
      +'<span class="zbadge '+st+'">'+z.status+'</span>'
      +'</div>'
      +'<div class="zname" style="color:'+col+'">Zone '+z.name+'</div>'
      +'<div class="zscore" style="color:'+col+'">'+z.score+'<span class="zscore-sub">/10</span></div>'
      +'</div>';
  }).join('');
}

let shownChatTotal = 0;
function updateChat(chatArr, chatTotal){
  if(!chatArr) return;
  let box = document.getElementById('chatBox');
  if(chatTotal < shownChatTotal){
    box.innerHTML = '';
    shownChatTotal = 0;
  }
  if(chatTotal > shownChatTotal){
    let newCount = chatTotal - shownChatTotal;
    let startAt = Math.max(0, chatArr.length - newCount);
    for(let i = startAt; i < chatArr.length; i++){
      let line = chatArr[i];
      if(line.startsWith('[CHAT]')){
        let div = document.createElement('div');
        div.className = 'chat';
        div.textContent = '\u{1F4AC} ' + line.substring(7);
        box.appendChild(div);
        box.scrollTop = box.scrollHeight;
      }
    }
    shownChatTotal = chatTotal;
  }
}

let animFrame = 0;
let renderState = null;

function drawMap(){
  animFrame++;
  let cvs = document.getElementById('mapCanvas');
  if(!cvs || !renderState){ requestAnimationFrame(drawMap); return; }

  let s = renderState;
  let zones = s.zones || [];
  let exits = s.exits || [];

  let dpr = window.devicePixelRatio || 1;
  let W = cvs.offsetWidth;
  let H = 370;
  if(cvs.width !== Math.round(W*dpr) || cvs.height !== Math.round(H*dpr)){
    cvs.width = Math.round(W*dpr); cvs.height = Math.round(H*dpr);
    cvs.style.height = H+'px';
  }
  let ctx = cvs.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);

  ctx.fillStyle='#f8fafc'; ctx.fillRect(0,0,W,H);

  // ── Layout ────────────────────────────────────────────────────────────────
  // Exit A (left) and Exit C (right) are flush with the sides of the corridor.
  // 4 top rooms span the inner width between the exits.
  // A vertical corridor drops from the centre of the horizontal corridor to Exit B.
  // N5 (Zone A) and N6 (Zone C) sit either side of the vertical section.
  let exSideW=Math.max(62,Math.min(80,Math.floor(W*0.13)));
  let exSideH=52;
  let innerX=exSideW;
  let innerW=W-2*exSideW;
  let roomW=Math.floor(innerW/4);
  let roomH=78, roomY=16;
  let corrH=54;
  let corrY=roomY+roomH;
  let corrMidY=corrY+Math.floor(corrH/2);
  let vertW=54;
  let vertX=Math.floor(W/2-vertW/2);
  let vertY=corrY+corrH;
  let vertH=86;
  let exBW=70, exBH=36;
  let exBX=Math.floor(W/2-exBW/2), exBY=vertY+vertH;

  // Zone colours
  function zoneCol(idx){
    let z=zones[idx];
    if(!z) return {fill:'#f1f5f9',bdr:'#cbd5e1',txt:'#94a3b8',ptc:'#94a3b8',lbl:'STANDBY'};
    let st=(z.status||'').toLowerCase();
    let score=z.score!==undefined?z.score:'–';
    if(st==='fire'||st==='critical') return {fill:'#fef2f2',bdr:'#f87171',txt:'#b91c1c',ptc:'#ef4444',lbl:'FIRE ⚠',score};
    if(st==='warning') return {fill:'#fffbeb',bdr:'#fbbf24',txt:'#92400e',ptc:'#f59e0b',lbl:'WARN',score};
    if(st==='safe')    return {fill:'#f0fdf4',bdr:'#4ade80',txt:'#15803d',ptc:'#22c55e',lbl:'SAFE ✓',score};
    return {fill:'#f8fafc',bdr:'#94a3b8',txt:'#64748b',ptc:'#94a3b8',lbl:'STANDBY',score};
  }
  let zA=zoneCol(0), zB=zoneCol(1), zC=zoneCol(2);

  function rr(x,y,w,h,r){
    ctx.beginPath();
    ctx.moveTo(x+r,y);ctx.lineTo(x+w-r,y);ctx.arcTo(x+w,y,x+w,y+r,r);
    ctx.lineTo(x+w,y+h-r);ctx.arcTo(x+w,y+h,x+w-r,y+h,r);
    ctx.lineTo(x+r,y+h);ctx.arcTo(x,y+h,x,y+h-r,r);
    ctx.lineTo(x,y+r);ctx.arcTo(x,y,x+r,y,r);
    ctx.closePath();
  }

  // ── Corridors ─────────────────────────────────────────────────────────────
  ctx.fillStyle='#e2e8f0';
  ctx.fillRect(0, corrY, W, corrH);           // full-width horizontal corridor
  ctx.fillRect(vertX, vertY, vertW, vertH);   // vertical section down to Exit B

  // ── Top rooms: N1 ZoneA, N2 ZoneB, N3 ZoneB, N4 ZoneC ───────────────────
  let topRooms=[
    {x:innerX,         col:zA, lbl:'N1', zone:'A'},
    {x:innerX+roomW,   col:zB, lbl:'N2', zone:'B'},
    {x:innerX+2*roomW, col:zB, lbl:'N3', zone:'B'},
    {x:innerX+3*roomW, col:zC, lbl:'N4', zone:'C'},
  ];
  topRooms.forEach(rm=>{
    ctx.fillStyle=rm.col.fill; rr(rm.x,roomY,roomW,roomH,8); ctx.fill();
    ctx.strokeStyle=rm.col.bdr; ctx.lineWidth=2; ctx.stroke();
    ctx.textAlign='center'; ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif'; ctx.fillText(rm.lbl, rm.x+roomW/2, roomY+28);
    ctx.font='11px system-ui,sans-serif'; ctx.fillText('Zone '+rm.zone, rm.x+roomW/2, roomY+46);
    if(rm.col.score!==undefined){
      rr(rm.x+roomW/2-20,roomY+52,40,20,4); ctx.fillStyle=rm.col.bdr; ctx.fill();
      ctx.fillStyle='#fff'; ctx.font='bold 11px system-ui,sans-serif';
      ctx.fillText(rm.col.score+'/10', rm.x+roomW/2, roomY+66);
    }
  });

  // ── Lower rooms: N5 ZoneA (left of vertical), N6 ZoneC (right) ───────────
  let lRoomW=Math.min(86, Math.floor((vertX-innerX)*0.62));
  let lRoomH=66, lRoomY=corrY+corrH+10;
  let n5X=innerX+Math.floor((vertX-innerX-lRoomW)/2);
  let n6X=vertX+vertW+Math.floor((W-exSideW-vertX-vertW-lRoomW)/2);
  [{x:n5X,col:zA,lbl:'N5',zone:'A'},{x:n6X,col:zC,lbl:'N6',zone:'C'}].forEach(rm=>{
    ctx.fillStyle=rm.col.fill; rr(rm.x,lRoomY,lRoomW,lRoomH,8); ctx.fill();
    ctx.strokeStyle=rm.col.bdr; ctx.lineWidth=2; ctx.stroke();
    ctx.textAlign='center'; ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif'; ctx.fillText(rm.lbl, rm.x+lRoomW/2, lRoomY+26);
    ctx.font='11px system-ui,sans-serif'; ctx.fillText('Zone '+rm.zone, rm.x+lRoomW/2, lRoomY+44);
    if(rm.col.score!==undefined){
      rr(rm.x+lRoomW/2-18,lRoomY+49,36,17,4); ctx.fillStyle=rm.col.bdr; ctx.fill();
      ctx.fillStyle='#fff'; ctx.font='bold 10px system-ui,sans-serif';
      ctx.fillText(rm.col.score+'/10', rm.x+lRoomW/2, lRoomY+61);
    }
  });

  // ── Exit boxes ────────────────────────────────────────────────────────────
  let congA=exits[0]&&exits[0].congested;
  let congB=exits[1]&&exits[1].congested;
  let congC=exits[2]&&exits[2].congested;
  let fA=zones[0]&&(zones[0].status||'').match(/fire|critical/i);
  let fB=zones[1]&&(zones[1].status||'').match(/fire|critical/i);
  let fC=zones[2]&&(zones[2].status||'').match(/fire|critical/i);

  function exitBox(x,y,w,h,lbl,cong){
    ctx.fillStyle=cong?'#fef3c7':'#dcfce7';
    ctx.strokeStyle=cong?'#f59e0b':'#22c55e'; ctx.lineWidth=1.5;
    rr(x,y,w,h,6); ctx.fill(); ctx.stroke();
    ctx.fillStyle=cong?'#92400e':'#14532d'; ctx.textAlign='center';
    ctx.font='bold 10px system-ui,sans-serif'; ctx.fillText(lbl, x+w/2, y+h/2-3);
    ctx.font='9px system-ui,sans-serif'; ctx.fillText(cong?'⚠ CONGESTED':'● OPEN', x+w/2, y+h/2+9);
  }
  exitBox(0, corrMidY-exSideH/2, exSideW, exSideH, 'Exit A', congA);
  exitBox(W-exSideW, corrMidY-exSideH/2, exSideW, exSideH, 'Exit C', congC);
  exitBox(exBX, exBY, exBW, exBH, 'Exit B', congB);

  // ── Particle system: path-length-based, uniform speed ─────────────────────
  const SPEED=1.2;
  function _sL(a,b){let dx=b[0]-a[0],dy=b[1]-a[1];return Math.sqrt(dx*dx+dy*dy);}
  function _pL(p){let l=0;for(let i=0;i<p.length-1;i++)l+=_sL(p[i],p[i+1]);return l;}
  function _pt(p,dist){
    let r=dist;
    for(let i=0;i<p.length-1;i++){
      let s=_sL(p[i],p[i+1]);
      if(r<=s){let t=s>0?r/s:0;return[p[i][0]+(p[i+1][0]-p[i][0])*t,p[i][1]+(p[i+1][1]-p[i][1])*t];}
      r-=s;
    }
    return[p[p.length-1][0],p[p.length-1][1]];
  }
  function drawParticles(path,color){
    let total=_pL(path);if(total<1)return;
    let head=(animFrame*SPEED)%total;
    let dte=total-head;                         // distance left to exit
    // 1 dot far from exit → 2 within 40% → 3 within 20% (mirrors physical LED)
    let pkt=dte<total*0.2?3:dte<total*0.4?2:1;
    let gap=9;                                  // pixel spacing inside the packet
    for(let p=0;p<pkt;p++){
      let d=head-p*gap; if(d<0)break;
      let[px,py]=_pt(path,d);
      let ga=p===0?'cc':'88';                   // lead dot glows slightly more
      let g=ctx.createRadialGradient(px,py,0,px,py,9);
      g.addColorStop(0,color+ga);g.addColorStop(1,color+'00');
      ctx.fillStyle=g;ctx.beginPath();ctx.arc(px,py,9,0,Math.PI*2);ctx.fill();
      ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,3.5,0,Math.PI*2);ctx.fill();
    }
  }

  // ── Path waypoints ─────────────────────────────────────────────────────────
  // All paths run along the corridor centreline (corrMidY) or vertical centreline (W/2).
  // Zone A column centre in corridor: innerX + roomW/2
  // Zone B column centre: W/2 (also the vertical corridor centre)
  // Zone C column centre: W - exSideW - roomW/2
  let zA_cx = innerX + roomW/2;
  let zC_cx = W - exSideW - roomW/2;
  let midX   = W/2;                    // Zone B / vertical corridor centre x
  let exAcx  = exSideW/2;             // Exit A centre x
  let exCcx  = W - exSideW/2;         // Exit C centre x
  let exBcy  = exBY + exBH/2;         // Exit B centre y

  // Zone B node centres in corridor (N2 left of junction, N3 right)
  let n2cx = midX - roomW/2;
  let n3cx = midX + roomW/2;

  // Normal routes
  let pA    = [[zA_cx,corrMidY],[exAcx,corrMidY]];                             // ZoneA → left → ExitA
  let pB_N2 = [[n2cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                  // ZoneB N2 → junction → ExitB
  let pB_N3 = [[n3cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                  // ZoneB N3 → junction → ExitB
  let pC    = [[zC_cx,corrMidY],[exCcx,corrMidY]];                             // ZoneC → right → ExitC
  // Redirect routes
  let pAB = [[zA_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                   // ZoneA → right → down → ExitB
  let pAC = [[zA_cx,corrMidY],[exCcx,corrMidY]];                               // ZoneA → right → ExitC
  let pBA = [[midX,corrMidY],[exAcx,corrMidY]];                                // ZoneB → left → ExitA
  let pBC = [[midX,corrMidY],[exCcx,corrMidY]];                                // ZoneB → right → ExitC
  let pCB = [[zC_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                   // ZoneC → left → down → ExitB
  let pCA = [[zC_cx,corrMidY],[exAcx,corrMidY]];                               // ZoneC → left → ExitA

  let gcA=congA?'#f59e0b':'#22c55e';
  let gcB=congB?'#f59e0b':'#22c55e';
  let gcC=congC?'#f59e0b':'#22c55e';
  let allF=fA&&fB&&fC;

  // Zone A
  if(!fA)       drawParticles(pA,  gcA);
  else if(allF) drawParticles(pA,  '#ef4444');
  else if(!fB)  drawParticles(pAB, '#ef4444');
  else          drawParticles(pAC, '#ef4444');
  // Zone B — N2 and N3 each run from their corridor position to the junction then down
  if(!fB)      { drawParticles(pB_N2, gcB);        drawParticles(pB_N3, gcB);        }
  else if(allF){ drawParticles(pB_N2, '#ef4444');   drawParticles(pB_N3, '#ef4444'); }
  else if(!fA)   drawParticles(pBA,   '#ef4444');
  else           drawParticles(pBC,   '#ef4444');
  // Zone C
  if(!fC)       drawParticles(pC,  gcC);
  else if(allF) drawParticles(pC,  '#ef4444');
  else if(!fB)  drawParticles(pCB, '#ef4444');
  else          drawParticles(pCA, '#ef4444');

  requestAnimationFrame(drawMap);
}

function render(s){
  renderState = s;

  let zh = zoneHtml(s.zones||[]);
  document.getElementById('wZones').innerHTML = zh;
  document.getElementById('tZones').innerHTML = zh;

  let ap =
    '<span class="pill '+(s.alerts.fire?'on F':'')+'">Fire</span>'
   +'<span class="pill '+(s.alerts.gas?'on G':'')+'">Gas Leak</span>'
   +'<span class="pill '+(s.alerts.sos?'on S':'')+'">SOS</span>';
  document.getElementById('wAlerts').innerHTML = ap;

  let sosCard = document.getElementById('sosCard');
  sosCard.style.borderColor = s.alerts.sos ? '#c0001a' : '#93c5fd';
  sosCard.style.background  = s.alerts.sos ? 'linear-gradient(135deg,#ffe0e3,#ffd0d4)' : '';
  document.getElementById('sosHead').textContent = s.alerts.sos ? 'SOS ACTIVE' : 'No SOS active';
  document.getElementById('sosLoc').textContent  = s.sosLocations || 'none';

  let exits = s.exits||[];
  let rh = (s.zones||[]).map(z => {
    let st=cls(z.status), fire=st==='fire'||st==='critical';
    let i=z.name.charCodeAt(0)-65;
    let cong=exits[i]&&exits[i].congested;
    let own=z.name==='A'?'Exit A (left)':z.name==='B'?'Exit B (down)':'Exit C (right)';
    let away=z.name==='A'?'Exit B (down) or Exit C (right)':z.name==='B'?'Exit A (left) or Exit C (right)':'Exit A (left) or Exit B (down)';
    return '<div class="routerow"><b>Zone '+z.name+':</b> '+(fire?'Move away &mdash; '+away:'Proceed to '+own)
      +(cong?' <span style="color:var(--orange)">('+own+' congested)</span>':'')+'</div>';
  }).join('');
  document.getElementById('routeText').innerHTML = rh;

  let nodes = s.nodes||[];
  let live = nodes.filter(n=>n.online).length;
  document.getElementById('connSummary').textContent = live+' node(s) currently transmitting';
  document.getElementById('connBar').innerHTML = nodes.map(n => {
    let connected = n.online;
    let bc = connected ? 'live' : n.forced ? 'forced' : 'off';
    let bt = connected ? 'LIVE' : 'OFFLINE';
    return '<span class="cbadge '+bc+'">Node '+n.id+'<br><small>'+bt+'</small></span>';
  }).join('');

  document.getElementById('nodeCards').innerHTML = nodes.map(n => {
    let everSeen = n.age >= 0;
    let hCls, sTxt;
    if(n.online)       { hCls = n.forced ? 'forced' : 'live';    sTxt = n.forced ? 'FORCED' : 'LIVE'; }
    else if(n.forced)  { hCls = 'forced';  sTxt = 'FORCED'; }
    else if(everSeen)  { hCls = 'stale';   sTxt = 'LAST KNOWN'; }
    else               { hCls = 'offline'; sTxt = 'NO DATA'; }
    let ageStr = n.online ? '' : everSeen ? (n.age<60 ? n.age+'s ago' : Math.floor(n.age/60)+'m ago') : '';
    let sCls = n.score>=8?'danger':n.score>=5?'danger':n.score>=3?'warn':'ok';
    let dash = !everSeen && !n.forced;
    return '<div class="ncard">'
      +'<div class="ncard-hdr '+hCls+'">'
      +'<span>Node '+n.id+(ageStr?' <small style="font-weight:400;opacity:.7">('+ageStr+')</small>':'')+'</span>'
      +'<span class="nstatus '+hCls+'">'+sTxt+'</span>'
      +'</div>'
      +'<div class="ncard-body">'
      +(dash
        ?'<div class="nrow" style="color:var(--muted);font-size:13px">Waiting for first packet&hellip;</div>'
        :'<div class="nrow"><span class="nlabel">Score</span><span class="nval '+sCls+'">'+n.score+'/10</span></div>'
        +'<div class="nrow"><span class="nlabel">CO</span><span class="nval">'+n.co+' ppm</span></div>'
        +'<div class="ndivider"></div>'
        +'<div class="nrow"><span class="nlabel">Temp</span><span class="nval">'+n.temp+'&deg;C</span></div>'
        +'<div class="nrow"><span class="nlabel">Humidity</span><span class="nval">'+n.hum+'%</span></div>'
        +'<div class="nrow"><span class="nlabel">MQ-2</span><span class="nval">'+n.mq2+'</span></div>'
        +'<div class="nrow"><span class="nlabel">MQ-135</span><span class="nval">'+n.mq135+'</span></div>'
        +'<div class="nrow"><span class="nlabel">Flame</span><span class="nval '+(n.flame?'danger':'ok')+'">'+(n.flame?'YES':'No')+'</span></div>'
        +'<div class="nrow"><span class="nlabel">Gas</span><span class="nval '+(n.gas?'warn':'ok')+'">'+(n.gas?'YES':'No')+'</span></div>'
      )
      +'</div>'
      +'</div>';
  }).join('');

  let logArr = (s.log||[]).filter(Boolean);
  let sl = document.getElementById('sysLog');
  sl.innerHTML = logArr.map(l => {
    let col = (l.includes('FIRE')||l.includes('CRITICAL')) ? 'color:#f87171'
            : (l.includes('GAS')||l.includes('WARN')) ? 'color:#fbbf24'
            : (l.includes('Ready')||l.includes('OK')) ? 'color:#34d399' : '';
    return '<div'+(col?' style="'+col+'"':'')+'>&gt; '+l+'</div>';
  }).join('');
  sl.scrollTop = sl.scrollHeight;

  updateChat(s.chat||[], s.chatTotal||0);

  let pm = s.pendingMap||0;
  document.getElementById('pendingMap').textContent = pm>0 ? 'Node '+pm : 'none';
}

requestAnimationFrame(drawMap);
connect();
</script>
</body>
</html>
)rawliteral";
