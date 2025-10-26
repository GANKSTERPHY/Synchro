#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#define SD_CS 5
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASS";

IPAddress local_IP(192, 168, 1, 177);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer server(80);
WiFiClient client;

// MP3 playback objects
AudioGeneratorMP3 mp3;
AudioFileSourceSD source;
AudioOutputI2S out;

// Store last game result
String lastResultJSON = "{}";

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed!");
    while (1);
  }
  Serial.println("SD ready.");

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started.");
}

// -------------------- MAIN LOOP --------------------
void loop() {
  client = server.available();
  if (!client) return;

  String req = client.readStringUntil('\n');
  req.trim();

  if (req.startsWith("POST /upload"){
    handleUpload(client);
  } else if (req.startsWith("GET /handshake")) {
    handleHandshake(client);
  } else if (req.startsWith("GET /getresult")) {
    handleGetResult(client);
  } else if (req.startsWith("GET /play")) {
    String songName = req.substring(req.indexOf("?song=") + 6);
    playMP3(songName);
    client.println("HTTP/1.1 200 OK");
    client.println("Connection: close");
    client.println();
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  }

  // Update MP3 playback without blocking
  if(mp3.isRunning()){
      mp3.loop();
  }

  client.stop();
}

// -------------------- HANDSHAKE --------------------
void handleHandshake(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();

  client.print("{\"status\":\"ESP32 ready\",\"songs\":[");

  File root = SD.open("/");
  bool first = true;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      if (!first) client.print(",");
      client.print("\"");
      client.print(entry.name());
      client.print("\"");
      first = false;
    }
    entry.close();
  }
  root.close();

  client.println("]}");
}

// -------------------- UPLOAD HANDLER --------------------
void handleUpload(WiFiClient& client) {
  String filename = "upload.bin";  // fallback
  int contentLength = 0;

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
    if (line.startsWith("Filename:")) {
      filename = line.substring(9);
      filename.trim();
      if (!filename.startsWith("/")) filename = "/" + filename;
    }
    if (line == "") break;
  }

  int dotIndex = filename.lastIndexOf('.');
  String baseName = filename.substring(1, dotIndex);
  String extension = filename.substring(dotIndex);

  String folderPath = "/" + baseName;
  if (!SD.exists(folderPath)) SD.mkdir(folderPath);

  String fullPath = folderPath + "/" + baseName + extension;
  if (SD.exists(fullPath)) SD.remove(fullPath);

  File f = SD.open(fullPath.c_str(), FILE_WRITE);
  if (!f) {
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println();
    return;
  }

  for (int i = 0; i < contentLength; i++) {
    while (!client.available()) delay(1);
    f.write(client.read());
  }
  f.close();

  client.println("HTTP/1.1 200 OK");
  client.println();
  client.println("Upload complete: " + filename);
}

// -------------------- GAME RESULT --------------------
void setGameResult(const String& songName, const String& artist, int miss, int ok, int great, int perfect) {
  lastResultJSON = "{\"songName\":\"" + songName + "\",\"artist\":\"" + artist + "\",\"result\":{\"miss\":" + String(miss) + ",\"ok\":" + String(ok) + ",\"great\":" + String(great) + ",\"perfect\":" + String(perfect) + "}}";
}

void handleGetResult(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.print(lastResultJSON);
  client.flush();
  //Reset game here
}

// -------------------- PLAY MP3 --------------------
void playMP3(const String& songName) {
    source.open(path.c_str());
    out.begin();
    mp3.begin(&source, &out);
}

