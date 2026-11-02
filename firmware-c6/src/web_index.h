#pragma once
// The web page served by the board (kept small: it lives in flash).
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>GivControl</title>
<style>
:root{--bg:#0e1217;--card:#24262a;--card2:#2e3136;--ink:#e8ecf1;--ink2:#c3cad4;--mut:#9aa4b1;--line:#363a40;--track:#363a40;--acc:#3b9eff;--good:#34c759;--warn:#ffb020;--bad:#ff5a5f}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;font-feature-settings:"tnum" 0;-webkit-font-smoothing:antialiased}
header{position:sticky;top:0;z-index:5;background:#0e1217e6;backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.hd{max-width:1080px;margin:0 auto;padding:12px 16px;display:flex;align-items:center;gap:10px}
.logo{width:26px;height:26px;border-radius:8px;background:var(--acc);display:grid;place-items:center;color:#fff;font-weight:700;font-size:13px}
h1{font-size:16px;margin:0;font-weight:650}.sp{flex:1}
.chip{font-size:12px;font-weight:650;padding:3px 10px;border-radius:99px;border:1px solid currentColor;white-space:nowrap}
.upd{font-size:13px;color:var(--mut)}@media(max-width:480px){.upd{display:none}}
main{max-width:1080px;margin:0 auto;padding:16px;display:grid;gap:16px}
.card{background:var(--card);border-radius:18px;padding:20px;min-width:0}
.k{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.06em;font-weight:650;margin:0 0 10px;display:flex;justify-content:space-between;gap:8px}
.grid3{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}
.flow{display:grid;grid-template-columns:minmax(220px,280px) 1fr;gap:16px}@media(max-width:720px){.flow{grid-template-columns:1fr}}
.ring{display:flex;flex-direction:column;align-items:center;justify-content:center}
.ring svg{width:176px;height:176px}.ring .st{font-weight:650;font-size:16px}.ring .sw2{color:var(--ink2);font-size:14px}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.stat{background:var(--card);border-radius:16px;padding:14px 16px;min-width:0}
.stat .k{margin-bottom:2px}.stat .v{font-size:clamp(18px,2.1vw,22px);font-weight:650;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.stat .s{font-size:13px;color:var(--mut)}
.good{color:var(--good)}.warn{color:var(--warn)}.bad{color:var(--bad)}.mut{color:var(--mut)}.acc{color:var(--acc)}
.banner{border-radius:12px;padding:11px 14px;font-size:14px;border:1px solid;background:var(--card)}
.banner.w{border-color:#ffb02066;background:#ffb02012}.banner.b{border-color:#ff5a5f66;background:#ff5a5f12}
.banner.g{border-color:#34c75966;background:#34c75912}.banner.i{border-color:#3b9eff66;background:#3b9eff12}
.f{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:11px 0;border-top:1px solid var(--line);flex-wrap:wrap}
.f:first-of-type{border-top:0;padding-top:2px}.f .l{font-weight:550}.f .h{font-size:12.5px;color:var(--mut);font-weight:400}
.unv{opacity:.5}.tag{font-size:10.5px;font-weight:650;letter-spacing:.04em;text-transform:uppercase;color:var(--mut);border:1px solid var(--line);border-radius:99px;padding:1px 7px;margin-left:6px;vertical-align:1px;white-space:nowrap}
input,select{font:inherit;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:7px 10px;color-scheme:dark}
input:focus,select:focus{outline:2px solid #3b9eff88;outline-offset:1px}
input[type=number]{width:76px;text-align:right}
.rng{display:flex;align-items:center;gap:10px;flex:1;max-width:280px;min-width:180px}.rng output{min-width:58px;text-align:right;font-variant-numeric:tabular-nums;font-weight:650}
input[type=range]{flex:1;accent-color:var(--acc);padding:0;border:0;background:none}
.sw{position:relative;display:inline-block;vertical-align:middle;width:46px;height:26px;flex:none}.sw input{opacity:0;position:absolute;inset:0;margin:0;cursor:pointer;z-index:1}
.sw i{position:absolute;inset:0;border-radius:99px;background:var(--track);transition:.2s}.sw i:after{content:"";position:absolute;left:3px;top:3px;width:20px;height:20px;border-radius:50%;background:#e8ecf2;transition:.2s}
.sw input:checked+i{background:var(--good)}.sw input:checked+i:after{transform:translateX(20px)}
button{font:inherit;font-weight:650;border:0;border-radius:10px;padding:9px 16px;background:var(--acc);color:#fff;cursor:pointer;white-space:nowrap}
button.sec{background:var(--track);color:var(--ink)}button:disabled{opacity:.35;cursor:not-allowed}
.acts{display:flex;gap:8px;justify-content:flex-end;margin-top:14px;flex-wrap:wrap}
.qa{display:flex;align-items:center;gap:12px;padding:11px 0;border-top:1px solid var(--line)}.qa:first-of-type{border-top:0;padding-top:2px}
.qa .t{flex:1;min-width:0}.qa .t b{display:block;font-weight:600}.qa .t span{font-size:13px;color:var(--mut)}
.note{font-size:13px;color:var(--mut);margin-top:12px}
details>summary{cursor:pointer;list-style:none;display:flex;justify-content:space-between;align-items:center;font-weight:650}
details>summary::-webkit-details-marker{display:none}details>summary:after{content:"Show";color:var(--acc);font-size:13px}details[open]>summary:after{content:"Hide"}
.tw{overflow-x:auto;margin-top:12px}table{border-collapse:separate;border-spacing:4px;width:100%;min-width:520px;table-layout:fixed;font-size:12px;font-variant-numeric:tabular-nums}
td{text-align:center;padding:8px 0;border-radius:7px;color:var(--ink)}th{font-size:11px;color:var(--mut);font-weight:550}
canvas{width:100%;height:180px;display:block}
ul{list-style:none;margin:0;padding:0}li{padding:7px 0;border-top:1px solid var(--line);font-size:13.5px;display:flex;gap:10px}li:first-child{border:0}
li time{color:var(--mut);font-size:12.5px;min-width:44px;font-variant-numeric:tabular-nums}
.dot{width:8px;height:8px;border-radius:50%;margin-top:7px;flex:none;background:var(--mut)}
.log{max-height:260px;overflow:auto}a{color:var(--acc);text-decoration:none}
dialog{border:1px solid var(--line);border-radius:18px;background:var(--card);color:var(--ink);max-width:460px;width:calc(100% - 32px);padding:22px}
dialog::backdrop{background:#000b;backdrop-filter:blur(2px)}dialog h3{margin:2px 0 12px;font-size:18px}
.chg{display:flex;justify-content:space-between;gap:12px;padding:9px 0;border-top:1px solid var(--line);font-size:14px}.chg:first-child{border:0}
.chg s{color:var(--mut);text-decoration:none}
.xs{width:100%;border-collapse:collapse;font-size:13px;margin-top:6px}.xs td,.xs th{text-align:left;padding:5px 6px;border-top:1px solid var(--line);color:var(--ink2);font-weight:400}.xs th{color:var(--mut);font-size:12px;border:0}
.flowv{font-size:clamp(20px,2.4vw,26px)!important}
.reg{display:none;font-size:10.5px;color:var(--mut);border:1px solid var(--line);border-radius:99px;padding:1px 7px;margin-left:6px;font-weight:600;letter-spacing:.03em}
.slot .t span.reg{display:none}
body.tech .reg,body.tech .slot .t span.reg{display:inline-block}
.q{background:var(--track);color:var(--ink2);width:22px;height:22px;padding:0;border-radius:50%;font-size:12px;line-height:22px;margin-left:8px;vertical-align:1px}
.help{background:var(--bg);border-radius:12px;padding:10px 12px;margin-top:8px;font-size:13.5px;color:var(--ink2)}
.modes{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px}
.mode{background:var(--card2);border:2px solid transparent;border-radius:14px;padding:12px 14px;cursor:pointer;text-align:left;color:var(--ink);font-weight:400;white-space:normal}
.mode b{display:block;font-size:15px}.mode span{font-size:13px;color:var(--mut)}
.mode.on{border-color:var(--acc)}.mode.cur b:after{content:"  · now";color:var(--good);font-weight:600;font-size:12px}
.tl{position:relative;height:18px;border-radius:9px;background:var(--card2);margin:22px 0 22px;overflow:visible}
.tl i{position:absolute;top:0;bottom:0;border-radius:4px}.tl i.c{background:var(--good)}.tl i.d{background:var(--warn)}.tl i.off{opacity:.3}
.tl b{position:absolute;top:-6px;bottom:-6px;width:3px;margin-left:-1px;background:var(--ink);border-radius:2px}
.tl b:after{content:"now";position:absolute;top:-17px;left:-10px;font-size:11px;color:var(--ink2);font-weight:600}
.tlh{position:absolute;top:22px;font-size:11px;color:var(--mut);transform:translateX(-50%)}
.slot{display:flex;gap:12px;align-items:flex-start;background:var(--card2);border-radius:14px;padding:12px 14px;margin-top:10px}
.slot .dot{margin-top:7px}.slot .t{flex:1;min-width:0}.slot .t b{font-weight:600}.slot .t span{display:block;font-size:13px;color:var(--mut)}
.ed{background:var(--bg);border-radius:12px;padding:12px 14px;margin-top:10px}
.sub{font-size:15px;font-weight:650;margin:22px 0 6px;display:flex;align-items:center}
.seg{display:inline-flex;background:var(--card2);border-radius:10px;padding:3px;gap:2px}.seg button{background:none;color:var(--ink2);padding:6px 12px;border-radius:8px}.seg button.on{background:var(--track);color:var(--ink)}
.hnav{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:10px}.hnav b{flex:1;min-width:140px;font-size:15px}
.tc{width:100%;border-collapse:collapse;font-size:13.5px}.tc td{padding:8px 6px;border-top:1px solid var(--line);vertical-align:top;text-align:left;color:var(--ink)}.tc td:first-child{font-weight:600}
.tc .bt{display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end}.tc button{padding:5px 10px;font-size:13px}
.res{font-size:13px;margin-left:8px}.ut{font-size:11px;color:var(--mut);font-weight:500;margin-left:6px}
.st0{color:var(--mut)}.st1{color:var(--good)}.st2{color:var(--bad)}.tced{background:var(--bg);border-radius:10px;padding:8px 10px;margin-top:6px;display:flex;gap:8px;flex-wrap:wrap;align-items:center}
progress{width:100%;height:10px;accent-color:var(--acc)}
.tot{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;margin-top:12px}.tot div{background:var(--card2);border-radius:12px;padding:10px 12px}.tot span{display:block;font-size:12px;color:var(--mut)}.tot b{font-size:18px}
#hc{height:220px}
</style></head><body>
<header><div class="hd"><div class="logo">G</div><h1>GivControl</h1><span class="sp"></span><span class="chip" id="lock">–</span><span class="upd" id="upd">loading…</span></div></header>
<main>
<div id="banners" style="display:grid;gap:8px"></div>
<section class="flow">
 <div class="card ring"><svg viewBox="0 0 120 120"><circle cx="60" cy="60" r="50" fill="none" stroke="var(--track)" stroke-width="10" stroke-dasharray="261.8 314.2" transform="rotate(120 60 60)" stroke-linecap="round"/>
  <circle id="arc" cx="60" cy="60" r="50" fill="none" stroke="var(--acc)" stroke-width="10" stroke-dasharray="0 314.2" transform="rotate(120 60 60)" stroke-linecap="round"/>
  <text id="soc" x="60" y="64" text-anchor="middle" font-size="28" font-weight="700" fill="var(--ink)">–</text><text x="60" y="82" text-anchor="middle" font-size="8.5" letter-spacing=".6" fill="var(--mut)">BATTERY</text></svg>
  <div class="st" id="bst">–</div><div class="sw2" id="bw"></div></div>
 <div class="stats">
  <div class="stat" id="solart" hidden><div class="k">Solar<span class="reg">IR18/20</span></div><div class="v flowv" id="solar">–</div><div class="s" id="solard">–</div></div>
  <div class="stat"><div class="k">Grid<span class="reg">IR30</span></div><div class="v flowv" id="grid">–</div><div class="s" id="gridd">–</div></div>
  <div class="stat"><div class="k">Home<span class="reg">IR42</span></div><div class="v flowv" id="home">–</div><div class="s">load</div></div>
  <div class="stat"><div class="k">Inverter</div><div class="v" id="ist">–</div><div class="s" id="isd">–</div></div>
  <div class="stat"><div class="k">Mode<span class="reg">HR27/HR59</span></div><div class="v" id="mode">–</div><div class="s" id="moded">–</div></div>
  <div class="stat"><div class="k">Today<span class="reg">IR36/37</span></div><div class="v" id="today">–</div><div class="s">charged · discharged</div></div>
 </div>
</section>
<section class="card"><div class="k">Schedule</div>
 <div class="sub" style="margin-top:0">Battery mode<button class="q" data-h="hmode">?</button><span class="res" id="mres"></span></div>
 <div class="help" id="hmode" hidden><b>Eco</b>: the battery covers your home's use and charges from spare power; no forced export. <b>Battery idle</b>: eco off, so the battery only charges in charge slots and otherwise holds its level. <b>Timed export</b>: the battery also discharges/exports in your discharge slots.</div>
 <div class="modes">
  <button class="mode" data-m="eco"><b>Eco</b><span>Battery covers the house. No forced export.</span></button>
  <button class="mode" data-m="idle"><b>Battery idle</b><span>Charges only in charge slots, otherwise holds.</span></button>
  <button class="mode" data-m="exp"><b>Timed export<span class="ut">untested</span></b><span>Discharges / exports in discharge slots.</span></button>
 </div>
 <div class="sub">Time slots<button class="q" data-h="hslot">?</button></div>
 <div class="help" id="hslot" hidden><b>Charge slot</b>: when the battery charges from the grid, e.g. on a cheap overnight tariff, up to its target. <b>Discharge slot</b>: forced discharge or export at set times, down to a level; used in Timed export mode.</div>
 <div class="tl" id="tl"></div>
 <div id="slots"></div>
 <div class="ed" id="eds" hidden>
  <div class="f"><div class="l" id="edt">Slot</div><div><input type="time" id="eda"> – <input type="time" id="edb"> <span id="edtw"><input type="number" id="edc" min="4" max="100" style="width:70px"> %</span></div></div>
  <div class="f" id="edacw"><div class="l">Charge from the grid<div class="h">off keeps the times but stops grid charging</div></div><label class="sw"><input type="checkbox" id="edac"><i></i></label></div>
  <div class="acts"><span class="res" id="edres"></span><button class="sec" onclick="$('#eds').hidden=true">Close</button><button class="sec ctl" onclick="slotSave(1)">Remove slot</button><button class="ctl" onclick="slotSave(0)">Save</button></div>
 </div>
 <div class="acts" style="justify-content:flex-start"><button class="sec ctl" id="addc">+ Add charge slot</button><button class="sec ctl" id="addd">+ Add discharge slot</button></div>
</section>
<section class="grid3">
 <div class="card"><div class="k">Battery</div>
  <div class="f"><div class="l">Reserve<button class="q" data-h="hres">?</button><div class="h">never discharges below this</div></div><div><input type="number" id="r114" min="4" max="100" class="ctl"> %</div></div>
  <div class="help" id="hres" hidden>Keeps some charge back, e.g. for a power cut or the evening peak. 4% is the lowest the inverter allows.</div>
  <div class="f"><div class="l">Charge rate<button class="q" data-h="hrate">?</button></div><div class="rng"><input type="range" id="r111" min="2" max="50" class="ctl"><output id="r111v"></output></div></div>
  <div class="help" id="hrate" hidden>Slower charging and discharging are gentler on the battery. Full speed is about 5.7 kW on an All-in-One.</div>
  <div class="f"><div class="l">Discharge rate</div><div class="rng"><input type="range" id="r112" min="2" max="50" class="ctl"><output id="r112v"></output></div></div>
  <div class="f"><div class="l">Charge target<span class="ut">untested</span><div class="h">charging stops at this level</div></div><div><input type="number" id="r116" min="4" max="100" class="ctl"> %</div></div>
  <div class="acts"><span class="res" id="bres"></span><button onclick="save(['r114','r111','r112','r116'],'#bres')" class="ctl">Save</button></div>
 </div>
 <div class="card"><div class="k">More settings</div>
  <div class="f"><div class="l">Export limit<span class="ut">untested</span><div class="h">most power sent to the grid</div></div><div><input type="number" id="r26" min="0" max="6000" step="100" class="ctl" style="width:90px"> W</div></div>
  <div class="f"><div class="l">Pause battery<span class="ut">untested</span></div><select id="r318" class="ctl"><option value="0">Off</option><option value="1">Pause charging</option><option value="2">Pause discharging</option><option value="3">Pause both</option></select></div>
  <div class="f"><div class="l">Pause between<span class="ut">untested</span></div><div><input type="time" id="r319" class="ctl"> – <input type="time" id="r320" class="ctl"></div></div>
  <div class="f"><div class="l">Use charge target<span class="ut">untested</span></div><label class="sw"><input type="checkbox" id="r20" class="ctl"><i></i></label></div>
  <div class="f"><div class="l">Charge target (newer register)<span class="ut">untested</span><div class="h">some firmware uses this one</div></div><div><input type="number" id="r242" min="4" max="100" class="ctl"> %</div></div>
  <div class="acts"><span class="res" id="mres2"></span><button onclick="save(['r26','r318','r319','r320','r20','r242'],'#mres2')" class="ctl">Save</button></div>
 </div>
 <div class="card"><div class="k">Quick actions<span class="res" id="qres" style="text-transform:none;letter-spacing:0"></span></div>
  <div class="qa"><div class="t"><b id="holdt">Pause battery</b><span id="holdd"></span></div><button class="ctl" id="bhold">Pause</button></div>
  <div class="qa"><div class="t"><b id="cnowt">Charge now</b><span id="cnowd">from the grid until</span></div><input type="time" id="until"><button class="ctl" id="bcnow">Start</button></div>
  <div class="qa"><div class="t"><b>Sync inverter clock</b><span id="drift"></span></div><button class="ctl" onclick="act({type:'clock'},'#qres')">Sync</button></div>
  <div class="qa" id="serrow" hidden><div class="t"><b>Restore serial</b><span id="serd"></span></div><button class="ctl" onclick="act({type:'serial'},'#qres')">Restore</button></div>
  <div class="qa"><div class="t"><b>Read the inverter now</b><span>otherwise every minute</span></div><button class="sec" id="bref">Refresh</button></div>
 </div>
</section>
<section class="card"><div class="k">History<span class="mut" style="text-transform:none;letter-spacing:0;font-weight:500" id="hkeep"></span></div>
 <div class="hnav"><div class="seg"><button data-v="day" class="on">Day</button><button data-v="week">Week</button><button data-v="month">Month</button></div>
  <button class="sec" id="hprev">‹</button><b id="hlabel">–</b><button class="sec" id="hnext">›</button></div>
 <canvas id="hc"></canvas>
 <div class="note"><span class="warn">■</span> grid import &nbsp;<span class="good">■</span> grid export &nbsp;<span style="color:var(--acc)">■</span> battery charge &nbsp;<span style="color:#c58cff">■</span> battery discharge &nbsp;<span id="hsocl">— battery %</span></div>
 <div class="tot" id="htot"></div>
 <div class="tw" id="hdays"></div>
 <div class="note">Half-hour periods match electricity tariff periods (:00 and :30). A period with gaps in the readings is marked incomplete. <a id="hcsv" href="#">Download this range (CSV)</a> · <a href="/halfhours.csv">Download everything</a></div>
</section>
<section class="card"><div class="k">Last 24 hours</div><canvas id="ch"></canvas><div class="note"><span style="color:var(--acc)">■</span> battery % &nbsp; <span class="good">■</span> charging / <span class="warn">■</span> discharging (kW) &nbsp; <span class="mut">■</span> home load (kW)</div></section>
<section class="grid3">
 <div class="card"><div class="k">Change log</div><ul id="log" class="log"></ul><div class="note"><a href="/download?f=changes.csv">Download CSV</a> <span id="files"></span></div></div>
 <div class="card"><div class="k">Events</div><ul id="ev" class="log"></ul></div>
</section>
<section class="card"><div class="k">Settings</div>
 <div class="f"><div class="l">GivEnergy dongle<div class="h" id="dongd">the WiFi dongle on the inverter (TCP port 8899)</div></div><div><input id="host" size="13" placeholder="not set"> <button class="sec" onclick="post('/api/settings',{host:$('#host').value}).then(load).catch(e=>alert(e.message))">Save</button> <button class="sec" id="bfind" onclick="post('/api/discover',{}).then(load).catch(e=>alert(e.message))">Find automatically</button></div></div>
 <div id="found"></div>
 <div class="f"><div class="l">Time zone<div class="h">POSIX format; UK is GMT0BST,M3.5.0/1,M10.5.0</div></div><div><input id="tz" size="24"> <button class="sec" onclick="post('/api/settings',{tz:$('#tz').value}).then(load)">Save</button></div></div>
 <div class="f scr"><div class="l">Screen colours<div class="h">invert the screen's colours if the device shows a light background</div></div><span><button class="sec" onclick="post('/api/testcard',{})">Colour test</button> <label class="sw"><input type="checkbox" id="inv" onchange="post('/api/settings',{invert:this.checked?1:0})"><i></i></label></span></div>
 <div class="f scr"><div class="l">Touchscreen<div class="h">shows four crosses to tap on the device</div></div><button class="sec" onclick="post('/api/touchcal',{}).then(()=>alert('Tap the four crosses on the screen.'))">Calibrate</button></div>
 <div class="f"><div class="l">Solar<div class="h">Auto shows it once the inverter reports solar generation</div></div><select id="solm" onchange="post('/api/settings',{solar:this.value}).then(load)"><option value="0">Auto</option><option value="1">Always show</option><option value="2">Hide</option></select></div>
 <div class="sub">Firmware</div>
 <div class="f"><div class="l" id="fwv">–<div class="h" id="fwd"></div></div></div>
 <div id="fwbox"></div>
 <div class="f"><div class="l">Show technical details<div class="h">register numbers and the raw values of settings that aren't tested yet</div></div><label class="sw"><input type="checkbox" id="tech"><i></i></label></div>
 <div id="techbox" hidden><table class="xs" id="regs"></table></div>
 <div id="lockbox"></div>
</section>
</main>
<script>
const $=s=>document.querySelector(s);let S=null,dirty=false,PID=0;
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const hmT=t=>new Date(t*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
const hm=v=>v<0?'':String(v).padStart(4,'0').replace(/(..)(..)/,'$1:$2');
const kw=l=>l<0?'–':S&&S.inv&&!S.inv.aio?l+'/50':(Math.min(l*166,5700)/1000).toFixed(1)+' kW';
const kW=w=>(Math.abs(w)/1000).toFixed(2)+' kW';
async function j(u){const r=await fetch(u);return r.json()}
async function post(u,o){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)});const x=await r.json().catch(()=>({}));if(!r.ok)throw new Error(x.error||r.status);return x}
document.addEventListener('input',e=>{if(/^r\d/.test(e.target.id))dirty=true;rv()});
function rv(){$('#r111v').textContent=kw(+$('#r111').value);$('#r112v').textContent=kw(+$('#r112').value)}
function fill(s,force){if(!s||!s.ok||(dirty&&!force))return;dirty=false;const i=s.inv;
 $('#r111').value=i.limit;$('#r112').value=i.dlimit;$('#r114').value=i.reserve;
 [116,26,318,242].forEach(r=>{if(V[r]>=0)$('#r'+r).value=V[r]});[319,320].forEach(r=>$('#r'+r).value=hm(V[r])||'00:00');$('#r20').checked=V[20]==1;
 const m=V[59]==1?(V[27]==1?'':'exp'):(V[27]==1?'eco':'idle');document.querySelectorAll('.mode').forEach(b=>b.classList.toggle('on',b.dataset.m==m));rv()}
// one Save: send the fields, then show what the inverter read back (✓ / ✗) next to the button
function fieldVal(e){return e.type=='checkbox'?(e.checked?1:0):e.value}
async function run(o,res){const r=$(res);r.className='res mut';r.textContent='saving…';
 try{const x=await post('/api/plan',Object.assign({go:1},o));}catch(e){r.className='res '+(/Nothing to change/.test(e.message)?'mut':'bad');r.textContent=/Nothing to change/.test(e.message)?'no change':'✗ '+e.message;return}
 for(let k=0;k<90;k++){await new Promise(z=>setTimeout(z,1500));const s=await j('/api/status');if(!s.job){const ok=/done/.test(s.jobResult);r.className='res '+(ok?'good':'bad');r.textContent=ok?'✓ saved':'✗ '+s.jobResult.replace(/^.*?: /,'');dirty=false;load();return}}}
function save(ids,res){const o={type:'set'};for(const id of ids){const e=$('#'+id);if(!e.checkValidity()){e.reportValidity();return}o[id]=fieldVal(e)}run(o,res)}
function act(o,res){run(o,res)}
document.querySelectorAll('.mode').forEach(b=>b.onclick=()=>{if(!S||!S.controls)return;const m=b.dataset.m;
 run({type:'set',r27:m=='eco'?1:0,r59:m=='exp'?1:0},'#mres')});
document.querySelectorAll('.q').forEach(b=>b.onclick=()=>{const h=$('#'+b.dataset.h);h.hidden=!h.hidden});
try{if(localStorage.getItem('tech')=='1'){document.body.classList.add('tech');$('#tech').checked=true;$('#techbox').hidden=false}}catch(e){}
$('#tech').onchange=e=>{document.body.classList.toggle('tech',e.target.checked);$('#techbox').hidden=!e.target.checked;try{localStorage.setItem('tech',e.target.checked?'1':'0')}catch(x){}};
// every slot the inverter has: [charge?, start reg, end reg, target reg or 0, verified?]
const mins=v=>Math.floor(v/100)*60+v%100,empty=(a,b)=>a<0||b<0||Math.abs(mins(a)-mins(b))<=1;
const SLOTS=[[1,94,95,0,1],[1,31,32,0,0]].concat([...Array(10)].map((_,k)=>[1,240+3*k,241+3*k,242+3*k,0]),[[0,56,57,0,1],[0,44,45,0,0]],[...Array(10)].map((_,k)=>[0,270+3*k,271+3*k,272+3*k,0]));
const V={};
function slotList(s){const L=[],seen=new Set(),timed=V[59]==1;
 SLOTS.forEach(([c,ra,rb,rt,ver])=>{const a=V[ra],b=V[rb],k=c+':'+a+'-'+b;if(empty(a,b)||seen.has(k))return;seen.add(k);
  L.push({charge:c,a,b,ra,rb,rt,ver,on:c?s.inv.ac==1:timed,lvl:rt?V[rt]:(c&&ra==94?V[116]:-1),regs:'HR'+ra+'/'+rb+(rt?'/'+rt:'')})});return L}
let ED=null;
function slotOpen(sl){ED=sl;$('#edt').innerHTML=(sl[0]?'Charge from the grid':'Discharge / export')+(sl[4]?'':'<span class="ut">untested</span>');
 $('#eda').value=hm(V[sl[1]])||'00:00';$('#edb').value=hm(V[sl[2]])||'00:00';$('#edtw').hidden=!sl[3];if(sl[3])$('#edc').value=V[sl[3]]>0?V[sl[3]]:(sl[0]?100:4);
 $('#edacw').hidden=sl[1]!=94;$('#edac').checked=S.inv.ac==1;$('#edres').textContent='';$('#eds').hidden=false;$('#eds').scrollIntoView({block:'nearest'})}
function slotNew(c){const sl=SLOTS.find(x=>x[0]==c&&empty(V[x[1]],V[x[2]]));if(!sl)return;slotOpen(sl);
 if(c){$('#eda').value='23:30';$('#edb').value='05:30'}else{$('#eda').value='16:00';$('#edb').value='19:00'}}
function slotSave(remove){const sl=ED;if(!sl)return;const o={type:'set'};
 o['r'+sl[1]]=remove?'00:00':$('#eda').value;o['r'+sl[2]]=remove?'00:00':$('#edb').value;
 if(sl[3]&&!remove){if(!$('#edc').checkValidity())return $('#edc').reportValidity();o['r'+sl[3]]=$('#edc').value}
 if(sl[1]==94&&!remove)o.r96=$('#edac').checked?1:0;run(o,'#edres')}
$('#addc').onclick=()=>slotNew(1);$('#addd').onclick=()=>slotNew(0);
function drawSlots(s){const L=slotList(s),tl=$('#tl'),pct=m=>m/1440*100;let h='';
 L.forEach(o=>{const a=mins(o.a),b=mins(o.b),c=(o.charge?'c':'d')+(o.on?'':' off');const seg=(x,y)=>h+=`<i class="${c}" style="left:${pct(x)}%;width:${pct(y-x)}%"></i>`;if(a<b)seg(a,b);else{seg(a,1440);seg(0,b)}});
 const n=new Date();h+=`<b style="left:${pct(n.getHours()*60+n.getMinutes())}%"></b>`;[0,6,12,18,24].forEach(x=>h+=`<span class="tlh" style="left:${x/24*100}%">${String(x).padStart(2,'0')}</span>`);tl.innerHTML=h;
 $('#slots').innerHTML=L.length?L.map((o,k)=>{const t=hm(o.a)+' → '+hm(o.b);
  const what=o.charge?`Charge from the grid <b>${t}</b>`+(o.lvl>0?`, up to ${o.lvl}%`:''):`Discharge / export <b>${t}</b>`+(o.lvl>0?`, down to ${o.lvl}%`:'');
  const why=!o.on?(o.charge?'AC charge is off, so this slot is not used':'Timed export is off, so this slot is not used'):'';
  return`<div class="slot"><span class="dot" style="background:${!o.on?'var(--mut)':o.charge?'var(--good)':'var(--warn)'}"></span><div class="t">${what}${o.ver?'':'<span class="ut">untested</span>'}<span class="reg">${o.regs}</span><span>${why}</span></div><button class="sec" ${s.controls&&!s.job?'':'disabled'} onclick="slotOpen(SLOTS.find(x=>x[1]==${o.ra}))">Edit</button></div>`}).join('')
  :'<div class="slot"><div class="t"><b>No time slots set</b><span>Without a charge slot the battery only charges from spare power.</span></div></div>';}
const C={};function cv(r){return C[r]==null?-1:C[r]}
$('#bref').onclick=()=>post('/api/refresh',{}).then(()=>setTimeout(load,8000));
async function load(){
 const s=await j('/api/status');S=s;const i=s.inv;
 const lk=$('#lock');lk.textContent=s.job?'Writing…':s.controls?'Unlocked':'Locked';lk.className='chip '+(s.job?'acc':s.controls?'warn':'bad');
 document.querySelectorAll('.ctl').forEach(x=>x.disabled=!s.controls||!!s.job||!s.ok);
 let bn='';
 if(s.job)bn+=`<div class="banner i">Writing to the inverter: <b>${esc(s.job.title)}</b> — step ${s.job.step+1} of ${s.job.of}</div>`;
 else if(s.jobResult)bn+=`<div class="banner ${/FAILED/.test(s.jobResult)?'b':'g'}">${esc(s.jobResult)}</div>`;
 if(!s.controls)bn+='<div class="banner b">Controls are <b>locked</b> (read only). Unlock them in Settings below.</div>';
 if(s.controls&&s.undo&&!s.job)bn+='<div class="note" style="margin:0"><a href="#" onclick="run({type:\'undo\'},\'#ures\');return false">Undo last change</a><span class="res" id="ures"></span></div>';
 if(!s.host)bn+=`<div class="banner i">No GivEnergy dongle set yet. ${s.disc.running?'Looking for it on the network… '+Math.round(s.disc.progress/2.54)+'%':'Find it or enter its address in Settings below.'}</div>`;
 else if(s.disc.running)bn+=`<div class="banner i">Looking for GivEnergy dongles on the network… ${Math.round(s.disc.progress/2.54)}%</div>`;
 if(s.ok&&i.valid&&!i.verified)bn+=`<div class="banner w">This is a <b>${esc(i.model)}</b>. GivControl's controls have only been verified on the All-in-One, so everything is <b>read only</b> (not verified for this model).</div>`;
 if(s.held)bn+='<div class="banner w">Battery is <b>paused</b> (reserve raised, AC charge off). Use Resume to go back.</div>';
 if(s.chargeNow)bn+='<div class="banner w"><b>Charge now</b> has replaced charge slot 1. Use Restore afterwards.</div>';
 if(s.fw&&s.fw.pending)bn+=`<div class="banner i">New firmware <b>${esc(s.fw.version)}</b> is running and will be kept once Wi-Fi and the inverter answer (within 2 minutes); otherwise the previous firmware comes back automatically.</div>`;
 if(s.ok&&i.fault)bn+=`<div class="banner b">Inverter fault code <b>0x${i.fault.toString(16)}</b>.</div>`;
 if(s.ok&&i.restore)bn+=`<div class="banner b">Inverter serial reads <b>${esc(i.serial)}</b>. It was <b>${esc(i.restore)}</b>: use Restore serial in Quick actions.</div>`;
 $('#banners').innerHTML=bn;
 if(s.job||s.disc.running)setTimeout(load,4000);
 $('#inv').checked=s.invert;document.querySelectorAll('.scr').forEach(e=>e.hidden=!s.display);
 if(!s.ok){$('#upd').textContent=s.host?'waiting for inverter':'no dongle set';$('#host').value=s.host;$('#tz').value=s.tz;
  $('#found').innerHTML=s.disc.running?`<div class="note">Scanning… ${s.disc.progress} / 254</div>`:s.disc.found.map(f=>`<div class="f"><div class="l">${esc(f[0])}<div class="h">${esc(f[2])} · inverter ${esc(f[1])}</div></div><button class="sec" onclick="post('/api/settings',{host:'${esc(f[0])}'}).then(load)">Use this one</button></div>`).join('');
  setTimeout(load,5000);return}
 $('#upd').textContent='Updated '+hmT(s.ts)+(s.fails?' · last read failed':'');
 s.cand.forEach(c=>C[c[0]]=c[1]);s.cand.forEach(c=>V[c[0]]=c[1]);s.ext.forEach((v,k)=>V[240+k]=v);
 Object.assign(V,{94:i.slot[0],95:i.slot[1],56:i.dslot[0],57:i.dslot[1],27:i.eco,96:i.ac});
 const w=i.w,st=s.tripped?'bad':w<-50?'good':w>50?'warn':'acc';
 $('#soc').textContent=i.soc+'%';
 $('#arc').setAttribute('stroke-dasharray',`${261.8*Math.max(0,Math.min(100,i.soc))/100} 314.2`);$('#arc').setAttribute('stroke',`var(--${st})`);
 $('#bst').className='st '+st;$('#bst').textContent=s.tripped?'Stopped':w<-50?'Charging':w>50?'Discharging':'Idle';
 if(!i.single){['#grid','#home','#today','#mode'].forEach(id=>$(id).textContent='—');$('#soc').textContent='—';$('#bst').textContent='';$('#bw').textContent='readings not supported for this model';}
 else{$('#bw').textContent=(Math.abs(w)>50?kW(w):w+' W')+(i.bv!=null?' · '+i.bv.toFixed(0)+' V':'');
 
 const g=i.grid;$('#grid').innerHTML=`<span class="${g<-30?'warn':g>30?'good':''}">${kW(g)}</span>`;$('#gridd').textContent=g<-30?'importing':g>30?'exporting':'balanced';
 $('#home').textContent=kW(i.load);
 $('#solart').hidden=!i.pv.show;$('#solar').innerHTML=`<span class="${i.pv.w>20?'good':''}">${kW(i.pv.w)}</span>`;
 $('#solard').textContent=(i.pv.today!=null?i.pv.today+' kWh today':'')+(i.pv.w1&&i.pv.w2?` · ${kW(i.pv.w1)} + ${kW(i.pv.w2)}`:'');}
 $('#ist').innerHTML=i.fault?'<span class="bad">Fault</span>':i.status==1?'<span class="good">Normal</span>':i.status==0?'<span class="bad">Stopped</span>':'?';
 $('#isd').textContent=`${i.model} · ${i.serial} · firmware ${i.arm}`;
 $('#mode').textContent=i.mode;$('#moded').textContent=`reserve ${i.reserve}%`;
 $('#today').textContent=i.chgDay==null?'–':`${i.chgDay} · ${i.disDay} kWh`;
 fill(s);
 drawSlots(s);
 // technical details: raw values of the settings that aren't tested yet
 $('#regs').innerHTML='<tr><th>Register</th><th>Setting</th><th>Value</th></tr>'+s.cand.map(c=>`<tr><td>HR${c[0]}</td><td>${esc(c[3])}</td><td>${c[2]==1?hm(c[1])||'00:00':c[1]}</td></tr>`).join('')
  +`<tr><td>HR240-299</td><td>Extra charge / discharge slots</td><td>${slotList(s).filter(o=>o.ra>=240).length} set</td></tr>`;
 $('#holdt').textContent=s.held?'Resume battery':'Pause battery';$('#bhold').textContent=s.held?'Resume':'Pause';
 $('#holdd').textContent=s.held?'put the saved reserve and AC charge back':`hold at ${i.soc}%: reserve to ${i.soc}%, AC charge off`;
 $('#bhold').onclick=()=>act({type:s.held?'resume':'hold'},'#qres');
 $('#cnowt').textContent=s.chargeNow?'Restore charge slot':'Charge now';$('#cnowd').textContent=s.chargeNow?'put the saved slot 1 back':'from the grid until';
 $('#until').hidden=s.chargeNow;$('#bcnow').textContent=s.chargeNow?'Restore':'Start';$('#bcnow').disabled||=s.held;
 $('#bcnow').onclick=()=>act(s.chargeNow?{type:'restore'}:{type:'chargenow',until:$('#until').value},'#qres');
 if(!$('#until').value){const d=new Date(Date.now()+2*3600e3);$('#until').value=String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')}
 $('#drift').textContent=i.drift==null?'from internet time':'inverter clock is '+(Math.abs(i.drift)<15?'right':Math.abs(i.drift)+' s '+(i.drift>0?'ahead':'behind'));
 $('#serrow').hidden=!i.restore;$('#serd').textContent='write back '+i.restore;
 if(document.activeElement!=$('#host'))$('#host').value=s.host;
 if(document.activeElement!=$('#tz'))$('#tz').value=s.tz;
 if(document.activeElement!=$('#solm'))$('#solm').value=i.pv.mode;
 $('#bfind').disabled=s.disc.running;$('#dongd').textContent=s.ok&&i.dongle?`dongle ${i.dongle} · ${i.model}`:'the WiFi dongle on the inverter (TCP port 8899)';
 $('#found').innerHTML=s.disc.running?`<div class="note">Scanning ${s.me.replace(/\.\d+$/,'.x')}… ${s.disc.progress} / 254</div>`
  :s.disc.found.map(f=>`<div class="f"><div class="l">${esc(f[0])}<div class="h">${esc(f[2])} · inverter ${esc(f[1])} · dongle ${esc(f[3])}</div></div>${f[0]==s.host?'<span class="good">in use</span>':`<button class="sec" onclick="post('/api/settings',{host:'${esc(f[0])}'}).then(load)">Use this one</button>`}</div>`).join('');
 fwUI(s);
 const lb=$('#lockbox'),want=s.controls?'on':'off';
 if(lb.dataset.s!=want){lb.dataset.s=want;lb.innerHTML=s.controls?'<div class="f"><div class="l">Controls<div class="h">unlocked: settings can be changed</div></div><button class="sec" onclick="post(\'/api/settings\',{controls:0}).then(load)">Lock</button></div>'
  :'<div class="f"><div class="l">Controls<div class="h">locked: tick to unlock</div></div><label style="display:flex;gap:8px;align-items:center;font-size:14px"><input type="checkbox" onchange="post(\'/api/settings\',{controls:1,manual:1}).then(load).catch(e=>alert(e.message))"> Nothing else is changing the battery settings</label></div>'}
 const ev=await j('/api/events');const dc=['var(--mut)','var(--good)','var(--warn)','var(--bad)'];
 $('#ev').innerHTML=ev.map(e=>`<li><span class="dot" style="background:${dc[e[1]]}"></span><time>${hmT(e[0])}</time><span>${esc(e[2])}</span></li>`).join('')||'<li class="mut">No events yet</li>';
 const lg=(await (await fetch('/api/changes')).text()).trim();
 $('#log').innerHTML=lg?lg.split('\n').reverse().map(l=>{const t=l.slice(11,16),r=l.slice(20).replace(/"/g,'');const bad=/FAILED/.test(r);
  return`<li><span class="dot" style="background:${bad?'var(--bad)':/,ok,|done/.test(r)?'var(--good)':'var(--mut)'}"></span><time>${t}</time><span>${esc(r.replace(/,/g,' · '))}</span></li>`}).join(''):'<li class="mut">No changes yet</li>';
 if(s.sd){const fl=await j('/api/files');$('#files').innerHTML='· SD: '+fl.map(f=>`<a href="/download?f=${encodeURIComponent(f[0])}">${esc(f[0])}</a>`).join(' ')}
 let H=await j('/api/history');
 const t0=Date.now()/1000-86400,first=H.length?H[0][0]:Date.now()/1000;
 if(first-t0>3600){const P=await j(`/api/halfhours?from=${t0|0}&to=${first|0}`);H=P.filter(r=>r[1]>=0).map(r=>[r[0]+1800,r[1],(r[6]-r[5])*2,(r[4]-r[3])*2,r[7]*2]).concat(H)}
 chart(H);
}
function chart(H){const c=$('#ch'),d=devicePixelRatio||1,W=c.clientWidth,Ht=180;c.width=W*d;c.height=Ht*d;const x=c.getContext('2d');x.scale(d,d);x.clearRect(0,0,W,Ht);
 const mut='#9aa4b1',acc='#3b9eff';
 if(!H.length){x.fillStyle=mut;x.font='13px system-ui';x.fillText('Collecting data (one point every 5 minutes)',10,90);return}
 const L=36,R=W-40,t1=Date.now()/1000,t0=t1-86400,X=t=>L+(t-t0)/(t1-t0)*(R-L),Ys=v=>Ht-22-v/100*(Ht-32),Yk=v=>(Ht-22+10)/2-v/6*((Ht-32)/2);
 x.font='11px system-ui';x.fillStyle=mut;x.strokeStyle=mut;x.lineWidth=1;
 [0,50,100].forEach(v=>{x.globalAlpha=.15;x.beginPath();x.moveTo(L,Ys(v));x.lineTo(R,Ys(v));x.stroke();x.globalAlpha=1;x.fillText(v+'%',4,Ys(v)+4)});
 [-6,0,6].forEach(v=>x.fillText((v>0?'+':'')+v+'kW',R+4,Yk(v)+4));
 for(let h=0;h<=24;h+=6){const t=t1-(24-h)*3600;x.fillText(hmT(t),Math.min(X(t)-14,R-30),Ht-5)}
 const bw=Math.max(1,(R-L)/288-1);H.forEach(r=>{const v=-r[2]/1000;if(Math.abs(r[2])<50)return;x.fillStyle=v>0?'#34c75999':'#ffb02099';const y0=Yk(0),y1=Yk(Math.max(-6,Math.min(6,v)));x.fillRect(X(r[0]),Math.min(y0,y1),bw,Math.abs(y1-y0))});
 const line=(f,c,sc,w)=>{x.strokeStyle=c;x.lineWidth=w;x.lineJoin='round';x.beginPath();let p=null;H.forEach(r=>{const v=f(r);if(v==null){p=null;return}const px=X(r[0]),py=sc(v);if(p&&r[0]-p>900)x.moveTo(px,py);else x[p?'lineTo':'moveTo'](px,py);p=r[0]});x.stroke()};
 line(r=>r[4]==null?null:Math.min(6,r[4]/1000),mut,Yk,1.2);line(r=>r[1]>=0?r[1]:null,acc,Ys,2.5);
}
// ---- firmware update (Settings > Firmware)
function fwUI(s){const f=s.fw;if(!f)return;$('#fwv').firstChild.textContent=`Version ${f.version} · ${f.board}`;
 $('#fwd').textContent=`built ${f.built} · running from ${f.slot}`;
 const b=$('#fwbox'),want=f.pin?'pin':'nopin';if(b.dataset.s==want)return;b.dataset.s=want;
 b.innerHTML=f.pin?`<div class="f"><div class="l">Install new firmware<div class="h">the firmware .bin for this ${esc(f.board)} board, up to ${f.slotKB} KB</div></div><div><input type="file" id="fwf" accept=".bin"> <input type="password" id="fwp" placeholder="update PIN" size="10"> <button onclick="fwUp()">Install</button></div></div>
  <progress id="fwpg" max="1" value="0" hidden></progress><div class="note" id="fwmsg">If the new firmware can't reach Wi-Fi and the inverter within 2 minutes, the previous one comes back by itself.</div>
  <div class="f"><div class="l">Change update PIN</div><div><input type="password" id="fwo" placeholder="current PIN" size="10"> <input type="password" id="fwn" placeholder="new PIN" size="10"> <button class="sec" onclick="post('/api/otapin',{old:$('#fwo').value,pin:$('#fwn').value}).then(()=>alert('PIN changed')).catch(e=>alert(e.message))">Change</button></div></div>`
  :`<div class="f"><div class="l">Set an update PIN<div class="h">needed before firmware can be installed over the network (6 or more characters; anyone on your network can reach this page)</div></div><div><input type="password" id="fwn" placeholder="new PIN" size="12"> <button onclick="post('/api/otapin',{pin:$('#fwn').value}).then(load).catch(e=>alert(e.message))">Set PIN</button></div></div>`}
function fwUp(){const f=$('#fwf').files[0];if(!f)return alert('Choose the firmware .bin first');
 const x=new XMLHttpRequest(),pg=$('#fwpg');x.open('POST','/update?size='+f.size);x.setRequestHeader('X-Update-Pin',$('#fwp').value);
 pg.hidden=false;pg.value=0;$('#fwmsg').textContent='Uploading…';x.upload.onprogress=e=>{if(e.lengthComputable)pg.value=e.loaded/e.total};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
  if(x.status!=200){pg.hidden=true;$('#fwmsg').textContent='';return alert(r.error||'Update failed')}
  $('#fwmsg').textContent='Installed and verified. Restarting… this page reloads when the board is back.';
  setTimeout(function w(){fetch('/api/status').then(()=>location.reload()).catch(()=>setTimeout(w,3000))},9000)};
 x.onerror=()=>{pg.hidden=true;alert('Upload failed (connection lost)')};
 const fd=new FormData();fd.append('firmware',f,f.name);x.send(fd)}

// ---- half-hour history
let HV='day',HA=(()=>{const d=new Date();d.setHours(0,0,0,0);return d})();
function hRange(){const a=new Date(HA);if(HV=='week'){const dw=(a.getDay()+6)%7;a.setDate(a.getDate()-dw)}if(HV=='month')a.setDate(1);
 const b=new Date(a);if(HV=='day')b.setDate(b.getDate()+1);else if(HV=='week')b.setDate(b.getDate()+7);else b.setMonth(b.getMonth()+1);return[a,b]}
function hStep(n){if(HV=='day')HA.setDate(HA.getDate()+n);else if(HV=='week')HA.setDate(HA.getDate()+7*n);else HA.setMonth(HA.getMonth()+n);hload()}
document.querySelectorAll('.seg button').forEach(b=>b.onclick=()=>{HV=b.dataset.v;document.querySelectorAll('.seg button').forEach(x=>x.classList.toggle('on',x==b));hload()});
$('#hprev').onclick=()=>hStep(-1);$('#hnext').onclick=()=>hStep(1);
const kwh=v=>(v/1000).toFixed(v<10000?2:1);
async function hload(){const[a,b]=hRange(),f=a/1000|0,t=b/1000|0;
 $('#hlabel').textContent=HV=='day'?a.toLocaleDateString([],{weekday:'short',day:'numeric',month:'short',year:'numeric'}):HV=='week'?'Week of '+a.toLocaleDateString([],{day:'numeric',month:'short'}):a.toLocaleDateString([],{month:'long',year:'numeric'});
 $('#hcsv').href=`/halfhours.csv?from=${f}&to=${t}`;$('#hnext').disabled=b>new Date();$('#hsocl').hidden=HV!='day';
 const R=await j(`/api/halfhours?from=${f}&to=${t}`+(HV=='day'?'':'&agg=day'));
 const pv=S&&S.inv&&S.inv.pv&&S.inv.pv.show;
 // rows as [start, imp, exp, chg, dis, load, pv, soc, complete-ish]
 const rows=HV=='day'?R.map(r=>[r[0],r[3],r[4],r[5],r[6],r[7],r[8],r[1],r[2]&1,r[2]&4]):R.map(r=>[r[0],r[1],r[2],r[3],r[4],r[5],r[6],-1,r[8]==48,0]);
 const sum=k=>rows.reduce((x,r)=>x+r[k],0),inc=HV=='day'?rows.filter(r=>!r[8]&&!r[9]).length:rows.filter(r=>!r[8]).length;
 $('#htot').innerHTML=[['Imported',1],['Exported',2],['Battery charged',3],['Battery discharged',4],['Home',5]].concat(pv?[['Solar',6]]:[])
  .map(([n,k])=>`<div><span>${n}</span><b>${kwh(sum(k))}</b> kWh</div>`).join('')+(rows.length&&inc?`<div><span>Incomplete</span><b>${inc}</b> ${HV=='day'?'half-hours':'days'}</div>`:'');
 $('#hdays').innerHTML=HV=='day'||!rows.length?'':'<table class="xs"><tr><th>Day</th><th>Import</th><th>Export</th><th>Charged</th><th>Discharged</th><th>Home</th>'+(pv?'<th>Solar</th>':'')+'</tr>'
  +rows.map(r=>`<tr><td>${new Date(r[0]*1000).toLocaleDateString([],{weekday:'short',day:'numeric'})}${r[8]?'':' <span class="mut">(part)</span>'}</td><td>${kwh(r[1])}</td><td>${kwh(r[2])}</td><td>${kwh(r[3])}</td><td>${kwh(r[4])}</td><td>${kwh(r[5])}</td>${pv?`<td>${kwh(r[6])}</td>`:''}</tr>`).join('')+'</table>';
 hchart(rows,f,t);
 if(S&&S.hist){const d=Math.round(S.hist.n/48),c=S.hist.capDays,cap=c>=730?'2 years':c+' days';$('#hkeep').textContent=`History: ${d<1?'under a day':d+' day'+(d==1?'':'s')} of ${cap}`}}
function hchart(rows,f,t){const c=$('#hc'),d=devicePixelRatio||1,W=c.clientWidth,Ht=220;c.width=W*d;c.height=Ht*d;const x=c.getContext('2d');x.scale(d,d);x.clearRect(0,0,W,Ht);
 const mut='#9aa4b1';x.font='11px system-ui';x.fillStyle=mut;
 if(!rows.length){x.fillText('No half-hours stored for this '+HV+' yet (one is saved every 30 minutes)',10,Ht/2);return}
 const L=40,Rr=W-34,slot=HV=='day'?1800:86400,n=Math.round((t-f)/slot),bw=(Rr-L)/n,day=HV=='day';
 const mx=Math.max(day?0.5:5,...rows.map(r=>Math.max(r[1],r[2],r[3],r[4])/1000)),mid=(Ht-20)/2+4,sc=v=>v/mx*((Ht-28)/2);
 x.strokeStyle=mut;x.globalAlpha=.25;x.beginPath();x.moveTo(L,mid);x.lineTo(Rr,mid);x.stroke();x.globalAlpha=1;
 x.fillText(mx.toFixed(1)+' kWh',2,12);x.fillText('in',2,mid-4);x.fillText('out',2,mid+12);
 rows.forEach(r=>{const i=Math.floor((r[0]-f)/slot);if(i<0||i>=n)return;const x0=L+i*bw,w=Math.max(1,bw/2-1);x.globalAlpha=r[8]||r[9]?1:.45;
  x.fillStyle='#ffb020';x.fillRect(x0,mid-sc(r[1]/1000),w,sc(r[1]/1000));x.fillStyle='#34c759';x.fillRect(x0,mid,w,sc(r[2]/1000));
  x.fillStyle='#3b9eff';x.fillRect(x0+w+1,mid-sc(r[3]/1000),w,sc(r[3]/1000));x.fillStyle='#c58cff';x.fillRect(x0+w+1,mid,w,sc(r[4]/1000))});
 x.globalAlpha=1;x.fillStyle=mut;
 const lab=day?[0,6,12,18,24].map(h=>[h*2,String(h).padStart(2,'0')+':00']):rows.map(r=>[Math.floor((r[0]-f)/slot),new Date(r[0]*1000).getDate()]).filter((v,k)=>HV=='week'||k%5==0);
 lab.forEach(([i,s])=>x.fillText(s,Math.min(L+i*bw,Rr-28),Ht-3));
 if(day){x.strokeStyle='#e8ecf1';x.lineWidth=1.5;x.beginPath();let p=0;rows.forEach(r=>{if(r[7]<0){p=0;return}const px=L+(Math.floor((r[0]-f)/slot)+1)*bw,py=Ht-18-r[7]/100*(Ht-28);x[p?'lineTo':'moveTo'](px,py);p=1});x.stroke();
  x.fillStyle=mut;['100%','0%'].forEach((v,k)=>x.fillText(v,Rr+4,k?Ht-18:14))}
}
load().then(hload);setInterval(load,30000);setInterval(hload,300000);
</script></body></html>)HTML";
