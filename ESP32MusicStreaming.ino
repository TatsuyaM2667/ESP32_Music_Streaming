#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <map>

// ====== WiFi設定 ======
const char* ssid     = "";
const char* password = "";

// ====== Webサーバー ======
WebServer server(80);

// ====== SDカードのSPIピン ======
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

#define ErrorLEDPin 14

// ====== グローバルキャッシュ ======
String musicListCache = "[]";                 // /list の結果をキャッシュ
std::map<String, String> lrcCache;           // LRC の内容をキャッシュ

// ====== URLデコード関数 ======
String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;

  while (i < len) {
    char c = input.charAt(i);
    if (c == '%') {
      if (i + 2 < len) {
        temp[2] = input.charAt(i + 1);
        temp[3] = input.charAt(i + 2);
        decoded += char(strtol(temp, NULL, 16));
        i += 3;
      }
    } else if (c == '+') {
      decoded += ' ';
      i++;
    } else {
      decoded += c;
      i++;
    }
  }
  return decoded;
}

// ====== CORS 共通関数 ======
void enableCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, ngrok-skip-browser-warning");
}

// ====== OPTIONS プリフライト ======
void handleOptions() {
  enableCORS();
  server.send(204);
}

// ====== LED ======
void ErrorLEDOn() {
  digitalWrite(ErrorLEDPin, HIGH);
  delay(500);
  digitalWrite(ErrorLEDPin, LOW);
}

// ====== music_index.json を読み込んでキャッシュ ======
void loadMusicIndex() {
  File file = SD.open("/music_index.json", FILE_READ);
  if (!file) {
    Serial.println("[loadMusicIndex] /music_index.json not found, fallback to []");
    musicListCache = "[]";
    return;
  }

  Serial.println("[loadMusicIndex] found /music_index.json");

  String json = "";
  while (file.available()) {
    json += (char)file.read();
  }
  file.close();

  if (json.length() == 0) {
    Serial.println("[loadMusicIndex] JSON empty, fallback to []");
    json = "[]";
  }

  musicListCache = json;

  Serial.println("[loadMusicIndex] JSON loaded. length = " + String(json.length()));
  // 必要なら中身も確認：
  // Serial.println(json);
}

// ====== /list: mp3一覧（music_index.json をそのまま返す） ======
void handleList() {
  enableCORS();

  File file = SD.open("/music_index.json", FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "music_index.json not found");
    ErrorLEDOn();
    return;
  }

  server.streamFile(file, "application/json");
  file.close();
}
// ====== /lyrics: mp3 パスから LRC 歌詞取得（キャッシュあり） ======
void handleLyrics() {
  enableCORS();

  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }

  // mp3 のパスを受け取る
  String rawPath = server.arg("file");
  String mp3Path = urlDecode(rawPath);

  Serial.println("===== Lyrics Request =====");
  Serial.println("MP3 Raw Path: " + rawPath);
  Serial.println("MP3 Decoded : " + mp3Path);

  // .mp3 → .lrc に変換
  String lrcPath = mp3Path;
  if (lrcPath.endsWith(".mp3")) {
    lrcPath.replace(".mp3", ".lrc");
  } else {
    server.send(400, "text/plain", "Invalid mp3 file");
    return;
  }

  Serial.println("LRC Path: " + lrcPath);

  // まずキャッシュを確認
  auto it = lrcCache.find(lrcPath);
  if (it != lrcCache.end()) {
    server.send(200, "text/plain; charset=utf-8", it->second);
    return;
  }

  // SD から LRC を読む
  if (!SD.exists(lrcPath)) {
    server.send(404, "text/plain", "LRC not found");
    ErrorLEDOn();
    return;
  }

  File file = SD.open(lrcPath, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Failed to open LRC");
    ErrorLEDOn();
    return;
  }

  String text = "";
  while (file.available()) {
    text += (char)file.read();
  }
  file.close();

  // キャッシュに保存
  lrcCache[lrcPath] = text;

  server.send(200, "text/plain; charset=utf-8", text);
}

// ====== mp3ファイル配信（大きめバッファ + 高速SPI） ======
void handleFileStream() {
  String rawPath = server.uri();
  String path = urlDecode(rawPath);

  path.trim();
  if (path.endsWith("/")) path.remove(path.length() - 1);

  Serial.println("===== File Request =====");
  Serial.println("Raw Path: " + rawPath);
  Serial.println("Decoded : " + path);

  if (!SD.exists(path)) {
    Serial.println("File not found");
    enableCORS();
    server.send(404, "text/plain", "Not found");
    ErrorLEDOn();
    return;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    enableCORS();
    server.send(500, "text/plain", "Failed to open");
    ErrorLEDOn();
    return;
  }

  size_t fileSize = file.size();

  enableCORS();
  server.setContentLength(fileSize);
  server.sendHeader("Content-Type", "audio/mpeg");
  server.sendHeader("Connection", "close");
  server.send(200);

  uint8_t buf[4096];  // 4096 バイトバッファ
  while (file.available()) {
    size_t len = file.read(buf, sizeof(buf));
    server.client().write(buf, len);
  }

  file.close();
  Serial.println("Stream finished");
}

// ====== SDカードの内容表示（デバッグ用） ======
void listSD(const char * dirname, uint8_t levels) {
  File root = SD.open(dirname);
  if (!root || !root.isDirectory()) return;

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
      if (levels) listSD(file.name(), levels - 1);
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// ====== セットアップ ======
void setup() {
  pinMode(ErrorLEDPin, OUTPUT);
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

  // --- SDカード（高速SPI） ---
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SPI.setFrequency(40000000);  // 40MHz に設定して高速化

  if (!SD.begin(SD_CS)) {
    Serial.println("SD mount failed");
    ErrorLEDOn();
    return;
  }
  Serial.println("SD mount success");
  listSD("/", 3);

  // ★ 起動時に music_index.json を読み込む
  //loadMusicIndex();

  // --- Webサーバー ---
  server.on("/list",    HTTP_GET,     handleList);
  server.on("/list",    HTTP_OPTIONS, handleOptions);

  server.on("/lyrics",  HTTP_GET,     handleLyrics);
  server.on("/lyrics",  HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleFileStream);

  server.begin();
  Serial.println("Web server started");
}

// ====== メインループ ======
void loop() {
  server.handleClient();
}
