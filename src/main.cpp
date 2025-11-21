#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "soc/soc.h" 
#include "soc/rtc_cntl_reg.h" 
#include "esp_task_wdt.h" 

// ==========================================
// 1. 핀맵
// ==========================================
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

// ==========================================
// 2. 설정
// ==========================================
const char *ssid = "My_Smart_Cane";
const char *password = "00000000";

#define CAMERA_PIN D5
#define HOLD_THRESHOLD 500

// ==========================================
// 3. 전역 변수
// ==========================================
AsyncWebServer server(80); 
WiFiServer streamServer(81); 
AsyncEventSource events("/events");

unsigned long camBtnPressTime = 0;
bool isCamBtnPressing = false;
bool lastCamBtnState = LOW;

unsigned long lastCaptureTime = 0;
const int CAPTURE_COOLDOWN = 1000; 
bool isBusy = false;

camera_fb_t *saved_fb = NULL; // 캡처 보관함

// ==========================================
// 4. 웹 페이지
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
 <title>Smart Cane Dashboard</title>
 <meta name="viewport" content="width=device-width, initial-scale=1">
 <style>
 body { font-family: sans-serif; text-align: center; background-color: #222; color: #eee; }
 #cam-container { margin: 20px auto; width: 100%; max-width: 640px; border: 2px solid #444; min-height: 300px; display: flex; align-items: center; justify-content: center; background: #000; }
 img { width: 100%; display: none; }
 #status { color: #aaa; margin-top: 10px; }
 </style>
</head>
<body>
 <h1>Smart Cane Dashboard</h1>
 <p>Latest Capture (Zero Lag Mode)</p>
 <div id="cam-container">
   <span id="placeholder">Waiting for Signal...</span>
   <img id="view" src="">
 </div>
 <div id="status">Ready</div>

 <script>
    var img = document.getElementById("view");
    var placeholder = document.getElementById("placeholder");
    var status = document.getElementById("status");
    
    var streamUrl = "http://" + window.location.hostname + ":81/stream";
    var captureUrl = "/capture";

    if (!!window.EventSource) {
      var source = new EventSource('/events');
      source.addEventListener('message', function(e) {
        console.log(e.data);
        if (e.data.includes("PHOTO CAPTURE")) {
           // 캐시 방지용 시간 추가
           img.src = captureUrl + "?t=" + new Date().getTime();
           showImage();
           status.innerText = "Photo Captured (Real-time)";
        } else if (e.data.includes("VIDEO START")) {
           img.src = streamUrl;
           showImage();
           status.innerText = "Video Streaming...";
        } else if (e.data.includes("VIDEO STOP")) {
           img.src = ""; 
           hideImage();
           status.innerText = "Video Stopped";
        }
      }, false);
    }
    
    function showImage() { img.style.display = "block"; placeholder.style.display = "none"; }
    function hideImage() { img.style.display = "none"; placeholder.style.display = "block"; }
 </script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. 버튼 로직
// ==========================================
void handleButtonLogic() {
 int currentCamBtnState = digitalRead(CAMERA_PIN);

 if (lastCamBtnState == LOW && currentCamBtnState == HIGH) {
   camBtnPressTime = millis();
   isCamBtnPressing = false;
 }

 // 홀드 (영상)
 if (currentCamBtnState == HIGH) {
   if (!isCamBtnPressing && (millis() - camBtnPressTime > HOLD_THRESHOLD)) {
     isCamBtnPressing = true;
     // 스트리밍 시작 시 저장된 사진 삭제
     if (saved_fb) {
       esp_camera_fb_return(saved_fb);
       saved_fb = NULL;
     }
     events.send("VIDEO START", "message", millis());
   }
 }

 // 사진 캡처
 if (lastCamBtnState == HIGH && currentCamBtnState == LOW) {
   if (isCamBtnPressing) {
     events.send("VIDEO STOP", "message", millis());
     isCamBtnPressing = false;
   } else {
     unsigned long now = millis();
     if (now - lastCaptureTime > CAPTURE_COOLDOWN) {
       lastCaptureTime = now;
       
       // 기존 저장 사진 삭제
       if (saved_fb) {
         esp_camera_fb_return(saved_fb);
         saved_fb = NULL;
       }
       
       camera_fb_t * temp_fb = NULL;

       // 버퍼 비우기
       for(int i=0; i<2; i++){
          temp_fb = esp_camera_fb_get();
          if(temp_fb) esp_camera_fb_return(temp_fb);
          delay(10);
       }
       
       // 최신 사진 촬영 및 저장
       saved_fb = esp_camera_fb_get();
       
       if (saved_fb) {
         Serial.printf("📸 Fresh Snap! Saved (%u bytes)\n", saved_fb->len);
         events.send("PHOTO CAPTURE", "message", millis());
       } else {
         Serial.println("❌ Camera Capture Failed");
       }
     }
   }
 }
 lastCamBtnState = currentCamBtnState;
}

// ==========================================
// 6. Setup
// ==========================================
void setup() {
 WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
 Serial.begin(115200);
 esp_task_wdt_init(30, false); 
 
 WiFi.setSleep(false);
 pinMode(CAMERA_PIN, INPUT_PULLDOWN);

 camera_config_t config;
 config.ledc_channel = LEDC_CHANNEL_0;
 config.ledc_timer = LEDC_TIMER_0;
 config.pin_d0 = Y2_GPIO_NUM;
 config.pin_d1 = Y3_GPIO_NUM;
 config.pin_d2 = Y4_GPIO_NUM;
 config.pin_d3 = Y5_GPIO_NUM;
 config.pin_d4 = Y6_GPIO_NUM;
 config.pin_d5 = Y7_GPIO_NUM;
 config.pin_d6 = Y8_GPIO_NUM;
 config.pin_d7 = Y9_GPIO_NUM;
 config.pin_xclk = XCLK_GPIO_NUM;
 config.pin_pclk = PCLK_GPIO_NUM;
 config.pin_vsync = VSYNC_GPIO_NUM;
 config.pin_href = HREF_GPIO_NUM;
 config.pin_sccb_sda = SIOD_GPIO_NUM;
 config.pin_sccb_scl = SIOC_GPIO_NUM;
 config.pin_pwdn = PWDN_GPIO_NUM;
 config.pin_reset = RESET_GPIO_NUM;
 
 config.xclk_freq_hz = 10000000; 
 config.frame_size = FRAMESIZE_SVGA; 
 config.pixel_format = PIXFORMAT_JPEG;
 config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
 config.fb_location = CAMERA_FB_IN_PSRAM;
 config.jpeg_quality = 12;
 config.fb_count = 3; 

 if (esp_camera_init(&config) != ESP_OK) {
   Serial.println("Camera Failed");
   return;
 }

 WiFi.softAP(ssid, password);
 Serial.print("IP: http://"); Serial.println(WiFi.softAPIP());

 server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
   request->send_P(200, "text/html", index_html);
 });

 // 캡처 핸들러 (저장된 사진 제공)
 server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
   if (saved_fb) {
     request->send_P(200, "image/jpeg", (const uint8_t *)saved_fb->buf, saved_fb->len);
   } else {
     request->send(404, "text/plain", "No Photo Yet");
   }
 });

 events.onConnect([](AsyncEventSourceClient *client){ client->send("Connected", NULL, millis(), 1000); });
 server.addHandler(&events);
 
 server.begin();
 streamServer.begin();
}

// ==========================================
// 7. Loop
// ==========================================
void loop() {
 WiFiClient client = streamServer.available();
 if (client) {
   // 스트리밍 시작 시 저장된 사진 메모리 해제
   if (saved_fb) {
     esp_camera_fb_return(saved_fb);
     saved_fb = NULL;
   }

   client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");
   while (client.connected()) {
     handleButtonLogic();
     camera_fb_t * fb = esp_camera_fb_get();
     if (!fb) break;
     client.print("--frame\r\nContent-Type: image/jpeg\r\n\r\n");
     client.write(fb->buf, fb->len);
     client.print("\r\n");
     esp_camera_fb_return(fb);
     delay(20); 
   }
   client.stop();
 }

 handleButtonLogic();
 delay(20);
}