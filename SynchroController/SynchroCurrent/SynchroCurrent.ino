#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <esp_heap_caps.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
WiFiServer server(80);

// ----- CONFIG -----
const int button1 = 25;  // lane 0
const int button2 = 26;  // lane 1
const int button3 = 27;  // lane 2
const int button4 = 14;  // lane 3
const char* ssid = "Guh";
const char* password = "Nuhuhwhysolong";

// SD on HSPI
#define SD_SCK   32
#define SD_MISO  19
#define SD_MOSI  13
#define SD_CS    4
SPIClass spiSD(HSPI);

// screen/game
#define SCREEN_W 240
#define SCREEN_H 320
#define NUM_LANES 4
#define UPDATE_INTERVAL 10
#define SCROLL_WINDOW 2000
#define SCROLL_OFFSET 100
#define FLICKER_INTERVAL 1000
#define WIFI_CHECK_INTERVAL 25000
#define HIT_WINDOW 400
#define TILE_EXPIRE_TIME 1500
#define MAX_TILES 200
#define COUNTDOWN_DURATION 3000
#define GAME_END_WAIT 200
// -------------------

int state = 0;  // 0=menu, 1=countdown, 2=playing, 3=results
bool buttonPressed[NUM_LANES] = {false, false, false, false};
int buttonPins[NUM_LANES] = {button1, button2, button3, button4};
float msToPixel = (float)SCREEN_W / (SCROLL_WINDOW + SCROLL_OFFSET);
unsigned long startTime = 0;
unsigned long countdownStartTime = 0;
unsigned long lastUpdate = 0;
int laneWidth = SCREEN_W / NUM_LANES;
int flickerMain = 0;
bool sdCardAvailable = false;
bool gameEnded = false;
unsigned long gameEndTime = 0;

// current song
String currentSongName = "";
String currentArtist = "";
int currentDifficulty = 0;
String currentFolderName = "";

struct Tile {
  int press;
  int lane;
  bool active;
  bool hit;
  unsigned long hitTime;
  int hitY;
};

Tile tiles[MAX_TILES];
int tileCount = 0;

// last result
int miss = 0;
int ok = 0;
int great = 0;
int perfect = 0;
String lastResultJSON = "{}";

///////////////////// Utility Functions /////////////////////

uint16_t fixColor(uint16_t c) {
  switch (c) {
    case TFT_BLACK: return TFT_WHITE;
    case TFT_WHITE: return TFT_BLACK;
    case TFT_RED: return TFT_CYAN;
    case TFT_CYAN: return TFT_RED;
    case TFT_GREEN: return TFT_MAGENTA;
    case TFT_MAGENTA: return TFT_GREEN;
    default: return c;
  }
}

void initializeTiles() {
  tileCount = 0;
  for (int i = 0; i < MAX_TILES; i++) {
    tiles[i].active = false;
    tiles[i].hit = false;
  }
}

