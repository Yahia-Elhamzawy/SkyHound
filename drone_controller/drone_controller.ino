#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <MPU9250_WE.h>

#define MPU9250_ADDR 0x68
MPU9250_WE myMPU9250 = MPU9250_WE(MPU9250_ADDR);

const char* ssid     = "SkyHund";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

Servo esc1; // Front-Left
Servo esc2; // Front-Right
Servo esc3; // Back-Left
Servo esc4; // Back-Right

const int esc1Pin = 13; // Front-Left  (شمال قدام)
const int esc2Pin = 32; // Front-Right (يمين قدام)
const int esc3Pin = 14; // Back-Left   (شمال ورا)
const int esc4Pin = 27; // Back-Right  (يمين ورا)

// ====================== GLOBAL VARIABLES ======================
unsigned long loop_timer = 0;
TaskHandle_t WebServerTask;

// ====================== GLOBAL SENSOR STATE ======================
xyzFloat g_angles = {0, 0, 0};

float pitch = 0, roll = 0, yaw = 0;

float current_throttle = 1000;
float target_throttle  = 1000;
float target_pitch     = 0;
float target_roll      = 0;
float target_yaw       = 0;

unsigned long lastThrottleChange = 0;

bool testing_mode = false;
int  test_motor   = 0;

// ====================== SPEED DOUBLE-TAP SYSTEM ======================
// أول ضغطة = 80% من السرعة، تاني ضغطة = 100%
int current_speed_level = 0;  // اللفل الحالي (1-4)
bool speed_full = false;      // false = 80%, true = 100%

// جدول السرعات: [80%, 100%] لكل لفل
const int speed_table[4][2] = {
  {1160, 1200},  // speed1: 80%=1160, 100%=1200
  {1280, 1350},  // speed2: 80%=1280, 100%=1350
  {1400, 1500},  // speed3: 80%=1400, 100%=1500
  {1520, 1650},  // speed4: 80%=1520, 100%=1650
};

// ====================== PID GAINS ======================
// القيم الافتراضية (للـ Reset)
const float DEFAULT_Kp = 0.8;
const float DEFAULT_Kd = 0.005;
const float DEFAULT_Kp_yaw = 1.5;
const float DEFAULT_Kd_yaw = 0.01;

float Kp = DEFAULT_Kp;
float Kd = DEFAULT_Kd;

// 👈 جديد: PID للـ Yaw (بيشتغل بالـ Gyro Rate مش الزاوية)
float Kp_yaw = DEFAULT_Kp_yaw;
float Kd_yaw = DEFAULT_Kd_yaw;

// ====================== DISCONNECT SAFETY ======================
unsigned long lastCommandTime = 0;
const unsigned long DISCONNECT_TIMEOUT = 5000; // 5 ثواني

// ====================== MOTOR TRIM ======================
int trim1 = 0;   // Front-Left
int trim2 = 0;   // Front-Right
int trim3 = 0;   // Back-Left
int trim4 = 0;  // Back-Right

