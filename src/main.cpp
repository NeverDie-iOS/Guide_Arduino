#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "soc/soc.h" 
#include "soc/rtc_cntl_reg.h" 
#include "esp_task_wdt.h" 

// DHCP
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"

// 핀맵
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

const char *ssid = "My_Smart_Cane";
const char *password = "00000000";

#define CAMERA_PIN D5
#define DEBOUNCE_DELAY 300

AsyncWebServer server(80);    
WiFiServer streamServer(81);  

bool btnClicked = false; 
unsigned long lastBtnTime = 0;

// ==========================================
// 웹 페이지
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <title>Smart Cane Live Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="utf-8">
  <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@400;600&display=swap" rel="stylesheet">
  <style>
    body {
      margin: 0; padding: 0;
      font-family: 'Poppins', sans-serif;
      background: linear-gradient(135deg, #1c1c1c 0%, #0f2027 100%);
      color: #ffffff; height: 100vh;
      display: flex; flex-direction: column;
      align-items: center; justify-content: center;
      overflow: hidden;
    }
    .dashboard-card {
      background: rgba(255, 255, 255, 0.05);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 24px; padding: 30px;
      width: 90%; max-width: 700px;
      box-shadow: 0 20px 50px rgba(0, 0, 0, 0.5);
      text-align: center;
    }
    h1 {
      margin: 0 0 5px 0; font-size: 24px;
      background: linear-gradient(90deg, #4facfe 0%, #00f2fe 100%);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    }
    p.subtitle { margin: 0 0 20px 0; color: #aaa; font-size: 14px; }
    #cam-container {
      position: relative; width: 100%;
      background-color: #000; border-radius: 16px;
      border: 2px solid #4facfe;
      box-shadow: 0 0 20px rgba(79, 172, 254, 0.2);
      overflow: hidden; display: flex;
      align-items: center; justify-content: center;
      aspect-ratio: 4/3;
    }
    img { width: 100%; height: 100%; object-fit: cover; display: block; }
    .status-bar {
      margin-top: 20px; display: inline-flex; align-items: center;
      padding: 8px 20px; background: rgba(0, 0, 0, 0.3);
      border-radius: 30px; border: 1px solid rgba(46, 204, 113, 0.3);
    }
    .status-dot {
      width: 10px; height: 10px; border-radius: 50%;
      background-color: #2ecc71; margin-right: 10px;
      box-shadow: 0 0 10px #2ecc71; animation: blink-green 2s infinite;
    }
    @keyframes blink-green {
      0% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0.7); }
      70% { box-shadow: 0 0 0 10px rgba(46, 204, 113, 0); }
      100% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0); }
    }
    #status { font-size: 14px; font-weight: 600; color: #fff; letter-spacing: 1px; }
  </style>
</head>
<body>
  <div class="dashboard-card">
    <h1>Live Monitor</h1>
    <p class="subtitle">Smart Cane Vision System</p>
    <div id="cam-container">
      <img id="stream" src="">
    </div>
    <div class="status-bar">
      <div class="status-dot"></div>
      <span id="status">System Online</span>
    </div>
  </div>
  <script>
    window.onload = function() {
      var streamImg = document.getElementById("stream");
      streamImg.src = "http://" + window.location.hostname + ":81/stream";
    };
  </script>
</body>
</html>
)rawliteral";

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) { delay(10); }

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
  
  config.xclk_freq_hz = 20000000; 
  config.frame_size = FRAMESIZE_VGA; 
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 14; 
  config.fb_count = 2; 

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera Failed");
    return;
  }

  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(0, 0, 0, 0);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
  dhcps_offer_t dhcp_offer = 0; 
  tcpip_adapter_dhcps_option(TCPIP_ADAPTER_OP_SET, TCPIP_ADAPTER_REQUESTED_IP_ADDRESS, &dhcp_offer, sizeof(dhcps_offer_t));
  tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);

  WiFi.softAP(ssid, password);
  Serial.println("System Ready");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Connection", "close");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // [폴링] 상태 확인 핸들러
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    if (btnClicked) {
      request->send(200, "text/plain", "1"); 
      btnClicked = false;
      Serial.println("Signal Sent to App: 1");
    } else {
      request->send(200, "text/plain", "0");
    }
  });

  // 캡처
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Error");
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", (const uint8_t *)fb->buf, fb->len);
    response->addHeader("Cache-Control", "no-store, no-cache");
    request->send(response);
    esp_camera_fb_return(fb);
  });

  server.begin();
  streamServer.begin();
}

void checkButton() {
  if (digitalRead(CAMERA_PIN) == HIGH) {
    if (millis() - lastBtnTime > DEBOUNCE_DELAY) {
      lastBtnTime = millis();
      Serial.println("🔘 Button Clicked! Flag Set.");
      btnClicked = true; 
    }
  }
}

void loop() {
  WiFiClient client = streamServer.available();
  
  if (client) {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");
    
    while (client.connected()) {
      checkButton(); 
      
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        delay(10);
        continue;
      }

      client.print("--frame\r\nContent-Type: image/jpeg\r\n\r\n");
      
      size_t sent = client.write(fb->buf, fb->len);
      client.print("\r\n");
      
      esp_camera_fb_return(fb);
      
      if (sent == 0) {
        break; 
      }
      
      delay(40); 
    }
    
    client.stop();
  }
  
  checkButton();
  
  delay(20);
}