bool loadTilesFromJSON(const String& folderName) {
  if (!sdCardAvailable) return false;

  String jsonPath = "/" + folderName + "/" + folderName + ".json";
  File file = SD.open(jsonPath, FILE_READ);
  if (!file) return false;

  String jsonContent = "";
  while (file.available()) jsonContent += (char)file.read();
  file.close();

  int tilesStart = jsonContent.indexOf("\"tiles\"");
  if (tilesStart == -1) return false;
  int arrayStart = jsonContent.indexOf('[', tilesStart);
  int arrayEnd = jsonContent.lastIndexOf(']');
  if (arrayStart == -1 || arrayEnd == -1) return false;

  int nameStart = jsonContent.indexOf("\"songName\"");
  if (nameStart != -1) {
    int colonPos = jsonContent.indexOf(':', nameStart);
    int q1 = jsonContent.indexOf('"', colonPos);
    int q2 = jsonContent.indexOf('"', q1 + 1);
    if (q1 != -1 && q2 != -1) currentSongName = jsonContent.substring(q1 + 1, q2);
  }

  int artistStart = jsonContent.indexOf("\"artist\"");
  if (artistStart != -1) {
    int colonPos = jsonContent.indexOf(':', artistStart);
    int q1 = jsonContent.indexOf('"', colonPos);
    int q2 = jsonContent.indexOf('"', q1 + 1);
    if (q1 != -1 && q2 != -1) currentArtist = jsonContent.substring(q1 + 1, q2);
  }

  int diffStart = jsonContent.indexOf("\"difficulty\"");
  if (diffStart != -1) {
    int colonPos = jsonContent.indexOf(':', diffStart);
    int commaPos = jsonContent.indexOf(',', colonPos);
    if (commaPos == -1) commaPos = jsonContent.indexOf('}', colonPos);
    String diffStr = jsonContent.substring(colonPos + 1, commaPos);
    diffStr.trim();
    currentDifficulty = diffStr.toInt();
  }

  String tilesStr = jsonContent.substring(arrayStart + 1, arrayEnd);
  int tileIndex = 0;
  int pos = 0;

  while (pos < tilesStr.length() && tileIndex < MAX_TILES) {
    int objStart = tilesStr.indexOf('{', pos);
    if (objStart == -1) break;
    int objEnd = tilesStr.indexOf('}', objStart);
    if (objEnd == -1) break;

    String t = tilesStr.substring(objStart, objEnd + 1);

    int pressPos = t.indexOf("\"press\"");
    if (pressPos != -1) {
      int colonPos = t.indexOf(':', pressPos);
      int commaPos = t.indexOf(',', colonPos);
      if (commaPos == -1) commaPos = t.indexOf('}', colonPos);
      String s = t.substring(colonPos + 1, commaPos);
      s.trim();
      tiles[tileIndex].press = s.toInt();
    }

    int slotPos = t.indexOf("\"slot\"");
    if (slotPos != -1) {
      int colonPos = t.indexOf(':', slotPos);
      int commaPos = t.indexOf(',', colonPos);
      if (commaPos == -1) commaPos = t.indexOf('}', colonPos);
      String s = t.substring(colonPos + 1, commaPos);
      s.trim();
      tiles[tileIndex].lane = s.toInt() - 1;  // json 1-4 -> 0-3
    }

    tiles[tileIndex].active = true;
    tiles[tileIndex].hit = false;
    tileIndex++;
    pos = objEnd + 1;
  }

  tileCount = tileIndex;
  return tileCount > 0;
}

void cleanupExpiredTiles(int currentTime) {
  for (int i = 0; i < tileCount; i++) {
    if (tiles[i].hit && (millis() - tiles[i].hitTime) > 500) {
      tiles[i].hit = false;
      tiles[i].active = false;
    }
    if (tiles[i].active && !tiles[i].hit && tiles[i].press < currentTime - TILE_EXPIRE_TIME) {
      tiles[i].active = false;
      miss += 1;
    }
  }
}

bool checkGameEnd(int currentTime) {
  if (tileCount == 0) return false;
  bool allInactive = true;
  int lastTileTime = 0;
  for (int i = 0; i < tileCount; i++) {
    if (tiles[i].active || tiles[i].hit) allInactive = false;
    if (tiles[i].press > lastTileTime) lastTileTime = tiles[i].press;
  }
  if (!allInactive) return false;
  if (lastTileTime > 0) {
    if (currentTime >= lastTileTime + TILE_EXPIRE_TIME + GAME_END_WAIT) return true;
  }
  return false;
}

int timeToY(int tileTime, int currentTime) {
  int delta = tileTime - currentTime;
  return SCREEN_H - (int)((delta + SCROLL_OFFSET) * msToPixel);
}

///////////////////// Drawing Functions /////////////////////

