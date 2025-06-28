#include "Arduino.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <WiFi.h>

const char* SSID ="ESP32-CAM" ;
const char* password ="12345678" ;

// define the frequency of the motor
const int freq = 830; 

httpd_handle_t camera_httpd = NULL;
esp_err_t stream_handler(httpd_req_t *req);

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define flashPin 4
#define CAMERA_MODEL_AI_THINKER

void startCameraServer();

void setup() {
  pinMode(flashPin, OUTPUT);
  digitalWrite(flashPin, LOW); // Ensure flash is off

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  camera_config_t Camera;
  Camera.pin_pwdn = PWDN_GPIO_NUM;
  Camera.pin_reset = RESET_GPIO_NUM;
  Camera.pin_xclk = XCLK_GPIO_NUM;
  Camera.pin_sscb_sda = SIOD_GPIO_NUM;
  Camera.pin_sscb_scl = SIOC_GPIO_NUM;
  Camera.pin_d7 = Y9_GPIO_NUM;
  Camera.pin_d6 = Y8_GPIO_NUM;
  Camera.pin_d5 = Y7_GPIO_NUM;
  Camera.pin_d4 = Y6_GPIO_NUM;
  Camera.pin_d3 = Y5_GPIO_NUM;
  Camera.pin_d2 = Y4_GPIO_NUM;
  Camera.pin_d1 = Y3_GPIO_NUM;
  Camera.pin_d0 = Y2_GPIO_NUM;
  Camera.pin_vsync = VSYNC_GPIO_NUM;
  Camera.pin_href = HREF_GPIO_NUM;
  Camera.pin_pclk = PCLK_GPIO_NUM;
  Camera.xclk_freq_hz = 20000000; // Lower clock speed
  Camera.pixel_format = PIXFORMAT_JPEG;

  Serial.println("Camera config success");
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());

  if (psramFound()) {
    Serial.println("Found PSRAM");
    Camera.frame_size = FRAMESIZE_VGA; // Lower resolution for testing
    Camera.jpeg_quality = 12;
    Camera.fb_count = 1; // Single buffer
  } else {
    Serial.println("PSRAM not found");
    Camera.frame_size = FRAMESIZE_VGA;
    Camera.jpeg_quality = 12;
    Camera.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&Camera);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  delay(100); // Allow camera to stabilize

  // Test single frame capture
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Single frame capture failed");
    return;
  } else {
    Serial.printf("Single frame captured: %u bytes\n", fb->len);
    esp_camera_fb_return(fb);
  }

  // Connect to Wi-Fi
  if (!WiFi.softAP(SSID, password)) {
    Serial.println("WiFi AP failed to start");
    return;
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Start streaming web server
  startCameraServer();

  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(myIP);
}

void loop() {
  // Empty loop
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 4096; // Increase stack size

  httpd_uri_t index_uri = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = stream_handler,
                           .user_ctx = NULL};

  httpd_uri_t stream_uri = {.uri = "/stream",
                            .method = HTTP_GET,
                            .handler = stream_handler,
                            .user_ctx = NULL};

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    Serial.println("Now registering URIs...");
    esp_err_t url_res = httpd_register_uri_handler(camera_httpd, &index_uri);
    Serial.printf(url_res == ESP_OK ? "URI '/' registered successfully\n"
                                    : "Failed to register URI '/'\n");
    url_res = httpd_register_uri_handler(camera_httpd, &stream_uri);
    Serial.printf(url_res == ESP_OK ? "URI '/stream' registered successfully\n"
                                    : "Failed to register URI '/stream'\n");
  } else {
    Serial.println("Error starting server!");
  }
}

esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      Serial.printf("Frame captured: %u bytes, format: %d\n", fb->len,
                    fb->format);
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          esp_camera_fb_return(fb);
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY,
                                  strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    delay(10); // Reduce CPU load
  }
  return res;
}