// ====================== HTML ======================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no"><title>DRONE CTRL</title><style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700;900&display=swap');
  :root {
    --bg:        #0a0c10;
    --panel:     #0f1318;
    --border:    #1e2a38;
    --green:     #00ff88;
    --cyan:      #00cfff;
    --red:       #ff3b5c;
    --orange:    #ff8c00;
    --yellow:    #f5c400;
    --dim:       #3a4a5a;
    --text:      #c8d8e8;
    --font-hud:  'Share Tech Mono', monospace;
    --font-title:'Orbitron', sans-serif;
  }
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-hud);
    height: 100%; width: 100%;
    overflow: hidden;
    user-select: none;
    -webkit-user-select: none;
  }
  body::after {
    content: '';
    position: fixed; inset: 0; pointer-events: none; z-index: 999;
    background: repeating-linear-gradient(
      0deg, transparent, transparent 3px,
      rgba(0,0,0,.15) 3px, rgba(0,0,0,.15) 4px
    );
  }
  #app {
    display: grid;
    grid-template-rows: auto 1fr auto;
    height: 100svh;
    max-width: 700px;
    margin: auto;
    padding: 8px;
    gap: 6px;
  }
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    border-bottom: 1px solid var(--border);
    padding-bottom: 6px;
  }
  .logo {
    font-family: var(--font-title);
    font-size: 18px;
    font-weight: 900;
    letter-spacing: 4px;
    color: var(--green);
    text-shadow: 0 0 12px var(--green);
  }
  .logo span { color: var(--cyan); }
  .conn-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--green);
    box-shadow: 0 0 8px var(--green);
    animation: blink 1.2s infinite;
  }
  @keyframes blink { 50% { opacity: .3; } }
  .hud { display: flex; gap: 8px; justify-content: center; flex-wrap: wrap; }
  .hud-item {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 4px 12px;
    display: flex; flex-direction: column; align-items: center;
    min-width: 76px;
  }
  .hud-label { font-size: 9px; letter-spacing: 2px; color: var(--dim); text-transform: uppercase; }
  .hud-val   { font-size: 18px; font-weight: bold; color: var(--cyan); }
  .hud-val.warn { color: var(--orange); }
  .viewport {
    display: flex; align-items: center; justify-content: center;
    perspective: 900px;
    flex: 1;
    position: relative;
  }
  .horizon {
    position: absolute;
    width: 240px; height: 240px;
    border-radius: 50%;
    border: 1px solid rgba(0,207,255,.15);
    background: radial-gradient(ellipse at center, rgba(0,207,255,.04) 0%, transparent 70%);
  }
  .horizon::before, .horizon::after {
    content: ''; position: absolute; background: rgba(0,207,255,.1);
  }
  .horizon::before { width: 100%; height: 1px; top: 50%; left: 0; }
  .horizon::after  { width: 1px; height: 100%; left: 50%; top: 0; }
  .thr-track {
    position: absolute; right: 6px; top: 10%; height: 80%; width: 12px;
    background: var(--panel); border: 1px solid var(--border); border-radius: 6px; overflow: hidden;
  }
  .thr-fill {
    position: absolute; bottom: 0; width: 100%;
    background: linear-gradient(to top, var(--green), var(--cyan));
    border-radius: 6px; transition: height .15s;
    box-shadow: 0 0 8px var(--green);
  }
  .drone {
    width: 180px; height: 180px;
    position: relative;
    transform-style: preserve-3d;
    transition: transform 80ms linear;
  }
  .arm { position: absolute; border-radius: 6px; }
  .arm-h {
    top: 82px; left: 0; width: 180px; height: 16px;
    background: linear-gradient(90deg, #1c2e1c, var(--green), #1c2e1c);
    box-shadow: 0 0 6px rgba(0,255,136,.5);
  }
  .arm-v {
    top: 0; left: 82px; width: 16px; height: 180px;
    background: linear-gradient(180deg,
      rgba(255,60,60,0) 0%, rgba(255,60,60,.6) 48%,
      rgba(51,51,255,.6) 52%, rgba(51,51,255,0) 100%);
  }
  .hub {
    position: absolute; top: 66px; left: 66px; width: 48px; height: 48px;
    background: linear-gradient(135deg, #2a3a2a, #111);
    border: 2px solid var(--green); border-radius: 10px;
    box-shadow: 0 0 14px rgba(0,255,136,.4), inset 0 0 8px rgba(0,0,0,.8);
    transform: translateZ(8px);
  }
  .hub::after {
    content: ''; position: absolute; top: 50%; left: 50%;
    width: 10px; height: 10px; border-radius: 50%;
    background: var(--cyan); box-shadow: 0 0 8px var(--cyan);
    transform: translate(-50%,-50%);
  }
  .prop {
    position: absolute; width: 36px; height: 36px;
    border-radius: 50%; border: 2px solid;
    display: flex; align-items: center; justify-content: center;
    font-size: 7px; letter-spacing: 1px; text-transform: uppercase;
  }
  .p1 { top: 2px;    left: 2px;   border-color: var(--green); color: var(--green); }
  .p2 { top: 2px;    right: 2px;  border-color: var(--cyan);  color: var(--cyan);  }
  .p3 { bottom: 2px; left: 2px;   border-color: var(--cyan);  color: var(--cyan);  }
  .p4 { bottom: 2px; right: 2px;  border-color: var(--green); color: var(--green); }
  .prop-spin { animation: spin .55s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .lbl-front {
    position: absolute; top: -20px; left: 50%; transform: translateX(-50%);
    font-size: 9px; letter-spacing: 2px;
    color: var(--red); text-shadow: 0 0 6px var(--red);
  }
  .controls { display: flex; flex-direction: column; gap: 6px; }
  .btn-stop {
    width: 100%; padding: 12px;
    background: transparent; border: 2px solid var(--red);
    color: var(--red); font-family: var(--font-title);
    font-size: 13px; font-weight: 700; letter-spacing: 4px;
    border-radius: 6px; cursor: pointer;
    text-shadow: 0 0 8px var(--red);
    box-shadow: 0 0 12px rgba(255,59,92,.2);
    transition: all .12s;
  }
  .btn-stop:active { background: var(--red); color: #000; box-shadow: 0 0 24px var(--red); }
  .row { display: flex; gap: 5px; justify-content: center; }
  .btn {
    padding: 9px 0;
    border: 1px solid var(--border); background: var(--panel);
    color: var(--text); font-family: var(--font-hud);
    font-size: 12px; letter-spacing: 1px;
    border-radius: 5px; cursor: pointer; flex: 1;
    transition: all .12s; text-align: center;
  }
  .btn:active { transform: scale(.95); }
  .btn-go   { border-color: var(--green); color: var(--green); }
  .btn-go:active   { background: rgba(0,255,136,.15); }
  .btn-land { border-color: var(--red); color: var(--red); }
  .btn-land:active { background: rgba(255,59,92,.15); }
  .btn-spd  { border-color: var(--orange); color: var(--orange); }
  .btn-spd:active,
  .btn-spd.sel { background: rgba(255,140,0,.18); box-shadow: 0 0 8px rgba(255,140,0,.3); }
  .dpad {
    display: grid;
    grid-template-columns: repeat(3, 44px);
    grid-template-rows: repeat(3, 44px);
    gap: 4px; justify-content: center;
  }
  .dpad .btn { padding: 0; font-size: 16px; min-width: 44px; height: 44px; flex: none; display: flex; align-items: center; justify-content: center; }
  .dpad .empty { background: transparent; border-color: transparent; }
  .btn-dir { border-color: var(--cyan); color: var(--cyan); }
  .btn-dir:active { background: rgba(0,207,255,.15); box-shadow: 0 0 8px rgba(0,207,255,.3); }
  .test-panel {
    flex: 1;
    background: var(--panel);
    border: 1px solid var(--yellow);
    border-radius: 6px; padding: 8px;
  }
  .test-title {
    font-size: 9px; letter-spacing: 3px; color: var(--yellow);
    text-align: center; margin-bottom: 6px;
  }
  .test-row { display: flex; gap: 5px; }
  .test-row + .test-row { margin-top: 4px; }
  .btn-test { border-color: var(--yellow); color: var(--yellow); font-size: 10px; }
  .btn-test:active { background: rgba(245,196,0,.15); }
  .hint { text-align: center; font-size: 9px; color: var(--dim); letter-spacing: 1px; padding: 2px 0; }
</style></head><body><div id="app">

  <!-- HEADER -->
  <header>
    <div class="logo">DRONE<span>CTRL</span></div>
    <div class="hud">
      <div class="hud-item">
        <span class="hud-label">PITCH</span>
        <span class="hud-val" id="vPitch">0.0</span>
      </div>
      <div class="hud-item">
        <span class="hud-label">ROLL</span>
        <span class="hud-val" id="vRoll">0.0</span>
      </div>
      <div class="hud-item">
        <span class="hud-label">THR</span>
        <span class="hud-val" id="vThr">1000</span>
      </div>
    </div>
    <div class="conn-dot" id="connDot"></div>
  </header>

  <!-- VIEWPORT -->
  <div class="viewport">
    <div class="horizon"></div>
    <div class="thr-track">
      <div class="thr-fill" id="thrFill" style="height:0%"></div>
    </div>
    <div class="drone" id="droneObj">
      <div class="lbl-front">FRONT</div>
      <div class="arm arm-h"></div>
      <div class="arm arm-v"></div>
      <div class="hub"></div>
      <div class="prop p1" id="p1">FL</div>
      <div class="prop p2" id="p2">FR</div>
      <div class="prop p3" id="p3">BL</div>
      <div class="prop p4" id="p4">BR</div>
    </div>
  </div>

  <!-- CONTROLS -->
  <div class="controls">
    <button class="btn-stop"
      onmousedown="cmd('stop')" ontouchstart="cmd('stop')">
      ⚡ EMERGENCY STOP [T]
    </button>

    <div class="row">
      <button class="btn btn-go"
        onmousedown="cmd('takeoff')" ontouchstart="cmd('takeoff')">▲ TAKE OFF [SPACE]</button>
      <button class="btn btn-land"
        onmousedown="cmd('land')" ontouchstart="cmd('land')">▼ LAND [5]</button>
    </div>

    <div class="row">
      <button class="btn btn-spd" id="s1" onmousedown="setSpd('speed1',1)">SPD 1</button>
      <button class="btn btn-spd" id="s2" onmousedown="setSpd('speed2',2)">SPD 2</button>
      <button class="btn btn-spd" id="s3" onmousedown="setSpd('speed3',3)">SPD 3</button>
      <button class="btn btn-spd" id="s4" onmousedown="setSpd('speed4',4)">SPD 4</button>
    </div>

    <div style="display:flex; gap:6px; align-items:center; justify-content:center;">

      <!-- D-pad -->
      <div class="dpad">
        <div class="btn empty"></div>
        <button class="btn btn-dir"
          onmousedown="cmd('forward')" onmouseup="cmd('stop_dir')"
          ontouchstart="cmd('forward')" ontouchend="cmd('stop_dir')">▲</button>
        <div class="btn empty"></div>

        <button class="btn btn-dir"
          onmousedown="cmd('left')" onmouseup="cmd('stop_dir')"
          ontouchstart="cmd('left')" ontouchend="cmd('stop_dir')">◀</button>
        <div class="btn empty"></div>
        <button class="btn btn-dir"
          onmousedown="cmd('right')" onmouseup="cmd('stop_dir')"
          ontouchstart="cmd('right')" ontouchend="cmd('stop_dir')">▶</button>

        <div class="btn empty"></div>
        <button class="btn btn-dir"
          onmousedown="cmd('backward')" onmouseup="cmd('stop_dir')"
          ontouchstart="cmd('backward')" ontouchend="cmd('stop_dir')">▼</button>
        <div class="btn empty"></div>
      </div>

      <!-- Motor test -->
      <div class="test-panel">
        <div class="test-title">⚙ MOTOR TEST</div>
        <div class="test-row">
          <button class="btn btn-test"
            onmousedown="cmd('test_1')" onmouseup="cmd('test_stop')"
            ontouchstart="cmd('test_1')" ontouchend="cmd('test_stop')">FL</button>
          <button class="btn btn-test"
            onmousedown="cmd('test_2')" onmouseup="cmd('test_stop')"
            ontouchstart="cmd('test_2')" ontouchend="cmd('test_stop')">FR</button>
        </div>
        <div class="test-row">
          <button class="btn btn-test"
            onmousedown="cmd('test_3')" onmouseup="cmd('test_stop')"
            ontouchstart="cmd('test_3')" ontouchend="cmd('test_stop')">BL</button>
          <button class="btn btn-test"
            onmousedown="cmd('test_4')" onmouseup="cmd('test_stop')"
            ontouchstart="cmd('test_4')" ontouchend="cmd('test_stop')">BR</button>
        </div>
      </div>

    </div>

    <div class="hint">WASD · 1-4 · SPACE · 5 · T</div>
  </div>

</div><script>
  const drone   = document.getElementById('droneObj');
  const thrFill = document.getElementById('thrFill');
  const connDot = document.getElementById('connDot');
  const props   = ['p1','p2','p3','p4'].map(id => document.getElementById(id));
  let   spinning = false;

  function cmd(c) { fetch('/control?cmd=' + c).catch(() => {}); }

  function setSpd(c, n) {
    cmd(c);
    document.querySelectorAll('.btn-spd').forEach(b => b.classList.remove('sel'));
    document.getElementById('s' + n).classList.add('sel');
  }

  setInterval(() => {
    fetch('/data')
      .then(r => r.json())
      .then(d => {
        document.getElementById('vPitch').textContent = d.pitch.toFixed(1);
        document.getElementById('vRoll').textContent  = d.roll.toFixed(1);
        document.getElementById('vThr').textContent   = d.throttle;

        document.getElementById('vPitch').className = 'hud-val' + (Math.abs(d.pitch) > 20 ? ' warn' : '');
        document.getElementById('vRoll').className  = 'hud-val' + (Math.abs(d.roll)  > 20 ? ' warn' : '');

        drone.style.transform =
          `rotateX(${-d.pitch}deg) rotateY(${d.yaw}deg) rotateZ(${-d.roll}deg)`;

        const pct = Math.max(0, Math.min(100, (d.throttle - 1000) / 10));
        thrFill.style.height = pct + '%';

        const shouldSpin = d.throttle > 1050;
        if (shouldSpin !== spinning) {
          spinning = shouldSpin;
          props.forEach(p => shouldSpin
            ? p.classList.add('prop-spin')
            : p.classList.remove('prop-spin'));
        }

        connDot.style.background = '#00ff88';
        connDot.style.boxShadow  = '0 0 8px #00ff88';
      })
      .catch(() => {
        connDot.style.background = '#ff3b5c';
        connDot.style.boxShadow  = '0 0 8px #ff3b5c';
      });
  }, 100);

  document.addEventListener('keydown', e => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === ' ')  { e.preventDefault(); cmd('takeoff'); }
    if (k === '5')  cmd('land');
    if (k === 't')  cmd('stop');
    if (k === '1')  setSpd('speed1', 1);
    if (k === '2')  setSpd('speed2', 2);
    if (k === '3')  setSpd('speed3', 3);
    if (k === '4')  setSpd('speed4', 4);
    if (k === 'w')  cmd('forward');
    if (k === 's')  cmd('backward');
    if (k === 'a')  cmd('left');
    if (k === 'd')  cmd('right');
  });

  document.addEventListener('keyup', e => {
    if (['w','a','s','d'].includes(e.key.toLowerCase())) cmd('stop_dir');
  });
</script></body></html>
)rawliteral";

// ====================== WEB SERVER TASK (Core 0) ======================
// دالة تشغيل الواي فاي والسيرفر في خلفية النظام
void webServerLoop(void * pvParameters) {
  for(;;) {
    server.handleClient();
    vTaskDelay(10 / portTICK_PERIOD_MS); // إراحة النواة لمدة 10 ملي ثانية
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000); // 👈 تسريع الـ I2C لـ 400kHz لضمان سرعة اللوب فائقة

  esc1.attach(esc1Pin, 1000, 2000);
  esc2.attach(esc2Pin, 1000, 2000);
  esc3.attach(esc3Pin, 1000, 2000);
  esc4.attach(esc4Pin, 1000, 2000);

  esc1.writeMicroseconds(1000);
  esc2.writeMicroseconds(1000);
  esc3.writeMicroseconds(1000);
  esc4.writeMicroseconds(1000);

  if (!myMPU9250.init()) {
    Serial.println("MPU9250 not found!");
  } else {
    Serial.println("MPU9250 found, calibrating...");
    delay(2000);
    myMPU9250.autoOffsets();
    Serial.println("MPU9250 ready");
  }

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", [] {
    server.send(200, "text/html", index_html);
  });

  server.on("/data", [] {
    lastCommandTime = millis(); // الـ polling = دليل اتصال
    String json =
      "{\"pitch\":"    + String(g_angles.x, 1) +
      ",\"roll\":"     + String(g_angles.y, 1) +
      ",\"yaw\":"      + String(g_angles.z, 1) +
      ",\"throttle\":" + String((int)current_throttle) +
      ",\"trim\":[" + String(trim1) + "," + String(trim2) + "," + String(trim3) + "," + String(trim4) + "]" +
      ",\"pid\":{\"kp\":" + String(Kp, 2) + ",\"kd\":" + String(Kd, 4) + ",\"kp_yaw\":" + String(Kp_yaw, 2) + ",\"kd_yaw\":" + String(Kd_yaw, 4) + "}" +
      "}";
    server.send(200, "application/json", json);
  });

  server.on("/control", [] {
    String cmd = server.arg("cmd");
    lastCommandTime = millis(); // كل أمر = دليل اتصال

    if      (cmd == "test_1")    { testing_mode = true;  test_motor = 1; }
    else if (cmd == "test_2")    { testing_mode = true;  test_motor = 2; }
    else if (cmd == "test_3")    { testing_mode = true;  test_motor = 3; }
    else if (cmd == "test_4")    { testing_mode = true;  test_motor = 4; }
    else if (cmd == "test_stop") { testing_mode = false; test_motor = 0; }

    else if (cmd == "takeoff")   { current_speed_level = 1; speed_full = false; target_throttle = speed_table[0][0]; }
    else if (cmd == "land")      { current_speed_level = 0; speed_full = false; target_throttle = 1000; }
    else if (cmd == "stop")      { current_speed_level = 0; speed_full = false; target_throttle = 1000; current_throttle = 1000; testing_mode = false; target_yaw = 0; }
    else if (cmd == "speed1" || cmd == "speed2" || cmd == "speed3" || cmd == "speed4") {
      int level = cmd.charAt(5) - '0';  // استخرج رقم اللفل (1-4)
      if (level == current_speed_level && !speed_full) {
        // نفس اللفل + ضغطة تانية → 100%
        speed_full = true;
      } else {
        // لفل جديد → ابدأ من 80%
        current_speed_level = level;
        speed_full = false;
      }
      target_throttle = speed_table[level - 1][speed_full ? 1 : 0];
    }
    // === Direction ===
    else if (cmd == "forward")   target_pitch =  15;
    else if (cmd == "backward")  target_pitch = -15;
    else if (cmd == "left")      target_roll  = -15;
    else if (cmd == "right")     target_roll  =  15;
    else if (cmd == "stop_dir")  { target_pitch = 0; target_roll = 0; }
    // === Yaw (للجيمباد) ===
    else if (cmd == "yaw_left")  target_yaw = -50;
    else if (cmd == "yaw_right") target_yaw =  50;
    else if (cmd == "stop_yaw")  target_yaw =  0;
    // === Trim ===
    else if (cmd == "trim_1_up")   trim1 += 5;
    else if (cmd == "trim_1_down") trim1 -= 5;
    else if (cmd == "trim_2_up")   trim2 += 5;
    else if (cmd == "trim_2_down") trim2 -= 5;
    else if (cmd == "trim_3_up")   trim3 += 5;
    else if (cmd == "trim_3_down") trim3 -= 5;
    else if (cmd == "trim_4_up")   trim4 += 5;
    else if (cmd == "trim_4_down") trim4 -= 5;
    else if (cmd == "trim_reset")  { trim1 = 0; trim2 = 0; trim3 = 0; trim4 = 0; }
    // === PID Tuning ===
    else if (cmd == "kp_up")       Kp += 0.1;
    else if (cmd == "kp_down")     Kp = max(0.0f, Kp - 0.1f);
    else if (cmd == "kd_up")       Kd += 0.001;
    else if (cmd == "kd_down")     Kd = max(0.0f, Kd - 0.001f);
    else if (cmd == "kp_yaw_up")   Kp_yaw += 0.1;
    else if (cmd == "kp_yaw_down") Kp_yaw = max(0.0f, Kp_yaw - 0.1f);
    else if (cmd == "kd_yaw_up")   Kd_yaw += 0.001;
    else if (cmd == "kd_yaw_down") Kd_yaw = max(0.0f, Kd_yaw - 0.001f);
    else if (cmd == "pid_reset")   { Kp = DEFAULT_Kp; Kd = DEFAULT_Kd; Kp_yaw = DEFAULT_Kp_yaw; Kd_yaw = DEFAULT_Kd_yaw; }

    server.send(200, "text/plain", "OK");
  });

  server.begin();

  // 👈 بدء السيرفر في خلفية النظام (النواة Core 0)
  xTaskCreatePinnedToCore(
    webServerLoop,    // Task function
    "WebServerTask",  // Task name
    8192,             // Stack size
    NULL,             // Parameters
    1,                // Priority
    &WebServerTask,   // Task handle
    0                 // Core 0 (Wi-Fi and Web Server)
  );

  loop_timer = micros(); // 👈 بدء حساب وقت الـ Loop
}