void drawTile(TFT_eSprite* gfx, int y, int lane, bool isHit) {
  int laneWidth = SCREEN_W / NUM_LANES;
  int x = lane * laneWidth;
  uint16_t tileColor;
  if (isHit) tileColor = fixColor(TFT_WHITE);
  else {
    switch (lane) {
      case 0: tileColor = fixColor(TFT_RED); break;
      case 1: tileColor = fixColor(TFT_GREEN); break;
      case 2: tileColor = fixColor(TFT_CYAN); break;
      case 3: tileColor = fixColor(TFT_MAGENTA); break;
      default: tileColor = tft.color565(0, 0, 0);
    }
  }
  gfx->fillRect(x + 5, y - 8, laneWidth - 10, 16, tileColor);
}

void drawGameFrame(int currentTime) {
  spr.fillSprite(fixColor(TFT_BLACK));
  for (int i = 1; i < NUM_LANES; i++) spr.drawLine(i * laneWidth, 0, i * laneWidth, SCREEN_H, TFT_DARKGREY);

  for (int i = 0; i < MAX_TILES; i++) {
    if (tiles[i].active || tiles[i].hit) {
      int y = tiles[i].hit ? tiles[i].hitY : SCREEN_H - (int)((tiles[i].press - currentTime + SCROLL_OFFSET) * msToPixel);
      if (y >= 0 && y <= SCREEN_H) drawTile(&spr, y, tiles[i].lane, tiles[i].hit);
    }
  }

  int hitLineY = SCREEN_H - 30;
  spr.fillRect(0, hitLineY, SCREEN_W, 5, TFT_BLACK);
  spr.pushSprite(0, 0);
}

void drawResultsScreen() {
  spr.fillSprite(fixColor(TFT_BLACK));
  spr.setTextSize(3);
  spr.setTextColor(fixColor(TFT_YELLOW));
  spr.setCursor(30, 20); spr.print("Results!");

  spr.setTextSize(1);
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(10, 60); spr.print(currentSongName);
  spr.setCursor(10, 75); spr.setTextColor(fixColor(TFT_CYAN)); spr.print(currentArtist);

  int total = perfect + great + ok + miss;
  int hits = perfect + great + ok;
  float accuracy = total > 0 ? (float)hits / total * 100.0 : 0;

  spr.setTextSize(2);
  int yPos = 110;
  spr.setTextColor(fixColor(TFT_GREEN));   spr.setCursor(20, yPos); spr.print("Perfect: "); spr.print(perfect);
  yPos += 30; spr.setTextColor(fixColor(TFT_CYAN));    spr.setCursor(20, yPos); spr.print("Great: ");   spr.print(great);
  yPos += 30; spr.setTextColor(fixColor(TFT_YELLOW));  spr.setCursor(20, yPos); spr.print("OK: ");      spr.print(ok);
  yPos += 30; spr.setTextColor(fixColor(TFT_RED));     spr.setCursor(20, yPos); spr.print("Miss: ");    spr.print(miss);

  yPos += 40;
  spr.setTextSize(2);
  spr.setTextColor(fixColor(TFT_MAGENTA));
  spr.setCursor(20, yPos); spr.print("Accuracy: "); spr.print(accuracy, 1); spr.print("%");

  spr.setTextSize(1);
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(30, 295); spr.print("Press any button");
  spr.pushSprite(0, 0);
}

void drawMainScreen() {
  spr.fillSprite(fixColor(TFT_BLACK));
  spr.setTextSize(4);
  spr.setTextColor(fixColor(TFT_MAGENTA));
  spr.setCursor(42, 53); spr.print("Synchro");
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(40, 50); spr.print("Synchro");

  if (flickerMain) {
    spr.setTextSize(2);
    spr.setTextColor(fixColor(TFT_WHITE));
    spr.setCursor(22, 200); spr.print("Waiting to Start");
  }
  spr.pushSprite(0, 0);
  flickerMain = !flickerMain;
}

