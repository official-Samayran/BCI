#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_ADS1X15.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HTTPClient.h>

/* ---------- CONFIGURATION ---------- */
const char* ssid = "NeuralGate_Pro";
const char* password = "password123";
uint8_t relayMAC[] = {0xE8, 0x68, 0xE7, 0xDC, 0xDE, 0x76};
uint8_t phoneMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};
uint8_t* activeDevice = relayMAC; 

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Adafruit_ADS1115 ads;

float Q = 0.00005, R = 0.08, P = 1.0, K = 0, X = 0; 
float prev_v = 0, threshold = 100.0; 
bool relayState = false;

/* ---------- DASHBOARD UI (LAG-FREE JS) ---------- */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  :root{--bg:#0a0a0b;--card:#161618;--text:#e0e0e0;--accent:#007aff;--border:rgba(255,255,255,0.08)}
  body{margin:0;font-family:sans-serif;background:var(--bg);color:var(--text);overflow:hidden}
  .nav{display:flex;justify-content:space-between;padding:15px 20px;background:var(--card);border-bottom:1px solid var(--border)}
  .container{max-width:800px;margin:10px auto;padding:0 15px}
  .card{background:var(--card);padding:20px;border-radius:24px;border:1px solid var(--border);margin-bottom:15px}
  .canvas-wrap{height:250px;background:#000;border-radius:18px;overflow:hidden;border:1px solid var(--border)}
  canvas{width:100%;height:100%}
  select{width:100%;padding:14px;background:#000;color:#fff;border-radius:12px;border:1px solid var(--border);font-weight:bold;margin-top:10px}
  .slider {width: 100%; height: 10px; border-radius: 5px; background: #333; outline: none; -webkit-appearance: none; margin: 20px 0;}
  .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 25px; height: 25px; border-radius: 50%; background: var(--accent); cursor: pointer;}
  .btn{padding:16px;background:var(--accent);color:#fff;border:none;border-radius:14px;width:100%;font-weight:bold;cursor:pointer;font-size:1.1rem;margin-top:10px}
  .label{font-size:0.9rem;color:var(--accent);font-weight:bold;float:right}
</style>
</head><body>
<div class="nav"><div style="font-weight:900;">NEURAL<span style="color:var(--accent)">GATE</span></div><div id="bt">Online</div></div>
<div class="container">
  <div class="card"><div class="canvas-wrap"><canvas id="c"></canvas></div></div>
  <div class="card">
    <select onchange="fetch('/setTarget?t='+this.value)">
      <option value="relay">Relay Module (ESP-01)</option>
      <option value="phone">Smartphone (MacroDroid)</option>
    </select>
    <div style="margin-top:20px;font-size:0.9rem;opacity:0.8">LIMIT: <span class="label" id="val">100</span></div>
    <input type="range" min="10" max="400" value="100" class="slider" id="thSlider" oninput="updateLimit(this.value)">
    <button class="btn" onclick="fetch('/manual')">MANUAL TRIGGER</button>
  </div>
</div>
<script>
let cvs=document.getElementById("c"), ctx=cvs.getContext("2d"), data=[], max=150, limit=100, graphMax=200;
function updateLimit(v) { limit=v; document.getElementById('val').innerText=v; fetch('/setTh?v='+v); }

// RENDER LOOP (Fix for Lag)
function render() {
  ctx.clearRect(0,0,cvs.width,cvs.height);
  let currentMax = Math.max(...data, limit, 50);
  graphMax = (graphMax * 0.95) + (currentMax * 1.1 * 0.05);
  
  let limitY = cvs.height - (limit / graphMax * cvs.height);
  ctx.beginPath(); ctx.strokeStyle="rgba(255, 0, 0, 0.7)"; ctx.setLineDash([5, 5]);
  ctx.moveTo(0, limitY); ctx.lineTo(cvs.width, limitY); ctx.stroke(); ctx.setLineDash([]);
  
  ctx.beginPath(); ctx.strokeStyle="#007aff"; ctx.lineWidth=3;
  for(let i=0;i<data.length;i++){
    let x=(i/max)*cvs.width, y=cvs.height - (data[i]/graphMax * cvs.height);
    if(i==0)ctx.moveTo(x,y); else ctx.lineTo(x,y);
  }
  ctx.stroke();
  requestAnimationFrame(render);
}

let ws=new WebSocket("ws://"+location.host+"/ws");
ws.onmessage=(e)=>{ try{ let o=JSON.parse(e.data); data.push(o.p); if(data.length>max)data.shift(); }catch(err){} };
cvs.width=cvs.clientWidth; cvs.height=cvs.clientHeight;
render();
</script></body></html>
)rawliteral";

/* ---------- LOGIC ---------- */
void performAction() {
  if (activeDevice == phoneMAC) {
    HTTPClient http;
    http.begin("http://192.168.4.2:8080/trigger"); 
    http.setTimeout(150); // Instant timeout
    http.GET(); http.end();
  } else {
    relayState = !relayState;
    uint8_t s = relayState ? 1 : 0;
    // Priority Send
    esp_now_send(relayMAC, &s, 1);
    Serial.println(">>> RELAY SENT <<<");
  }
}

void setup() {
  Serial.begin(115200);
  ads.begin(); ads.setGain(GAIN_TWO); ads.setDataRate(RATE_ADS1115_860SPS);
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  WiFi.setSleep(false); // Lag fix
  
  esp_now_init();
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, relayMAC, 6);
  peerInfo.channel = 0; peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });
  server.on("/manual", HTTP_GET, [](AsyncWebServerRequest *r){ performAction(); r->send(200); });
  server.on("/setTh", HTTP_GET, [](AsyncWebServerRequest *r){ if(r->hasParam("v")) threshold = r->getParam("v")->value().toFloat(); r->send(200); });
  server.on("/setTarget", HTTP_GET, [](AsyncWebServerRequest *r){
    if(r->hasParam("t")){
      String t = r->getParam("t")->value();
      activeDevice = (t == "phone") ? phoneMAC : relayMAC;
      Serial.println("Switched Target");
    }
    r->send(200);
  });
  
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  float rV = ads.computeVolts(ads.readADC_SingleEnded(0));
  float rW = rV - prev_v; prev_v = rV;
  P = P + Q; K = P / (P + R); X = X + K * (rW - X); P = (1 - K) * P;

  float focusPower = abs(X * 8000); 
  static unsigned long lastTrigger = 0, lastWS = 0;

  // 1. Plotter (Always Active)
  Serial.print("Focus_Power:"); Serial.print(focusPower); Serial.print(",");
  Serial.print("Limit:"); Serial.println(threshold);

  // 2. Trigger (Instant)
  if (focusPower > threshold && (millis() - lastTrigger > 3000)) {
    performAction();
    lastTrigger = millis();
  }

  // 3. WS Update (Balanced)
  if(millis() - lastWS > 80){ 
    if(ws.count() > 0 && ws.availableForWriteAll()){
        ws.textAll("{\"p\":" + String(focusPower, 2) + "}");
    }
    lastWS = millis();
    ws.cleanupClients(); 
  }
}