// ====================== LOOP (Core 1) =======================
// الدالة دي أصبحت مخصصة للطيران فقط بدون أي تأخيرات
void loop() {
  unsigned long now = micros();
  
  // 👈 ضبط سرعة اللوب لتكون ثابتة كل 4000 مايكرو ثانية (4 ملي ثانية = 250Hz)
  if (now - loop_timer < 4000) {
    return;
  }
  
  // 👈 حساب الوقت الفعلي الذي استغرقته اللوب بالثواني (dt)
  float dt = (now - loop_timer) / 1000000.0;
  loop_timer = now;

  // 👈 Asymmetric throttle ramp: صعود بطيء (سلس) + نزول سريع (هبوط فوري)
  static int throttle_counter = 0;
  throttle_counter++;
  if (current_throttle < target_throttle) {
    // صعود: +1 كل 3 cycles (~12ms) = بطيء وسلس عشان ما يتنطرش
    if (throttle_counter >= 3) {
      current_throttle++;
      throttle_counter = 0;
    }
  } else if (current_throttle > target_throttle) {
    // نزول: -5 كل cycle (~4ms) = سريع جداً عشان يهبط فوراً
    current_throttle = max((float)target_throttle, current_throttle - 5);
    throttle_counter = 0;
  }

  // اقرأ الـ sensor
  g_angles = myMPU9250.getAngles();

  // 👈 جديد: قراءة سرعة الدوران من الـ Gyroscope (°/s) للـ Yaw
  xyzFloat gyr = myMPU9250.getGyrValues();

  // motor test mode
  if (testing_mode) {
    esc1.writeMicroseconds(test_motor == 1 ? 1150 : 1000);
    esc2.writeMicroseconds(test_motor == 2 ? 1150 : 1000);
    esc3.writeMicroseconds(test_motor == 3 ? 1150 : 1000);
    esc4.writeMicroseconds(test_motor == 4 ? 1150 : 1000);
    return;
  }

  // ====================== PD STABILISATION ======================
  float error_pitch = g_angles.x - target_pitch;
  float error_roll  = -(g_angles.y - target_roll);

  static float prev_error_pitch = 0;
  static float prev_error_roll  = 0;

  float d_pitch = (error_pitch - prev_error_pitch) / dt;
  float d_roll  = (error_roll  - prev_error_roll) / dt;

  prev_error_pitch = error_pitch;
  prev_error_roll  = error_roll;

  if (abs(error_pitch) < 0.5) { error_pitch = 0; d_pitch = 0; }
  if (abs(error_roll)  < 0.5) { error_roll  = 0; d_roll  = 0; }

  float pitchCorr = (error_pitch * Kp) + (d_pitch * Kd);
  float rollCorr  = (error_roll  * Kp) + (d_roll  * Kd);

  // ====================== 👈 جديد: YAW STABILISATION ======================
  // الـ Yaw بيشتغل بالـ Gyro Rate (سرعة الدوران °/s) مش الزاوية
  // الهدف: نخلي سرعة الدوران حوالين المحور Z = 0 (يعني ما يلفش)
  float yaw_rate = gyr.z;  // سرعة الدوران الحالية بالدرجات/ثانية
  float yaw_rate_error = yaw_rate - target_yaw;  // target_yaw = 0 يعني ما يلفش

  static float prev_yaw_rate_error = 0;
  float d_yaw = (yaw_rate_error - prev_yaw_rate_error) / dt;
  prev_yaw_rate_error = yaw_rate_error;

  if (abs(yaw_rate_error) < 2.0) { yaw_rate_error = 0; d_yaw = 0; }

  float yawCorr = (yaw_rate_error * Kp_yaw) + (d_yaw * Kd_yaw);

  // ====================== MOTOR MIXING ======================
  // M1 (FL) = CCW → +yaw    M2 (FR) = CW  → -yaw
  // M3 (BL) = CW  → -yaw    M4 (BR) = CCW → +yaw
  int m1 = constrain((int)(current_throttle - pitchCorr + rollCorr + yawCorr) + trim1, 1000, 2000);
  int m2 = constrain((int)(current_throttle - pitchCorr - rollCorr - yawCorr) + trim2, 1000, 2000);
  int m3 = constrain((int)(current_throttle + pitchCorr + rollCorr - yawCorr) + trim3, 1000, 2000);
  int m4 = constrain((int)(current_throttle + pitchCorr - rollCorr + yawCorr) + trim4, 1000, 2000);

  // ====================== DISCONNECT AUTO-LAND ======================
  if (current_throttle > 1050 && lastCommandTime > 0 && (millis() - lastCommandTime > DISCONNECT_TIMEOUT)) {
    // اتصال مفقود! هبوط تلقائي
    target_throttle = 1000;
    target_pitch = 0;
    target_roll = 0;
    target_yaw = 0;
  }

  if (current_throttle > 1050) {
    esc1.writeMicroseconds(m1);
    esc2.writeMicroseconds(m2);
    esc3.writeMicroseconds(m3);
    esc4.writeMicroseconds(m4);
  } else {
    esc1.writeMicroseconds(1000);
    esc2.writeMicroseconds(1000);
    esc3.writeMicroseconds(1000);
    esc4.writeMicroseconds(1000);
  }
}
