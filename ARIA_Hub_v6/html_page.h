#pragma once
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ARIA Hub</title>
<style>
:root{--bg:#f4f6f9;--white:#fff;--border:#e0e4ea;--ink:#181c22;--muted:#5a6475;--blue:#0058be;--blue-lt:#ddeaff;--red:#c0001a;--red-lt:#ffe0e3;--green:#006944;--green-lt:#d4f4e6;--orange:#d97706;--orange-lt:#fff3cd;--yellow:#ca9500;--yellow-lt:#fff9c4}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:"Segoe UI",Arial,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh}
.hdr{display:flex;align-items:center;gap:12px;padding:0 20px;height:54px;background:var(--white);border-bottom:1px solid var(--border);position:sticky;top:0;z-index:100;box-shadow:0 1px 4px rgba(0,0,0,.07)}
.hdr-brand{font-size:17px;font-weight:900;letter-spacing:-.02em;flex:1;color:var(--ink)}
.tabs{display:flex;gap:4px}
.tab{padding:7px 16px;border-radius:8px;border:1px solid var(--border);background:var(--white);font-weight:700;cursor:pointer;font-size:13px;color:var(--muted)}
.tab.active{background:var(--blue);color:#fff;border-color:var(--blue)}
.page{display:none;padding:16px;max-width:1200px;margin:0 auto}
.page.active{display:block}
.card{background:var(--white);border:1px solid var(--border);border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
.card-title{font-size:14px;font-weight:700;margin-bottom:10px;color:var(--ink)}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:12px}
.zcard{background:var(--white);border:1px solid var(--border);border-radius:12px;padding:12px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
.zcard-top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:8px}
.zicon{width:36px;height:36px;border-radius:9px;display:flex;align-items:center;justify-content:center;font-size:18px;flex-shrink:0}
.zbadge{padding:3px 9px;border-radius:999px;font-size:10px;font-weight:700;text-transform:uppercase}
.zbadge.safe{background:var(--green-lt);color:var(--green)}.zbadge.warning{background:var(--orange-lt);color:var(--orange)}.zbadge.fire{background:var(--red-lt);color:var(--red)}.zbadge.critical{background:var(--red);color:#fff}
.zname{font-size:13px;font-weight:600;color:var(--muted);margin-bottom:3px}
.zscore{font-size:26px;font-weight:900;line-height:1}
.zscore-sub{font-size:13px;font-weight:600;color:#bbb}
.safe{color:var(--green)}.warning{color:var(--orange)}.fire{color:var(--red)}.critical{color:#7a0010}
.node-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:12px}
.ncard{background:var(--white);border:1px solid var(--border);border-radius:10px;overflow:hidden}
.ncard-hdr{display:flex;justify-content:space-between;align-items:center;padding:7px 12px;border-bottom:1px solid var(--border);font-size:13px;font-weight:700}
.ncard-hdr.live{background:#f0fdf8}.ncard-hdr.forced{background:#fffbeb}.ncard-hdr.stale{background:#fefce8}.ncard-hdr.offline{background:#fff1f2}
.nstatus{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.04em}
.nstatus.live{color:var(--green)}.nstatus.forced{color:var(--orange)}.nstatus.stale{color:#ca8a04}.nstatus.offline{color:var(--red)}
.ncard-body{display:grid;grid-template-columns:1fr 1fr;gap:4px;padding:9px 12px;font-size:11px}
.nrow{display:flex;flex-direction:column;gap:1px}
.nlabel{color:var(--muted);text-transform:uppercase;font-size:9px;font-weight:700;letter-spacing:.05em}
.nval{font-weight:700;font-size:13px;color:var(--ink)}
.nval.danger{color:var(--red)}.nval.warn{color:var(--orange)}.nval.ok{color:var(--green)}
.ndivider{grid-column:1/-1;height:1px;background:var(--border);margin:2px 0}
.pills{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px}
.pill{padding:5px 13px;border-radius:999px;background:var(--bg);border:1px solid var(--border);font-weight:700;font-size:12px;color:var(--muted)}
.pill.on.F{background:var(--red-lt);color:var(--red);border-color:var(--red)}.pill.on.G{background:var(--yellow-lt);color:var(--yellow);border-color:var(--yellow)}.pill.on.S{background:#dbeafe;color:#1e40af;border-color:#93c5fd}
#mapCanvas{width:100%;display:block;border-radius:10px;border:1px solid var(--border)}
.routerow{padding:7px 12px;border-radius:8px;margin-bottom:5px;border:1px solid var(--border);background:var(--white);font-size:13px}
.terminal{background:#0f1117;border-radius:10px;font-family:monospace;font-size:11px;padding:10px;height:200px;overflow-y:auto;color:#94a3b8}
.terminal div{padding:1px 0;border-bottom:1px solid rgba(255,255,255,.03)}
.chatbox{background:var(--white);border:1px solid var(--border);border-radius:10px;font-size:12px;padding:10px 12px;height:150px;overflow-y:auto}
.chatbox .chat{background:#dbeafe;border-radius:6px;padding:3px 8px;margin-bottom:3px;color:#1e3a8a;font-weight:600}
.inrow{display:flex;gap:8px;margin-top:8px}
input[type=text]{flex:1;padding:8px 12px;border-radius:8px;border:1px solid var(--border);background:var(--white);color:var(--ink);font-size:13px;outline:none}
input[type=text]:focus{border-color:var(--blue);box-shadow:0 0 0 2px rgba(0,88,190,.15)}
button{padding:8px 15px;border:0;border-radius:8px;background:var(--blue);color:#fff;font-weight:700;cursor:pointer;font-size:13px}
button:hover{opacity:.88}
select{padding:7px 12px;border-radius:8px;border:1px solid var(--border);font-size:13px;color:var(--ink);background:var(--white)}
.connbar{display:grid;grid-template-columns:repeat(6,1fr);gap:6px}
.cbadge{display:block;text-align:center;padding:7px 4px;border-radius:8px;background:var(--bg);font-size:12px;border:1px solid var(--border);font-weight:700}
.cbadge.live{background:var(--green-lt);color:var(--green)}.cbadge.forced{background:var(--orange-lt);color:var(--orange)}.cbadge.off{background:var(--red-lt);color:var(--red)}
.demobar{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px}
.dbtn{padding:7px 13px;border:1px solid var(--border);border-radius:8px;background:var(--white);cursor:pointer;font-size:13px;font-weight:600;color:var(--ink)}
.dbtn:hover{background:var(--bg)}.dbtn.red{background:var(--red-lt);color:var(--red);border-color:rgba(192,0,26,.2)}.dbtn.clr{background:var(--green-lt);color:var(--green);border-color:rgba(0,105,68,.2)}
.legend{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}
.lg{padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700}
.lgN{background:var(--green-lt);color:var(--green)}.lgE{background:var(--red-lt);color:var(--red)}.lgO{background:var(--orange-lt);color:var(--orange)}.lgY{background:var(--yellow-lt);color:var(--yellow)}
#sosCard{border:2px solid #93c5fd}
@media(max-width:700px){.grid3,.node-grid{grid-template-columns:1fr 1fr}.connbar{grid-template-columns:repeat(3,1fr)}}
@media(max-width:480px){.grid3{grid-template-columns:1fr}.tabs{gap:2px}.tab{padding:6px 10px;font-size:12px}}
</style>
</head>
<body>
<div class="hdr">
<span class="hdr-brand">ARIA Hub</span>
<div class="tabs">
<div class="tab active" onclick="show('warden')">Warden</div>
<div class="tab" onclick="show('tech')">Technical</div>
<div class="tab" onclick="show('demo')">Demo/Test</div>
</div>
</div>

<div class="page active" id="page-warden">
<div class="legend">
<span class="lg lgN">&#9679; Safe</span>
<span class="lg lgE">&#9679; Fire</span>
<span class="lg lgO">&#9679; Congested</span>
<span class="lg lgY">&#9679; Gas</span>
</div>
<div class="grid3" id="wZones"></div>
<div class="card"><div class="pills" id="wAlerts"></div></div>
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
<div class="card-title">Exit Sensors (LDR)</div>
<div style="font-size:11px;color:var(--muted);margin-bottom:8px">LDR raw ADC — beam blocked when value drops below threshold. Use LDRTHRESH &lt;n&gt; in serial to tune.</div>
<div id="exitSensors"></div>
</div>
<div class="card">
<div class="card-title">System Log</div>
<div class="terminal" id="sysLog"></div>
</div>
</div>

<div class="page" id="page-demo">
<div class="card">
<div class="card-title">Node Mapping</div>
<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
<select id="nodeMapSel">
<option value="0">-- clear --</option>
<option value="1">Node 1 — Zone A</option>
<option value="2">Node 2 — Zone B</option>
<option value="3">Node 3 — Zone B</option>
<option value="4">Node 4 — Zone C</option>
<option value="5">Node 5 — Zone A</option>
<option value="6">Node 6 — Zone C</option>
</select>
<button onclick="applyNodeMap()">Apply</button>
<span id="mapResult" style="font-size:13px;color:var(--green)"></span>
</div>
<div style="margin-top:8px;font-size:13px;color:var(--muted)">Pending: <b id="pendingMap">none</b></div>
</div>
<div class="card">
<div class="card-title">Fire Scenarios</div>
<div class="demobar">
<button class="dbtn red" onclick="sc('NODE 1 7')">N1 Fire</button>
<button class="dbtn red" onclick="sc('NODE 2 7')">N2 Fire</button>
<button class="dbtn red" onclick="sc('NODE 3 7')">N3 Fire</button>
<button class="dbtn red" onclick="sc('NODE 4 7')">N4 Fire</button>
<button class="dbtn red" onclick="sc('NODE 5 7')">N5 Fire</button>
<button class="dbtn red" onclick="sc('NODE 6 7')">N6 Fire</button>
</div>
<div class="demobar">
<button class="dbtn red" onclick="sc('NODE 1 9')">N1 Critical</button>
<button class="dbtn red" onclick="sc('NODE 4 9')">N4 Critical</button>
<button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 4 7'),200)">Zone A+C Fire</button>
<button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 2 7'),200);setTimeout(()=>sc('NODE 4 7'),400)">All Fire</button>
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
<button class="dbtn" onclick="sc('GAS 1 ON')">Gas N1</button>
<button class="dbtn clr" onclick="sc('GAS 1 OFF')">Gas N1 Off</button>
<button class="dbtn" onclick="sc('SOS 1 ON')">SOS N1</button>
<button class="dbtn clr" onclick="sc('SOS 1 OFF')">SOS N1 Off</button>
</div>
</div>
<div class="card">
<div class="card-title">Custom Command</div>
<div style="font-size:11px;color:var(--muted);margin-bottom:6px">NODE &lt;id&gt; &lt;score&gt; | GAS &lt;id&gt; ON/OFF | SOS &lt;id&gt; ON/OFF | FLAME &lt;id&gt; ON/OFF | CONGEST A/B/C | CLEAR | STATUS</div>
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
let ws,lastState=null;
function connect(){
  ws=new WebSocket('ws://'+location.host+'/ws');
  ws.onmessage=e=>{lastState=JSON.parse(e.data);render(lastState);};
  ws.onerror=()=>ws.close();
  ws.onclose=()=>setTimeout(connect,800);
}
function send(msg){if(ws&&ws.readyState===1)ws.send(msg);}
function sc(cmd){send(cmd);}
function sendChat(){let v=document.getElementById('chatIn').value.trim();if(v){send(v);document.getElementById('chatIn').value='';}}
function sendDemoCmd(){let v=document.getElementById('demoCmd').value.trim();if(v){send(v);document.getElementById('demoCmd').value='';}}
function applyNodeMap(){
  let v=document.getElementById('nodeMapSel').value;
  send('SETNODE '+v);
  let r=document.getElementById('mapResult');
  r.textContent=v==='0'?'Cleared':'Mapping → Node '+v;
  setTimeout(()=>{r.textContent='';},3000);
}
function show(name){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  document.querySelectorAll('.tab').forEach(t=>{if(t.textContent.trim().toLowerCase().startsWith(name.substring(0,4)))t.classList.add('active');});
}
document.addEventListener('keydown',e=>{
  if(e.key==='Enter'){
    if(document.activeElement.id==='chatIn')sendChat();
    if(document.activeElement.id==='demoCmd')sendDemoCmd();
  }
});
function cls(s){s=(s||'').toLowerCase();return s==='critical'?'critical':s==='fire'?'fire':s==='warning'?'warning':'safe';}
function zBg(s){const c=cls(s);if(c==='critical')return['#ffdad6','#7a0010'];if(c==='fire')return['#ffe0e3','#c0001a'];if(c==='warning')return['#fff3cd','#d97706'];return['#d4f4e6','#006944'];}
function zIconBg(s){const c=cls(s);if(c==='critical'||c==='fire')return'#ffcdd2';if(c==='warning')return'#fde68a';return'#bbf7d0';}
function zIcon(s){const c=cls(s);if(c==='critical')return'&#128308;';if(c==='fire')return'&#128293;';if(c==='warning')return'&#9888;';return'&#9989;';}
function zoneHtml(zones){
  return zones.map(z=>{
    let[bg,col]=zBg(z.status),st=cls(z.status);
    return'<div class="zcard" style="background:'+bg+';border-color:'+bg+'">'
      +'<div class="zcard-top"><div class="zicon" style="background:'+zIconBg(z.status)+'">'+zIcon(z.status)+'</div>'
      +'<span class="zbadge '+st+'">'+z.status+'</span></div>'
      +'<div class="zname" style="color:'+col+'">Zone '+z.name+'</div>'
      +'<div class="zscore" style="color:'+col+'">'+z.score+'<span class="zscore-sub">/10</span></div>'
      +'</div>';
  }).join('');
}
let shownChatTotal=0;
function updateChat(chatArr,chatTotal){
  if(!chatArr)return;
  let box=document.getElementById('chatBox');
  if(chatTotal<shownChatTotal){box.innerHTML='';shownChatTotal=0;}
  if(chatTotal>shownChatTotal){
    let newCount=chatTotal-shownChatTotal,startAt=Math.max(0,chatArr.length-newCount);
    for(let i=startAt;i<chatArr.length;i++){
      let line=chatArr[i];
      if(line.startsWith('[CHAT]')){let div=document.createElement('div');div.className='chat';div.textContent='\u{1F4AC} '+line.substring(7);box.appendChild(div);box.scrollTop=box.scrollHeight;}
    }
    shownChatTotal=chatTotal;
  }
}
let animFrame=0,renderState=null;
function drawMap(){
  animFrame++;
  let cvs=document.getElementById('mapCanvas');
  if(!cvs||!renderState){requestAnimationFrame(drawMap);return;}
  let s=renderState,zones=s.zn||[],exits=s.ex||[];
  let dpr=window.devicePixelRatio||1,W=cvs.offsetWidth,H=370;
  if(cvs.width!==Math.round(W*dpr)||cvs.height!==Math.round(H*dpr)){cvs.width=Math.round(W*dpr);cvs.height=Math.round(H*dpr);cvs.style.height=H+'px';}
  let ctx=cvs.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle='#f8fafc';ctx.fillRect(0,0,W,H);
  let exSideW=Math.max(62,Math.min(80,Math.floor(W*0.13))),exSideH=52;
  let innerW=W-2*exSideW,roomW=Math.floor(innerW/4),roomH=78,roomY=16;
  let corrH=54,corrY=roomY+roomH,corrMidY=corrY+Math.floor(corrH/2);
  let vertW=54,vertX=Math.floor(W/2-vertW/2),vertY=corrY+corrH,vertH=86;
  let exBW=70,exBH=36,exBX=Math.floor(W/2-exBW/2),exBY=vertY+vertH;
  let innerX=exSideW;
  function zoneCol(idx){
    let z=zones[idx];
    if(!z)return{fill:'#f1f5f9',bdr:'#cbd5e1',txt:'#94a3b8',ptc:'#94a3b8',lbl:'STANDBY'};
    let st=(z.status||'').toLowerCase(),score=z.score!==undefined?z.score:'–';
    if(st==='fire'||st==='critical')return{fill:'#fef2f2',bdr:'#f87171',txt:'#b91c1c',ptc:'#ef4444',lbl:'FIRE',score};
    if(st==='warning')return{fill:'#fffbeb',bdr:'#fbbf24',txt:'#92400e',ptc:'#f59e0b',lbl:'WARN',score};
    if(st==='safe')return{fill:'#f0fdf4',bdr:'#4ade80',txt:'#15803d',ptc:'#22c55e',lbl:'SAFE',score};
    return{fill:'#f8fafc',bdr:'#94a3b8',txt:'#64748b',ptc:'#94a3b8',lbl:'STANDBY',score};
  }
  let zA=zoneCol(0),zB=zoneCol(1),zC=zoneCol(2);
  function rr(x,y,w,h,r){ctx.beginPath();ctx.moveTo(x+r,y);ctx.lineTo(x+w-r,y);ctx.arcTo(x+w,y,x+w,y+r,r);ctx.lineTo(x+w,y+h-r);ctx.arcTo(x+w,y+h,x+w-r,y+h,r);ctx.lineTo(x+r,y+h);ctx.arcTo(x,y+h,x,y+h-r,r);ctx.lineTo(x,y+r);ctx.arcTo(x,y,x+r,y,r);ctx.closePath();}
  ctx.fillStyle='#e2e8f0';ctx.fillRect(0,corrY,W,corrH);ctx.fillRect(vertX,vertY,vertW,vertH);
  let topRooms=[{x:innerX,col:zA,lbl:'N1',zone:'A'},{x:innerX+roomW,col:zB,lbl:'N2',zone:'B'},{x:innerX+2*roomW,col:zB,lbl:'N3',zone:'B'},{x:innerX+3*roomW,col:zC,lbl:'N4',zone:'C'}];
  topRooms.forEach(rm=>{
    ctx.fillStyle=rm.col.fill;rr(rm.x,roomY,roomW,roomH,8);ctx.fill();
    ctx.strokeStyle=rm.col.bdr;ctx.lineWidth=2;ctx.stroke();
    ctx.textAlign='center';ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif';ctx.fillText(rm.lbl,rm.x+roomW/2,roomY+28);
    ctx.font='11px system-ui,sans-serif';ctx.fillText('Zone '+rm.zone,rm.x+roomW/2,roomY+46);
    if(rm.col.score!==undefined){rr(rm.x+roomW/2-20,roomY+52,40,20,4);ctx.fillStyle=rm.col.bdr;ctx.fill();ctx.fillStyle='#fff';ctx.font='bold 11px system-ui,sans-serif';ctx.fillText(rm.col.score+'/10',rm.x+roomW/2,roomY+66);}
  });
  let lRoomW=Math.min(86,Math.floor((vertX-innerX)*0.62)),lRoomH=66,lRoomY=corrY+corrH+10;
  let n5X=innerX+Math.floor((vertX-innerX-lRoomW)/2),n6X=vertX+vertW+Math.floor((W-exSideW-vertX-vertW-lRoomW)/2);
  [{x:n5X,col:zA,lbl:'N5',zone:'A'},{x:n6X,col:zC,lbl:'N6',zone:'C'}].forEach(rm=>{
    ctx.fillStyle=rm.col.fill;rr(rm.x,lRoomY,lRoomW,lRoomH,8);ctx.fill();
    ctx.strokeStyle=rm.col.bdr;ctx.lineWidth=2;ctx.stroke();
    ctx.textAlign='center';ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif';ctx.fillText(rm.lbl,rm.x+lRoomW/2,lRoomY+26);
    ctx.font='11px system-ui,sans-serif';ctx.fillText('Zone '+rm.zone,rm.x+lRoomW/2,lRoomY+44);
    if(rm.col.score!==undefined){rr(rm.x+lRoomW/2-18,lRoomY+49,36,17,4);ctx.fillStyle=rm.col.bdr;ctx.fill();ctx.fillStyle='#fff';ctx.font='bold 10px system-ui,sans-serif';ctx.fillText(rm.col.score+'/10',rm.x+lRoomW/2,lRoomY+61);}
  });
  let congA=exits[0]&&exits[0].cg,congB=exits[1]&&exits[1].cg,congC=exits[2]&&exits[2].cg;
  let fA=zones[0]&&(zones[0].status||'').match(/fire|critical/i);
  let fB=zones[1]&&(zones[1].status||'').match(/fire|critical/i);
  let fC=zones[2]&&(zones[2].status||'').match(/fire|critical/i);
  function exitBox(x,y,w,h,lbl,cong,cut,ldrVal){
    ctx.fillStyle=cong?'#fef3c7':cut?'#fefce8':'#dcfce7';
    ctx.strokeStyle=cong?'#f59e0b':cut?'#eab308':'#22c55e';ctx.lineWidth=1.5;
    rr(x,y,w,h,6);ctx.fill();ctx.stroke();
    ctx.fillStyle=cong?'#92400e':cut?'#713f12':'#14532d';ctx.textAlign='center';
    ctx.font='bold 10px system-ui,sans-serif';ctx.fillText(lbl,x+w/2,y+h/2-6);
    ctx.font='9px system-ui,sans-serif';ctx.fillText(cong?'CONGESTED':cut?'CROSSING':'OPEN',x+w/2,y+h/2+5);
    if(ldrVal!==undefined){ctx.fillStyle='#94a3b8';ctx.font='8px system-ui,sans-serif';ctx.fillText('LDR:'+ldrVal,x+w/2,y+h/2+16);}
  }
  let cutA=exits[0]&&exits[0].bc,cutB=exits[1]&&exits[1].bc,cutC=exits[2]&&exits[2].bc;
  let ldrA=exits[0]&&exits[0].ldr,ldrB=exits[1]&&exits[1].ldr,ldrC=exits[2]&&exits[2].ldr;
  exitBox(0,corrMidY-exSideH/2,exSideW,exSideH,'Exit A',congA,cutA,ldrA);
  exitBox(W-exSideW,corrMidY-exSideH/2,exSideW,exSideH,'Exit C',congC,cutC,ldrC);
  exitBox(exBX,exBY,exBW,exBH,'Exit B',congB,cutB,ldrB);
  const SPEED=1.2;
  function _sL(a,b){let dx=b[0]-a[0],dy=b[1]-a[1];return Math.sqrt(dx*dx+dy*dy);}
  function _pL(p){let l=0;for(let i=0;i<p.length-1;i++)l+=_sL(p[i],p[i+1]);return l;}
  function _pt(p,dist){let r=dist;for(let i=0;i<p.length-1;i++){let s=_sL(p[i],p[i+1]);if(r<=s){let t=s>0?r/s:0;return[p[i][0]+(p[i+1][0]-p[i][0])*t,p[i][1]+(p[i+1][1]-p[i][1])*t];}r-=s;}return[p[p.length-1][0],p[p.length-1][1]];}
  function drawParticles(path,color){
    let total=_pL(path);if(total<1)return;
    let head=(animFrame*SPEED)%total,dte=total-head;
    let pkt=dte<total*0.2?3:dte<total*0.4?2:1,gap=9;
    for(let p=0;p<pkt;p++){let d=head-p*gap;if(d<0)break;let[px,py]=_pt(path,d);let ga=p===0?'cc':'88';let g=ctx.createRadialGradient(px,py,0,px,py,9);g.addColorStop(0,color+ga);g.addColorStop(1,color+'00');ctx.fillStyle=g;ctx.beginPath();ctx.arc(px,py,9,0,Math.PI*2);ctx.fill();ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,3.5,0,Math.PI*2);ctx.fill();}
  }
  let zA_cx=innerX+roomW/2,zC_cx=W-exSideW-roomW/2,midX=W/2;
  let exAcx=exSideW/2,exCcx=W-exSideW/2,exBcy=exBY+exBH/2;
  let n2cx=midX-roomW/2,n3cx=midX+roomW/2;
  let pA=[[zA_cx,corrMidY],[exAcx,corrMidY]];
  let pB_N2=[[n2cx,corrMidY],[midX,corrMidY],[midX,exBcy]];
  let pB_N3=[[n3cx,corrMidY],[midX,corrMidY],[midX,exBcy]];
  let pC=[[zC_cx,corrMidY],[exCcx,corrMidY]];
  let pAB=[[zA_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];
  let pAC=[[zA_cx,corrMidY],[exCcx,corrMidY]];
  let pBA=[[midX,corrMidY],[exAcx,corrMidY]];
  let pBC=[[midX,corrMidY],[exCcx,corrMidY]];
  let pCB=[[zC_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];
  let pCA=[[zC_cx,corrMidY],[exAcx,corrMidY]];
  let gcA=congA?'#f59e0b':'#22c55e',gcB=congB?'#f59e0b':'#22c55e',gcC=congC?'#f59e0b':'#22c55e',allF=fA&&fB&&fC;
  if(!fA)drawParticles(pA,gcA);else if(allF)drawParticles(pA,'#ef4444');else if(!fB)drawParticles(pAB,'#ef4444');else drawParticles(pAC,'#ef4444');
  if(!fB){drawParticles(pB_N2,gcB);drawParticles(pB_N3,gcB);}else if(allF){drawParticles(pB_N2,'#ef4444');drawParticles(pB_N3,'#ef4444');}else if(!fA)drawParticles(pBA,'#ef4444');else drawParticles(pBC,'#ef4444');
  if(!fC)drawParticles(pC,gcC);else if(allF)drawParticles(pC,'#ef4444');else if(!fB)drawParticles(pCB,'#ef4444');else drawParticles(pCA,'#ef4444');
  requestAnimationFrame(drawMap);
}

let _el={};
function g(id){return _el[id]||(_el[id]=document.getElementById(id));}
function setTxt(id,v){let e=g(id);if(e){let s=String(v);if(e.textContent!==s)e.textContent=s;}}
function setHdr(id,v){let e=g(id);if(e&&e.innerHTML!==v)e.innerHTML=v;}
function setCls(id,c){let e=g(id);if(e&&e.className!==c)e.className=c;}

function buildShells(){
  let badges='',cards='';
  for(let i=1;i<=6;i++){
    badges+='<span class="cbadge off" id="cb-'+i+'">Node '+i+'<br><small id="cbs-'+i+'">OFFLINE</small></span>';
    cards+='<div class="ncard">'
      +'<div class="ncard-hdr offline" id="nch-'+i+'">'
      +'<span>Node '+i+' <small id="nca-'+i+'" style="font-weight:400;opacity:.7"></small></span>'
      +'<span class="nstatus offline" id="ncs-'+i+'">NO DATA</span>'
      +'</div>'
      +'<div class="ncard-body">'
      +'<div style="color:var(--muted);font-size:13px" id="ncw-'+i+'">Waiting…</div>'
      +'<div id="ncd-'+i+'" style="display:none">'
      +'<div class="nrow"><span class="nlabel">Score</span><span class="nval ok" id="nsc-'+i+'">-</span></div>'
      +'<div class="nrow"><span class="nlabel">CO</span><span class="nval" id="nco-'+i+'">-</span></div>'
      +'<div class="ndivider"></div>'
      +'<div class="nrow"><span class="nlabel">Temp</span><span class="nval" id="ntm-'+i+'">-</span></div>'
      +'<div class="nrow"><span class="nlabel">Humidity</span><span class="nval" id="nhm-'+i+'">-</span></div>'
      +'<div class="nrow"><span class="nlabel">MQ-2</span><span class="nval" id="nmq-'+i+'">-</span></div>'
      +'<div class="nrow"><span class="nlabel">MQ-135</span><span class="nval" id="nm1-'+i+'">-</span></div>'
      +'<div class="nrow"><span class="nlabel">Flame</span><span class="nval ok" id="nfl-'+i+'">No</span></div>'
      +'<div class="nrow"><span class="nlabel">Gas</span><span class="nval ok" id="ngs-'+i+'">No</span></div>'
      +'<div class="ndivider"></div>'
      +'<div class="nrow"><span class="nlabel">ESP-NOW pkts</span><span class="nval" id="npk-'+i+'">0</span></div>'
      +'</div>'
      +'</div>'
      +'</div>';
  }
  document.getElementById('connBar').innerHTML=badges;
  document.getElementById('nodeCards').innerHTML=cards;
  _el={};
}

let _prevZoneKey='',_prevLogKey='';
function render(s){
  renderState=s;
  let zoneKey=(s.zn||[]).map(z=>z.name+z.status+z.score).join('|');
  if(zoneKey!==_prevZoneKey){_prevZoneKey=zoneKey;let zh=zoneHtml(s.zn||[]);document.getElementById('wZones').innerHTML=zh;document.getElementById('tZones').innerHTML=zh;}
  setHdr('wAlerts','<span class="pill '+(s.al.fire?'on F':'')+'">Fire</span><span class="pill '+(s.al.gas?'on G':'')+'">Gas</span><span class="pill '+(s.al.sos?'on S':'')+'">SOS</span>');
  let sosCard=g('sosCard');
  if(sosCard){sosCard.style.borderColor=s.al.sos?'#c0001a':'#93c5fd';sosCard.style.background=s.al.sos?'linear-gradient(135deg,#ffe0e3,#ffd0d4)':'';}
  setTxt('sosHead',s.al.sos?'SOS ACTIVE':'No SOS active');
  setTxt('sosLoc',s.sosL||'none');
  let exits=s.ex||[];
  setHdr('routeText',(s.zn||[]).map(z=>{
    let st=cls(z.status),fire=st==='fire'||st==='critical',idx=z.name.charCodeAt(0)-65;
    let cong=exits[idx]&&exits[idx].cg;
    let own=z.name==='A'?'Exit A':z.name==='B'?'Exit B':'Exit C';
    let away=z.name==='A'?'Exit B or C':z.name==='B'?'Exit A or C':'Exit A or B';
    return'<div class="routerow"><b>Zone '+z.name+':</b> '+(fire?'Move away &mdash; '+away:'Proceed to '+own)+(cong?' <span style="color:var(--orange)">(congested)</span>':'')+'</div>';
  }).join(''));
  let nodes=s.nd||[];
  setTxt('connSummary',nodes.filter(n=>n.on).length+' node(s) transmitting');
  nodes.forEach(n=>{
    let i=n.id,everSeen=n.age>=0,hCls,sTxt;
    if(n.on){hCls=n.fc?'forced':'live';sTxt=n.fc?'FORCED':'LIVE';}
    else if(n.fc){hCls='forced';sTxt='FORCED';}
    else if(everSeen){hCls='stale';sTxt='LAST KNOWN';}
    else if(n.wt){hCls='offline';sTxt='WAITING';}
    else{hCls='offline';sTxt='NO DATA';}
    let ageStr=!n.on&&everSeen?(n.age<60?n.age+'s ago':Math.floor(n.age/60)+'m ago'):'';
    setCls('cb-'+i,'cbadge '+(n.on?'live':n.fc?'forced':'off'));
    setTxt('cbs-'+i,n.on?'LIVE':n.wt?'WAITING':'OFFLINE');
    setCls('nch-'+i,'ncard-hdr '+hCls);setCls('ncs-'+i,'nstatus '+hCls);setTxt('ncs-'+i,sTxt);
    setTxt('nca-'+i,ageStr?'('+ageStr+')':'');
    setTxt('ncw-'+i,n.wt?'WAITING FOR LIVE DATA…':'Waiting…');
    let waitEl=g('ncw-'+i),dataEl=g('ncd-'+i);
    if(!everSeen&&!n.fc){if(waitEl)waitEl.style.display='';if(dataEl)dataEl.style.display='none';}
    else{
      if(waitEl)waitEl.style.display='none';if(dataEl)dataEl.style.display='';
      setCls('nsc-'+i,'nval '+(n.score>=5?'danger':n.score>=3?'warn':'ok'));setTxt('nsc-'+i,n.score+'/10');
      setTxt('nco-'+i,n.co+' ppm');setTxt('ntm-'+i,n.temp+'°C');setTxt('nhm-'+i,n.hum+'%');
      setTxt('nmq-'+i,n.mq2);setTxt('nm1-'+i,n.mq135);
      setCls('nfl-'+i,'nval '+(n.fl?'danger':'ok'));setTxt('nfl-'+i,n.fl?'YES':'No');
      setCls('ngs-'+i,'nval '+(n.gas?'warn':'ok'));setTxt('ngs-'+i,n.gas?'YES':'No');
      setTxt('npk-'+i,n.pk||0);
    }
  });
  let exArr=s.ex||[];
  setHdr('exitSensors',exArr.map(e=>{
    let st=e.ob?'OBSTRUCTION':e.cg?'CONGESTED':e.bc?'CROSSING':'OPEN';
    let col=e.ob?'var(--red)':e.cg?'var(--orange)':e.bc?'var(--yellow)':'var(--green)';
    let ldrNote=e.ldr!==undefined?(' — LDR: <b>'+e.ldr+'</b>'):'';
    return'<div style="display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid var(--border);font-size:12px">'
      +'<b>Exit '+e.name+'</b>'
      +'<span style="color:'+col+';font-weight:700">'+st+'</span>'
      +'<span style="color:var(--muted)">'+e.pc+' persons/30s</span>'
      +'<span style="color:var(--muted)">'+ldrNote+'</span>'
      +'</div>';
  }).join(''));
  let logArr=(s.lg||[]).filter(Boolean),logKey=logArr.join('\x00');
  if(logKey!==_prevLogKey){
    _prevLogKey=logKey;
    let sl=g('sysLog');
    sl.innerHTML=logArr.map(l=>{
      let col=(l.includes('FIRE')||l.includes('CRITICAL'))?'color:#f87171':(l.includes('GAS')||l.includes('WARN'))?'color:#fbbf24':(l.includes('Ready')||l.includes('OK'))?'color:#34d399':'';
      return'<div'+(col?' style="'+col+'"':'')+'>&gt; '+l+'</div>';
    }).join('');
    sl.scrollTop=sl.scrollHeight;
  }
  updateChat(s.ch||[],s.ct||0);
  setTxt('pendingMap',(s.pm||0)>0?'Node '+s.pm:'none');
}

buildShells();
requestAnimationFrame(drawMap);
connect();
</script>
</body>
</html>
)rawliteral";