void drawCountdownScreen(int secondsLeft) {
  spr.fillSprite(fixColor(TFT_BLACK));
  spr.setTextSize(2);
  spr.setTextColor(fixColor(TFT_CYAN));
  spr.setCursor(10, 40); spr.print("Starting:");
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(10, 70); spr.print(currentSongName);
  spr.setTextSize(1);
  spr.setTextColor(fixColor(TFT_YELLOW));
  spr.setCursor(10, 100); spr.print(currentArtist);

  spr.setTextSize(8);
  spr.setTextColor(fixColor(TFT_GREEN));
  String countText = String(secondsLeft);
  int textWidth = countText.length() * 48;
  spr.setCursor((SCREEN_W - textWidth) / 2, 150);
  spr.print(countText);
  spr.pushSprite(0, 0);
}

///////////////////// SD Card Functions /////////////////////

bool initSDCard() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  bool sdOk = false;
  const int frequencies[] = {10000000, 4000000};
  for (int i = 0; i < 2 && !sdOk; i++) {
    if (SD.begin(SD_CS, spiSD, frequencies[i], "/sd", 5, false)) {
      delay(200);
      uint64_t totalBytes = SD.totalBytes();
      if (totalBytes > 0) { sdOk = true; break; }
      else { SD.end(); }
    }
    delay(500);
  }

  if (!sdOk) {
    SD.end(); delay(500);
    if (SD.begin(SD_CS, SPI, 1000000)) { SD.end(); }
  }
  if (!sdOk) return false;

  uint8_t cardType = SD.cardType(); (void)cardType;
  uint64_t cardSize = SD.cardSize() / (1024 * 1024); (void)cardSize;
  return true;
}

///////////////////// Network Functions /////////////////////

void handleCORSPreflight(WiFiClient& client) {
  client.println("HTTP/1.1 204 No Content");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type, Content-Length, Filename");
  client.println("Connection: close");
  client.println();
}

void handleHandshake(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();

  client.print("{\"status\":\"Arduino ready\",\"sdAvailable\":");
  client.print(sdCardAvailable ? "true" : "false");
  client.print(",\"gameState\":");
  client.print(state);
  client.print(",\"countdownDuration\":");
  client.print(COUNTDOWN_DURATION);
  client.print(",\"songs\":[");

  if (sdCardAvailable) {
    File root = SD.open("/");
    bool first = true;
    while (true) {
      File entry = root.openNextFile();
      if (!entry) break;
      if (entry.isDirectory()) {
        String folderName = String(entry.name());
        if (folderName != "System Volume Information" && folderName != ".Trash" && folderName != ".spotlight" && !folderName.startsWith(".")) {
          if (!first) client.print(",");
          client.print("\""); client.print(folderName); client.print("\"");
          first = false;
        }
      }
      entry.close();
    }
    root.close();
  }
  client.println("]}");
}

void handlePlay(WiFiClient& client, String songFolder) {
  songFolder.trim();
  if (songFolder.length() == 0) {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Missing song parameter\"}");
    client.flush();
    return;
  }

  currentFolderName = songFolder;
  initializeTiles();

  if (!loadTilesFromJSON(songFolder)) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Failed to load song data\"}");
    client.flush();
    return;
  }

  state = 1;
  countdownStartTime = millis();
  resetScore();
  gameEnded = false;

  unsigned long responseTime = millis();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.print("{\"status\":\"Game starting\",\"song\":\"");
  client.print(currentSongName);
  client.print("\",\"artist\":\"");
  client.print(currentArtist);
  client.print("\",\"difficulty\":");
  client.print(currentDifficulty);
  client.print(",\"countdownStartTime\":");
  client.print(countdownStartTime);
  client.print(",\"serverTime\":");
  client.print(responseTime);
  client.print(",\"countdownDuration\":");
  client.print(COUNTDOWN_DURATION);
  client.println("}");
  client.flush();
}

void handleGetResult(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();

  int total = perfect + great + ok + miss;
  int hits = perfect + great + ok;
  float accuracy = total > 0 ? (float)hits / total * 100.0 : 0;

  client.print("{\"songName\":\"");
  client.print(currentSongName);
  client.print("\",\"artist\":\"");
  client.print(currentArtist);
  client.print("\",\"difficulty\":");
  client.print(currentDifficulty);
  client.print(",\"gameEnded\":");
  client.print(gameEnded ? "true" : "false");
  client.print(",\"result\":{\"miss\":");
  client.print(miss);
  client.print(",\"ok\":");
  client.print(ok);
  client.print(",\"great\":");
  client.print(great);
  client.print(",\"perfect\":");
  client.print(perfect);
  client.print(",\"total\":");
  client.print(total);
  client.print(",\"accuracy\":");
  client.print(accuracy);
  client.println("}}");
  client.flush();

  gameEnded = true;
}

