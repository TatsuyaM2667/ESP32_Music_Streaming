# ESP32_Music_Streaming

このコードでは、ESP32 を使って **SD カード内の mp3 ファイルを HTTP 経由で配信する仕組み**を実装した。  
最小構成ながら、ブラウザからアクセスするだけで音楽を再生できるようになる仕組みである。

---

## 1. WiFi 接続の準備

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

ESP32 をネットワークに接続するための SSID とパスワードを設定する。  
この接続が成功すると、ESP32 はローカルネットワーク内で HTTP サーバーとして動作できるようになる。

---

## 2. Web サーバーの起動

```cpp
WebServer server(80);
```

ESP32 上に **ポート 80 の HTTP サーバー**を立ち上げます。  
ブラウザから `http://ESP32のIPアドレス/ファイル名.mp3` のようにアクセスすると、このサーバーがリクエストを受け取る仕組み。

---

## 3. SD カードの SPI 設定

```cpp
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
```

SD カードモジュールを SPI 接続で使用するためのピン定義。  
AE-MICRO-SD-DIP のような SPI タイプの SD モジュールでは、この設定が正しくないとマウントに失敗する。

---

## 4. mp3 ファイルを配信する処理

音楽配信の中心となるのが `handleFileStream()` である。

### 4-1. リクエストされたパスを取得

```cpp
String path = server.uri();
```

ブラウザがアクセスした URL（例: `/music/song.mp3`）を取得する。

---

### 4-2. SD カード上にファイルが存在するか確認

```cpp
if (!SD.exists(path)) {
  server.send(404, "text/plain", "Not found");
  return;
}
```

ファイルが存在しない場合は 404 を返す。

---

### 4-3. ファイルを開く

```cpp
File file = SD.open(path, FILE_READ);
```

読み込み専用で mp3 ファイルを開く。

---

### 4-4. HTTP ヘッダを送信

```cpp
size_t fileSize = file.size();
server.setContentLength(fileSize);
server.sendHeader("Content-Type", "audio/mpeg");
server.sendHeader("Connection", "close");
server.send(200);
```

ここが重要なポイント。

- **Content-Length**  
  ブラウザが「どれくらいのデータが来るか」を理解できる  
- **Content-Type: audio/mpeg**  
  mp3 として扱われる  
- **Connection: close**  
  転送後に接続を閉じる

これにより、ブラウザ側が音楽ファイルとして正しく処理できる。

---

### 4-5. バイナリデータを送信

```cpp
uint8_t buf[1024];
while (file.available()) {
  size_t len = file.read(buf, sizeof(buf));
  server.client().write(buf, len);
}
```

SD カードから 1024 バイトずつ読み出し、  
そのまま HTTP クライアントへ送信。

---

## 5. setup() の処理

### 5-1. WiFi 接続

```cpp
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) { ... }
```

接続が完了するまで待機し、IP アドレスをシリアルに表示。

---

### 5-2. SD カードの初期化

```cpp
SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
SD.begin(SD_CS);
```

SPI を開始し、SD カードをマウント。  
失敗した場合は早期 return して処理を止める。

---

### 5-3. SD カードの中身を一覧表示

```cpp
listSD("/", 3);
```

デバッグ用に SD カードのファイル構造を表示。  
「ちゃんと mp3 が入っているか」を確認できる。

---

### 5-4. Web サーバーのルーティング設定

```cpp
server.onNotFound(handleFileStream);
```

どんな URL にアクセスされても `handleFileStream()` が呼ばれるようにしている。  
これにより、`/song.mp3` のようなパスをそのまま SD カードのファイルパスとして扱うことができる。

---

## 6. loop() の処理

```cpp
server.handleClient();
```

HTTP リクエストを処理するためのループ。  
ESP32 の WebServer ライブラリを使う場合は必須の処理である。

---

