/*
 * ESP32-CAM Drone AI Camera
 * ===========================
 * Firmware for AI-powered drone camera
 * Streams MJPEG video & provides capture endpoint for AI analysis
 * 
 * Mode: Station (connects to Drone's SkyHund WiFi)
 * Board: AI Thinker ESP32-CAM
 * Partition: Huge APP (3MB No OTA/1MB SPIFFS)
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>

// ============================================
// Station Mode - Connect to Drone's Network
// ============================================
const char* DRONE_SSID = "SkyHund";
const char* DRONE_PASS = "YOUR_WIFI_PASSWORD";
const int   MAX_RETRIES = 5;       // عدد محاولات الاتصال
const int   RETRY_DELAY = 3000;    // وقت الانتظار بين كل محاولة (3 ثواني)
// ============================================

// AI-Thinker ESP32-CAM Pin Definitions
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22
#define FLASH_LED       4

httpd_handle_t httpd_main = NULL;
httpd_handle_t httpd_stream = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CT = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUND = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ===== Camera Initialization =====
bool initCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;  // 800x600
    config.jpeg_quality = 10;
    config.fb_count = 3;
  } else {
    config.frame_size = FRAMESIZE_VGA;   // 640x480
    config.jpeg_quality = 14;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[ERROR] Camera init FAILED!");
    return false;
  }

  // Optimize camera settings for drone aerial view
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
  }

  Serial.println("[OK] Camera initialized");
  return true;
}

// ===== WiFi Station Mode - Connect to Drone's Network =====
bool connectToNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);  // إعادة الاتصال تلقائياً لو انقطع

  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("[WIFI] Attempt %d/%d - Connecting to '%s'...\n", attempt, MAX_RETRIES, DRONE_SSID);
    
    WiFi.begin(DRONE_SSID, DRONE_PASS);
    
    // استنى الاتصال لمدة 10 ثواني كحد أقصى
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[OK] Connected to '%s'\n", DRONE_SSID);
      Serial.printf("[OK] Camera IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    
    Serial.printf("[FAIL] Attempt %d failed. ", attempt);
    if (attempt < MAX_RETRIES) {
      Serial.printf("Retrying in %d seconds...\n", RETRY_DELAY / 1000);
      WiFi.disconnect();
      delay(RETRY_DELAY);
    } else {
      Serial.println("All attempts exhausted!");
    }
  }
  
  Serial.println("[ERROR] Could not connect to drone network!");
  Serial.printf("[INFO] Make sure drone AP '%s' is running.\n", DRONE_SSID);
  return false;
}

// ===== HTTP: Status Endpoint =====
static esp_err_t status_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  char resp[256];
  snprintf(resp, sizeof(resp), 
    "{\"status\":\"online\",\"ip\":\"%s\",\"psram\":%s,\"uptime\":%lu,\"rssi\":%d}",
    WiFi.localIP().toString().c_str(),
    psramFound() ? "true" : "false",
    millis() / 1000,
    WiFi.RSSI()
  );
  
  return httpd_resp_send(req, resp, strlen(resp));
}

// ===== HTTP: Capture Single Frame =====
static esp_err_t capture_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_set_type(req, "application/json");
    const char* e = "{\"error\":\"capture failed\"}";
    return httpd_resp_send(req, e, strlen(e));
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  
  return res;
}

// ===== HTTP: MJPEG Stream =====
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  char part_buf[64];
  
  httpd_resp_set_type(req, STREAM_CT);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

  Serial.println("[STREAM] Client connected");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[STREAM] Frame grab failed");
      delay(100);
      continue;
    }

    size_t hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
    
    esp_err_t res = httpd_resp_send_chunk(req, STREAM_BOUND, strlen(STREAM_BOUND));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
    
    if (res != ESP_OK) {
      Serial.println("[STREAM] Client disconnected");
      break;
    }
    
    delay(33);  // ~30fps
  }
  
  return ESP_OK;
}

// ===== HTTP: Flash LED Control =====
static esp_err_t flash_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  
  char buf[32], val[4] = {0};
  int len = httpd_req_get_url_query_len(req) + 1;
  if (len > 1 && len < (int)sizeof(buf)) {
    httpd_req_get_url_query_str(req, buf, len);
    httpd_query_key_value(buf, "state", val, sizeof(val));
  }
  
  bool state = (atoi(val) == 1);
  digitalWrite(FLASH_LED, state ? HIGH : LOW);
  
  char resp[40];
  snprintf(resp, sizeof(resp), "{\"flash\":%s}", state ? "true" : "false");
  return httpd_resp_send(req, resp, strlen(resp));
}

// ===== HTTP: Set Resolution =====
static esp_err_t resolution_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  
  char buf[32], val[4] = {0};
  int len = httpd_req_get_url_query_len(req) + 1;
  if (len > 1 && len < (int)sizeof(buf)) {
    httpd_req_get_url_query_str(req, buf, len);
    httpd_query_key_value(buf, "val", val, sizeof(val));
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_framesize(s, (framesize_t)atoi(val));
  }
  
  return httpd_resp_send(req, "{\"success\":true}", 16);
}

// ===== Start HTTP Servers =====
void startServers() {
  // Main server on port 80
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 8;
  config.stack_size = 8192;

  if (httpd_start(&httpd_main, &config) == ESP_OK) {
    httpd_uri_t uris[] = {
      {"/",          HTTP_GET, status_handler,     NULL},
      {"/status",    HTTP_GET, status_handler,     NULL},
      {"/capture",   HTTP_GET, capture_handler,    NULL},
      {"/flash",     HTTP_GET, flash_handler,      NULL},
      {"/resolution",HTTP_GET, resolution_handler, NULL},
    };
    for (auto &u : uris) httpd_register_uri_handler(httpd_main, &u);
    Serial.println("[OK] HTTP server on port 80");
  }

  // Stream server on port 81
  httpd_config_t sc = HTTPD_DEFAULT_CONFIG();
  sc.server_port = 81;
  sc.ctrl_port = 32769;
  if (httpd_start(&httpd_stream, &sc) == ESP_OK) {
    httpd_uri_t u = {"/stream", HTTP_GET, stream_handler, NULL};
    httpd_register_uri_handler(httpd_stream, &u);
    Serial.println("[OK] Stream server on port 81");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=============================");
  Serial.println("  Drone AI Camera v2.0");
  Serial.println("  Mode: Station (SkyHund)");
  Serial.println("=============================");

  // Initialize flash LED
  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  // Initialize camera
  if (!initCamera()) {
    Serial.println("[FATAL] Camera failed! Restarting...");
    delay(3000);
    ESP.restart();
  }

  // Connect to drone's WiFi network (with retries)
  if (!connectToNetwork()) {
    Serial.println("[FATAL] WiFi connection failed! Restarting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }

  // Start HTTP servers
  startServers();

  Serial.println("\n=============================");
  Serial.println("  System Ready!");
  Serial.printf("  WiFi: %s\n", DRONE_SSID);
  Serial.printf("  Camera IP:  %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Stream:  http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
  Serial.println("=============================\n");
}

// ===== LOOP =====
void loop() {
  // Check WiFi connection & reconnect if lost
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARNING] WiFi disconnected! Attempting reconnect...");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(DRONE_SSID, DRONE_PASS);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n[OK] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("\n[ERROR] Reconnect failed. Will retry...");
    }
  }

  // Heartbeat
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 30000) {
    lastPrint = millis();
    Serial.printf("[HEARTBEAT] Uptime: %lus | IP: %s | RSSI: %ddBm\n",
      millis() / 1000,
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI()
    );
  }
  delay(1000);
}