void handleUpload(WiFiClient& client) {
  if (!sdCardAvailable) {
    client.println("HTTP/1.1 503 Service Unavailable");
    client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"SD card not available\"}");
    client.flush();
    return;
  }

  String filename = "";
  int contentLength = 0;
  bool hasContentLength = false;
  bool hasFilename = false;

  String headerBuffer = "";
  unsigned long headerTimeout = millis();
  while (client.connected() && (millis() - headerTimeout < 5000)) {
    if (client.available()) {
      char c = client.read();
      headerBuffer += c;
      if (headerBuffer.endsWith("\r\n\r\n") || headerBuffer.endsWith("\n\n")) break;
    } else {
      delay(1);
    }
  }

  int lineStart = 0;
  int lineEnd = 0;
  while (lineEnd < headerBuffer.length()) {
    lineEnd = headerBuffer.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = headerBuffer.length();
    String line = headerBuffer.substring(lineStart, lineEnd);
    line.trim();
    if (line.length() > 0) {
      String lowerLine = line; lowerLine.toLowerCase();
      if (lowerLine.startsWith("content-length:")) {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
          String lengthStr = line.substring(colonPos + 1); lengthStr.trim();
          contentLength = lengthStr.toInt(); hasContentLength = true;
        }
      }
      if (lowerLine.startsWith("filename:")) {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
          filename = line.substring(colonPos + 1); filename.trim(); hasFilename = true;
        }
      }
    }
    lineStart = lineEnd + 1;
  }

  if (!hasFilename || filename.length() == 0) {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Filename header required");
    client.flush();
    return;
  }

  if (!hasContentLength || contentLength <= 0) {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Content-Length required");
    client.flush();
    return;
  }

  if (!filename.startsWith("/")) filename = "/" + filename;

  int dotIndex = filename.lastIndexOf('.');
  if (dotIndex == -1) {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: File must have extension");
    client.flush();
    return;
  }

  String baseName = filename.substring(1, dotIndex);
  String extension = filename.substring(dotIndex);

  baseName.replace(" ", "_");
  baseName.replace("-", "_");
  String sanitized = "";
  for (int i = 0; i < baseName.length(); i++) {
    char c = baseName.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') sanitized += c;
  }
  baseName = sanitized;

  String folderPath = "/" + baseName;
  if (!SD.exists(folderPath)) {
    bool folderCreated = false;
    for (int attempt = 1; attempt <= 5; attempt++) {
      if (SD.mkdir(folderPath)) { folderCreated = true; delay(100); break; }
      delay(200);
    }
    if (!folderCreated) {
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Connection: close");
      client.println();
      client.println("{\"error\":\"Failed to create folder\"}");
      client.flush();
      return;
    }
  }

  String fullPath = folderPath + "/" + baseName + extension;
  if (SD.exists(fullPath)) { SD.remove(fullPath); delay(100); }

  File f = SD.open(fullPath.c_str(), FILE_WRITE);
  if (!f) {
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Failed to create file on SD card");
    client.flush();
    return;
  }

  int bytesWritten = 0;
  unsigned long lastByteTime = millis();
  unsigned long lastFlush = millis();

  const int BUFFER_SIZE = 512;
  uint8_t buffer[BUFFER_SIZE];
  int bufferIndex = 0;

  while (bytesWritten < contentLength) {
    if (!client.connected()) { f.close(); SD.remove(fullPath); return; }
    if (client.available()) {
      byte b = client.read();
      buffer[bufferIndex++] = b;
      bytesWritten++;
      lastByteTime = millis();

      if (bufferIndex >= BUFFER_SIZE) {
        f.write(buffer, bufferIndex);
        bufferIndex = 0;
        if (millis() - lastFlush > 1000) { f.flush(); lastFlush = millis(); }
      }
    } else {
      if (millis() - lastByteTime > 30000) {
        if (bufferIndex > 0) f.write(buffer, bufferIndex);
        f.close(); SD.remove(fullPath);
        client.println("HTTP/1.1 408 Request Timeout");
        client.println("Access-Control-Allow-Origin: *");
        client.println("Connection: close");
        client.println();
        client.flush();
        return;
      }
      delay(1);
    }
  }

  if (bufferIndex > 0) f.write(buffer, bufferIndex);
  f.flush();  
  f.close();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.print("{\"success\":true,\"filename\":\"");
  client.print(filename);
  client.print("\",\"size\":");
  client.print(bytesWritten);
  client.println("}");
  client.flush();
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  client.setTimeout(600000);

  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 5000) { client.stop(); return; }
    delay(1);
  }

  if (client.available()) {
    String req = client.readStringUntil('\n');
    req.trim();

    if (req.indexOf("OPTIONS") >= 0) {
      handleCORSPreflight(client);
    } else if (req.indexOf("GET /handshake") >= 0 || req.indexOf("POST /handshake") >= 0) {
      while (client.available()) { String line = client.readStringUntil('\n'); if (line.length() <= 2) break; }
      handleHandshake(client);
    } else if (req.indexOf("GET /play") >= 0 || req.indexOf("POST /play") >= 0) {
      int songParamPos = req.indexOf("song=");
      String songFolder = "";
      if (songParamPos != -1) {
        int spacePos = req.indexOf(' ', songParamPos);
        int ampPos = req.indexOf('&', songParamPos);
        int endPos = spacePos;
        if (ampPos != -1 && (ampPos < endPos || endPos == -1)) endPos = ampPos;
        songFolder = req.substring(songParamPos + 5, endPos);
        songFolder.replace("%20", " ");
        songFolder.replace("+", " ");
      }
      while (client.available()) { String line = client.readStringUntil('\n'); if (line.length() <= 2) break; }
      handlePlay(client, songFolder);
    } else if (req.indexOf("GET /getresult") >= 0 || req.indexOf("POST /getresult") >= 0) {
      while (client.available()) { String line = client.readStringUntil('\n'); if (line.length() <= 2) break; }
      handleGetResult(client);
    } else if (req.indexOf("POST /upload") >= 0) {
      handleUpload(client);
    } else {
      client.println("HTTP/1.1 404 Not Found");
      client.println("Connection: close");
      client.println();
      client.flush();
    }
  }

  delay(10);
  client.stop();
}

