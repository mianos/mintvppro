#pragma once

// Settings page served at GET /. Reads and writes /config; values are set via
// input.value, never innerHTML, so stored strings can't inject markup.
inline constexpr char kSettingsPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mini TV Pro clock</title>
<style>
body{font-family:system-ui,sans-serif;max-width:32rem;margin:1.5rem auto;padding:0 1rem;color:#222;background:#fafafa}
h1{font-size:1.3rem;margin-bottom:.2rem}
#info{color:#777;font-size:.85rem;margin-bottom:1.2rem}
fieldset{border:1px solid #ddd;border-radius:6px;margin:0 0 1rem;padding:.6rem 1rem 1rem;background:#fff}
legend{font-weight:600;padding:0 .3rem}
label{display:block;margin-top:.7rem;font-size:.9rem}
input,select{width:100%;box-sizing:border-box;padding:.45rem;margin-top:.2rem;font-size:1rem;border:1px solid #ccc;border-radius:4px}
small{color:#777}
button{padding:.6rem 1.4rem;font-size:1rem;border:0;border-radius:4px;background:#2563eb;color:#fff;cursor:pointer}
#status{margin-left:.8rem}
</style></head><body>
<h1>Mini TV Pro clock</h1>
<div id="info">&nbsp;</div>
<form id="f">
<fieldset><legend>Device</legend>
<label>Hostname<input name="hostname" required pattern="[A-Za-z0-9]([A-Za-z0-9-]{0,30}[A-Za-z0-9])?">
<small>Changing it restarts the clock; it then answers as &lt;name&gt;.mianos.com</small></label>
</fieldset>
<fieldset><legend>Time</legend>
<label>Timezone (POSIX TZ)<input name="tz" required>
<small>Sydney: AEST-10AEDT,M10.1.0,M4.1.0/3</small></label>
<label>Clock face<select name="face"><option value="1">Nixie tubes</option><option value="2">VFD (VCR style)</option></select></label>
</fieldset>
<fieldset><legend>Location (for sunset)</legend>
<label>Latitude<input name="latitude" inputmode="decimal" required><small>South is negative</small></label>
<label>Longitude<input name="longitude" inputmode="decimal" required><small>East is positive</small></label>
</fieldset>
<fieldset><legend>MQTT (saving a change here restarts the clock)</legend>
<label>Server<input name="mqtt_server" required></label>
<label>Port<input name="mqtt_port" type="number" min="1" max="65535" required></label>
<label>Inside temperature topic<input name="inside_topic"></label>
<label>Outside temperature topic<input name="outside_topic"></label>
<small>Topics carry {"temperature":22.6,"timestamp":"..."}; leave empty to hide.</small>
</fieldset>
<button>Save</button><span id="status"></span>
</form>
<p><button type="button" id="rb" style="background:#6b7280">Restart clock</button></p>
<script>
const f=document.getElementById('f'),st=document.getElementById('status');
const ints=['face','mqtt_port'];
function fill(c){for(const[k,v]of Object.entries(c)){if(f.elements[k])f.elements[k].value=v;}}
fetch('/config').then(r=>r.json()).then(fill).catch(()=>st.textContent='Could not load settings');
fetch('/healthz').then(r=>r.json()).then(h=>{document.getElementById('info').textContent=
 'Firmware '+h.version+' on '+h.partition+', up '+Math.round(h.uptime_s/60)+' min';}).catch(()=>{});
f.addEventListener('submit',async e=>{
 e.preventDefault();st.textContent='Saving...';
 const body={};for(const el of f.elements){if(!el.name)continue;body[el.name]=ints.includes(el.name)?Number(el.value):el.value.trim();}
 try{const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const j=await r.json();if(!r.ok)throw new Error(j.error||r.status);
  fill(j);st.textContent=j.rebooting?'Saved, restarting...':'Saved';
 }catch(err){st.textContent='Failed: '+err.message;}
});
document.getElementById('rb').addEventListener('click',async()=>{
 if(!confirm('Restart the clock?'))return;
 st.textContent='Restarting...';
 try{await fetch('/reboot',{method:'POST'});}catch(e){}
 setTimeout(()=>location.reload(),8000);
});
</script></body></html>)HTML";
