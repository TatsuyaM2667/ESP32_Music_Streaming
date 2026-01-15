#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// ====== WiFi設定 ======
const char* ssid = "WifiのSSID";
const char* password = "Password";

// ====== Webサーバー ======
WebServer server(80);

// ====== SDカードのSPIピン ======
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

// ====== mp3ファイル配信 ======
void handleFileStream() {
  String path = server.uri();
  path.trim();
  if (path.endsWith("/")) path.remove(path.length() - 1);

  Serial.print("Request path: ");
  Serial.println(path);

  if (!SD.exists(path)) {
    Serial.println("File not found");
    server.send(404, "text/plain", "Not found");
    return;
  }

  File file = [SD.open](http://SD.open)(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Failed to open");
    return;
  }

  // --- Content-Length を明示する ---
  size_t fileSize = file.size();
  server.setContentLength(fileSize);
  server.sendHeader("Content-Type", "audio/mpeg");
  server.sendHeader("Connection", "close");
  server.send(200);

  // --- バイナリ送信 ---
  uint8_t buf[1024];
  while (file.available()) {
    size_t len = [file.read](http://file.read)(buf, sizeof(buf));
    server.client().write(buf, len);
  }

  file.close();
  Serial.println("Stream finished");
}

void listSD(const char * dirname, uint8_t levels) {
  File root = [SD.open](http://SD.open)(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println([file.name](http://file.name)());
      if (levels) {
        listSD([file.name](http://file.name)(), levels - 1);
      }
    } else {
      Serial.print("FILE: ");
      Serial.print([file.name](http://file.name)());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// ====== セットアップ ======
void setup() {
  Serial.begin(115200);
  delay(500);

  // --- WiFi ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // 必要に応じて省電力をオフにする
  // WiFi.setSleep(false);

  // --- SDカード ---
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD mount failed");
    return;
  }
  Serial.println("SD mount success");

  // SDカード内のファイル一覧を表示
  listSD("/", 3);

  // --- Webサーバー ---
  server.onNotFound(handleFileStream);
  server.begin();
  Serial.println("Web server started");
}

// ====== メインループ ======
void loop() {
  server.handleClient();
}