void checkWiFiStatus() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if (now - lastCheck > WIFI_CHECK_INTERVAL) {
    lastCheck = now;
    Serial.print("WiFi: ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(" | IP: ");
    Serial.println(WiFi.localIP());
  }
}

///////////////////// Input Functions /////////////////////

void checkButtonPress(int lane, int currentTime) {
  bool currentButtonState = (digitalRead(buttonPins[lane]) == HIGH);
  bool buttonJustPressed = currentButtonState && !buttonPressed[lane];
  buttonPressed[lane] = currentButtonState;

  if (buttonJustPressed) {
    for (int i = 0; i < MAX_TILES; i++) {
      if (tiles[i].active && !tiles[i].hit && tiles[i].lane == lane) {
        int timeDiff = tiles[i].press - (currentTime + SCROLL_OFFSET);
        if (abs(timeDiff) <= HIT_WINDOW) {
          tiles[i].active = false;
          tiles[i].hit = true;
          tiles[i].hitY = SCREEN_H - (int)((tiles[i].press - currentTime + SCROLL_OFFSET + 100) * msToPixel);
          tiles[i].hitTime = millis();
          if (abs(timeDiff) < 50)      perfect += 1;
          else if (abs(timeDiff) < 150) great += 1;
          else                           ok += 1;
          return;
        }
      }
    }
    miss += 1;
  }
}

