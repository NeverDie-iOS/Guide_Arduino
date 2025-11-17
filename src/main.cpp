
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// ==========================================
// 초기 설정
const char *ssid = "Wifi_Test"; // AP 모드 SSID
const char *password = "00000000"; // AP 모드 Password

#define CAMERA_PIN D5  // 캠 버튼 핀 (Active High)
#define HOLD_THRESHOLD 500   // 0.5초 기준 (이상 누르면 영상 모드)

AsyncWebServer server(80);
AsyncEventSource events("/events"); // 실시간 알림을 위한 통로 (SSE)
// ==========================================

// D5 버튼 상태 추적 변수
unsigned long camBtnPressTime = 0; // 버튼이 눌린 시점 저장
bool isCamBtnPressing = false;  // 현재 홀드(영상) 상태인지 체크
bool lastCamBtnState = LOW; // Active High 방식이므로 초기 상태는 LOW (꺼짐)

// ==========================================
// 웹 페이지 (HTML + JS)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Button Test</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; margin-top: 30px; }
    h1 { color: #0D47A1; }
    p { font-size: 1.2rem; }
    #log { 
      width: 90%; max-width: 600px; margin: 0 auto; height: 400px; 
      border: 2px solid #333; overflow-y: scroll; 
      padding: 10px; text-align: left; background: #f4f4f4;
      font-family: monospace; font-size: 14px;
    }
    .click { color: #2E7D32; font-weight: bold; }     /* 초록색 */
    .hold-start { color: #C62828; font-weight: bold; } /* 빨간색 */
    .hold-stop { color: #EF6C00; font-weight: bold; }  /* 주황색 */
    .info { color: #555; }
  </style>
</head>
<body>
  <h1>Button Event Monitor</h1>
  
  <div id="log">
    Waiting for events...<br>
  </div>

  <script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');

      source.addEventListener('open', function(e) {
        console.log("Events Connected");
        logMessage("System Connected", "info");
      }, false);

      source.addEventListener('error', function(e) {
        if (e.target.readyState != EventSource.OPEN) {
          console.log("Events Disconnected");
          logMessage("System Disconnected", "info");
        }
      }, false);

      source.addEventListener('message', function(e) {
        console.log("message", e.data);
        logRawMessage(e.data);
      }, false);
    }

    function logMessage(msg, className) {
      var logDiv = document.getElementById("log");
      var today = new Date();
      var time = today.getHours().toString().padStart(2, '0') + ":" + 
                 today.getMinutes().toString().padStart(2, '0') + ":" + 
                 today.getSeconds().toString().padStart(2, '0');
      
      logDiv.innerHTML = "[" + time + "] <span class='" + className + "'>" + msg + "</span><br>" + logDiv.innerHTML;
    }

    function logRawMessage(htmlMsg) {
      var logDiv = document.getElementById("log");
      var today = new Date();
      var time = today.getHours().toString().padStart(2, '0') + ":" + 
                 today.getMinutes().toString().padStart(2, '0') + ":" + 
                 today.getSeconds().toString().padStart(2, '0');
      
      logDiv.innerHTML = "[" + time + "] " + htmlMsg + "<br>" + logDiv.innerHTML;
    }
  </script>
</body>
</html>
)rawliteral";
// ==========================================

// ==========================================
// setup
void setup() {
  Serial.begin(115200);

  pinMode(CAMERA_PIN, INPUT_PULLDOWN);   // 버튼을 누르면 3.3V(HIGH)가 들어오고, 떼면 LOW가 되도록 PULLDOWN 설정

  // ==========================================
  // Wi-Fi AP 모드 
  WiFi.softAP(ssid, password);
  
  Serial.println("\n=================================");
  Serial.println("AP Mode Started!");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("PW: ");   Serial.println(password);
  Serial.print("IP Address: http://"); Serial.println(WiFi.softAPIP());
  Serial.println("=================================\n");
  // ==========================================

  // ==========================================
  // 웹 서버 경로 설정
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){   // 루트 페이지 ("/") 접속 시 HTML 전송
    request->send_P(200, "text/html", index_html);
  });

  events.onConnect([](AsyncEventSourceClient *client){   // 이벤트 핸들러 설정 (SSE 연결)
    client->send("Web Client Connected", NULL, millis(), 1000);
    Serial.println("Web Client Connected via Wi-Fi");
  });
  server.addHandler(&events);

  server.begin();
  // ==========================================
}
// ==========================================

// ==========================================
// 5. Loop (버튼 감지 로직 - Active High)
void loop() {
  // 현재 버튼 상태 읽기 (누르면 HIGH, 안누르면 LOW)
  int currentCamBtnState = digitalRead(CAMERA_PIN);

  // -----------------------------------------------------
  // 케이스 1: [방금 막 눌렸을 때] (LOW -> HIGH)
  // -----------------------------------------------------
  if (lastCamBtnState == LOW && currentCamBtnState == HIGH) {
    camBtnPressTime = millis(); // 시간 재기 시작
    isCamBtnPressing = false;         // 홀드 상태 초기화
    Serial.println("⬇️ Cam Button Pressed (Down)");
  }

  // -----------------------------------------------------
  // 케이스 2: [누르고 있는 중] (HIGH 유지)
  // -----------------------------------------------------
  if (currentCamBtnState == HIGH) {
    // 누른지 0.5초가 지났고, 아직 홀드 처리가 안 됐다면 -> 영상 모드 진입
    if (!isCamBtnPressing && (millis() - camBtnPressTime > HOLD_THRESHOLD)) {
      isCamBtnPressing = true;
      
      Serial.println(">>> VIDEO START (Holding)");
      events.send("<span class='hold-start'>🎥 VIDEO START (Streaming...)</span>", "message", millis());
    }
  }

  // -----------------------------------------------------
  // 케이스 3: [방금 손을 뗐을 때] (HIGH -> LOW)
  // -----------------------------------------------------
  if (lastCamBtnState == HIGH && currentCamBtnState == LOW) {

    // (A) 홀드 상태였다가 뗀 경우 -> 영상 종료
    if (isCamBtnPressing) {
      Serial.println("<<< VIDEO STOP (Released)");
      events.send("<span class='hold-stop'>⏹ VIDEO STOP (End Stream)</span>", "message", millis());
      isCamBtnPressing = false;
    } 
    // (B) 홀드 되기 전(0.5초 이내)에 뗀 경우 -> 사진 캡처
    else {
      Serial.println("!!! PHOTO CAPTURE (Click)");
      events.send("<span class='click'>📸 PHOTO CAPTURE (Snap!)</span>", "message", millis());
    }
  }

  lastCamBtnState = currentCamBtnState;
  
  delay(20); // 디바운싱 딜레이
}
// ==========================================