bool checkAnyButtonPressed() {
  for (int i = 0; i < NUM_LANES; i++) if (digitalRead(buttonPins[i]) == HIGH) return true;
  return false;
}

///////////////////// Result /////////////////////

void resetScore() { miss = ok = great = perfect = 0; }

void setGameResult(const String& songName, const String& artist, int difficulty, int missV, int okV, int greatV, int perfectV) {
  lastResultJSON = "{\"songName\":\"" + songName +
                   "\",\"artist\":\"" + artist +
                   "\",\"difficulty\":" + String(difficulty) +
                   ",\"result\":{\"miss\":" + String(missV) +
                   ",\"ok\":" + String(okV) +
                   ",\"great\":" + String(greatV) +
                   ",\"perfect\":" + String(perfectV) + "}}";
}

///////////////////// Setup /////////////////////

void initDisplay() {
  tft.init();
  tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(8);
  tft.setRotation(2);
  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    if (!spr.createSprite(SCREEN_W, SCREEN_H / 2)) { /* nope */ }
  }
}

void initWiFi() {
  // simple splash
  spr.fillSprite(fixColor(TFT_BLACK));
  spr.setTextSize(4);
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(50, 110); spr.print("WiFi...");
  spr.pushSprite(0, 0);

  WiFi.begin(ssid, password);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - t0 > 30000) {
      spr.fillSprite(fixColor(TFT_BLACK));
      spr.setTextSize(2);
      spr.setTextColor(fixColor(TFT_RED));
      spr.setCursor(20, 120); spr.print("WiFi Failed!");
      spr.pushSprite(0, 0);
      delay(1200);
      return;
    }
  }

  server.begin();
  if (!MDNS.begin("synchro")) { /* ignore */ }
}

void setup() {
  Serial.begin(9600);
  delay(2000);

  for (int i = 0; i < NUM_LANES; i++) pinMode(buttonPins[i], INPUT_PULLUP);

  initDisplay();
  sdCardAvailable = initSDCard();
  initWiFi();
  WiFi.setSleep(false);

  drawMainScreen();
  startTime = millis();
  lastUpdate = millis();
}

///////////////////// Main Loop /////////////////////

void updateMainMenu() {
  unsigned long now = millis();
  if (now - lastUpdate >= FLICKER_INTERVAL) {
    lastUpdate = now;
    drawMainScreen();
  }
}

void updateCountdown() {
  unsigned long now = millis();
  unsigned long elapsed = now - countdownStartTime;
  if (elapsed >= COUNTDOWN_DURATION) {
    state = 2;
    startTime = millis();
    lastUpdate = millis();
    return;
  }
  if (now - lastUpdate >= 100) {
    lastUpdate = now;
    int secondsLeft = 3 - (elapsed / 1000);
    if (secondsLeft < 0) secondsLeft = 0;
    drawCountdownScreen(secondsLeft);
  }
}

void updateGamePlay() {
  unsigned long now = millis();
  int currentTime = now - startTime;

  for (int i = 0; i < NUM_LANES; i++) {
    checkButtonPress(i, currentTime);
  }

  cleanupExpiredTiles(currentTime);

  if (!gameEnded && checkGameEnd(currentTime)) {
    gameEnded = true;
    gameEndTime = millis();
    state = 3;
    setGameResult(currentSongName, currentArtist, currentDifficulty, miss, ok, great, perfect);
    drawResultsScreen();
  }

  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    drawGameFrame(currentTime);
  }
}

void updateResults() {
  static bool wasPressed = false;
  bool isPressed = checkAnyButtonPressed();
  if (isPressed && !wasPressed) {
    state = 0;
    drawMainScreen();
    delay(200);
  }
  wasPressed = isPressed;
}

void loop() {
  handleClient();
  checkWiFiStatus();

  if (state == 0)      updateMainMenu();
  else if (state == 1) updateCountdown();
  else if (state == 2) updateGamePlay();
  else if (state == 3) updateResults();
}
