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
const int button1 = 25;  // Lane 0
const int button2 = 26;  // Lane 1
const int button3 = 27;  // Lane 2
const int button4 = 14;  // Lane 3
const char* ssid = "Guh";
const char* password = "Nuhuhwhysolong";

// SD Card on HSPI (separate bus from TFT)
#define SD_SCK   32
#define SD_MISO  19
#define SD_MOSI  13
#define SD_CS    4
SPIClass spiSD(HSPI);

IPAddress local_IP(192, 168, 1, 177);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

#define SCREEN_W 240
#define SCREEN_H 320
#define NUM_LANES 4
#define UPDATE_INTERVAL 10
#define SCROLL_WINDOW 2000
#define SCROLL_OFFSET 100
#define FLICKER_INTERVAL 1000
#define WIFI_CHECK_INTERVAL 25000
#define HIT_WINDOW 400  // +- 200ms hit window
#define TILE_EXPIRE_TIME 1500  // Tiles expire 1.5s after their press time
#define MAX_TILES 200
#define COUNTDOWN_DURATION 3000  // 3 second countdown
// -------------------

int state = 0;  // 0=menu, 1=countdown, 2=playing
bool buttonPressed[NUM_LANES] = {false, false, false, false};
int buttonPins[NUM_LANES] = {button1, button2, button3, button4};
float msToPixel = (float)SCREEN_W / (SCROLL_WINDOW + SCROLL_OFFSET);
unsigned long startTime = 0;
unsigned long countdownStartTime = 0;
unsigned long lastUpdate = 0;
int laneWidth = SCREEN_W / NUM_LANES;
int flickerMain = 0;
bool sdCardAvailable = false;

// Current song info
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
  int hitY;  // Store the Y position where tile was hit
};

Tile tiles[MAX_TILES];
int tileCount = 0;

// Store last game result
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
  if (!sdCardAvailable) {
    Serial.println("SD card not available");
    return false;
  }
  
  String jsonPath = "/" + folderName + "/" + folderName + ".json";
  Serial.print("Loading tiles from: ");
  Serial.println(jsonPath);
  
  File file = SD.open(jsonPath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open JSON file");
    return false;
  }
  
  // Read entire file
  String jsonContent = "";
  while (file.available()) {
    jsonContent += (char)file.read();
  }
  file.close();
  
  Serial.println("JSON content loaded, parsing...");
  
  // Parse JSON manually (simple parser for our structure)
  int tilesStart = jsonContent.indexOf("\"tiles\"");
  if (tilesStart == -1) {
    Serial.println("No tiles array found");
    return false;
  }
  
  int arrayStart = jsonContent.indexOf('[', tilesStart);
  int arrayEnd = jsonContent.lastIndexOf(']');
  
  if (arrayStart == -1 || arrayEnd == -1) {
    Serial.println("Invalid tiles array");
    return false;
  }
  
  // Extract song metadata
  int nameStart = jsonContent.indexOf("\"songName\"");
  if (nameStart != -1) {
    int colonPos = jsonContent.indexOf(':', nameStart);
    int quoteStart = jsonContent.indexOf('"', colonPos);
    int quoteEnd = jsonContent.indexOf('"', quoteStart + 1);
    if (quoteStart != -1 && quoteEnd != -1) {
      currentSongName = jsonContent.substring(quoteStart + 1, quoteEnd);
    }
  }
  
  int artistStart = jsonContent.indexOf("\"artist\"");
  if (artistStart != -1) {
    int colonPos = jsonContent.indexOf(':', artistStart);
    int quoteStart = jsonContent.indexOf('"', colonPos);
    int quoteEnd = jsonContent.indexOf('"', quoteStart + 1);
    if (quoteStart != -1 && quoteEnd != -1) {
      currentArtist = jsonContent.substring(quoteStart + 1, quoteEnd);
    }
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
  
  Serial.print("Song: ");
  Serial.print(currentSongName);
  Serial.print(" by ");
  Serial.println(currentArtist);
  Serial.print("Difficulty: ");
  Serial.println(currentDifficulty);
  
  // Parse tiles
  String tilesStr = jsonContent.substring(arrayStart + 1, arrayEnd);
  int tileIndex = 0;
  int pos = 0;
  
  while (pos < tilesStr.length() && tileIndex < MAX_TILES) {
    int objStart = tilesStr.indexOf('{', pos);
    if (objStart == -1) break;
    
    int objEnd = tilesStr.indexOf('}', objStart);
    if (objEnd == -1) break;
    
    String tileObj = tilesStr.substring(objStart, objEnd + 1);
    
    // Extract press time
    int pressPos = tileObj.indexOf("\"press\"");
    if (pressPos != -1) {
      int colonPos = tileObj.indexOf(':', pressPos);
      int commaPos = tileObj.indexOf(',', colonPos);
      if (commaPos == -1) commaPos = tileObj.indexOf('}', colonPos);
      String pressStr = tileObj.substring(colonPos + 1, commaPos);
      pressStr.trim();
      tiles[tileIndex].press = pressStr.toInt();
    }
    
    // Extract slot (lane)
    int slotPos = tileObj.indexOf("\"slot\"");
    if (slotPos != -1) {
      int colonPos = tileObj.indexOf(':', slotPos);
      int commaPos = tileObj.indexOf(',', colonPos);
      if (commaPos == -1) commaPos = tileObj.indexOf('}', colonPos);
      String slotStr = tileObj.substring(colonPos + 1, commaPos);
      slotStr.trim();
      tiles[tileIndex].lane = slotStr.toInt();
    }
    
    tiles[tileIndex].active = true;
    tiles[tileIndex].hit = false;
    
    tileIndex++;
    pos = objEnd + 1;
  }
  
  tileCount = tileIndex;
  Serial.print("Loaded ");
  Serial.print(tileCount);
  Serial.println(" tiles");
  
  return tileCount > 0;
}

void cleanupExpiredTiles(int currentTime) {
  for (int i = 0; i < MAX_TILES; i++) {
    // Remove hit tiles after 500ms
    if (tiles[i].hit && (millis() - tiles[i].hitTime) > 500) {
      tiles[i].hit = false;
      tiles[i].active = false;
    }
    
    // Remove missed tiles
    if (tiles[i].active && !tiles[i].hit && tiles[i].press < currentTime - TILE_EXPIRE_TIME) {
      tiles[i].active = false;
      miss += 1;
    }
  }
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
  if (isHit) {
    tileColor = fixColor(TFT_WHITE);
  } else {
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

  // Draw vertical lanes
  for (int i = 1; i < NUM_LANES; i++) {
    spr.drawLine(i * laneWidth, 0, i * laneWidth, SCREEN_H, TFT_DARKGREY);
  }

  // Draw active tiles
  for (int i = 0; i < MAX_TILES; i++) {
    if (tiles[i].active || tiles[i].hit) {
      int y;
      if (tiles[i].hit) {
        // Hit tiles stay at hit line
        y = tiles[i].hitY;
      } else {
        // Moving tiles
        y = SCREEN_H - (int)((tiles[i].press - currentTime + SCROLL_OFFSET) * msToPixel);
      }
      
      if (y >= 0 && y <= SCREEN_H) {
        drawTile(&spr, y, tiles[i].lane, tiles[i].hit);
      }
    }
  }

  // Draw hit line near bottom
  int hitLineY = SCREEN_H - 30;
  spr.fillRect(0, hitLineY, SCREEN_W, 5, TFT_BLACK);

  spr.pushSprite(0, 0);
}

void drawMainScreen() {
  spr.fillSprite(fixColor(TFT_BLACK));

  // Draw title "Synchro" with shadow effect
  spr.setTextSize(4);
  spr.setTextColor(fixColor(TFT_MAGENTA));
  spr.setCursor(42, 53);
  spr.print("Synchro");
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(40, 50);
  spr.print("Synchro");

  // Draw flickering "Waiting to Start" text
  if (flickerMain) {
    spr.setTextSize(2);
    spr.setTextColor(fixColor(TFT_WHITE));
    spr.setCursor(22, 200);
    spr.print("Waiting to Start");
  }

  spr.pushSprite(0, 0);
  flickerMain = !flickerMain;
}

void drawCountdownScreen(int secondsLeft) {
  spr.fillSprite(fixColor(TFT_BLACK));

  // Draw song info
  spr.setTextSize(2);
  spr.setTextColor(fixColor(TFT_CYAN));
  spr.setCursor(10, 40);
  spr.print("Starting:");
  
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(10, 70);
  spr.print(currentSongName);
  
  spr.setTextSize(1);
  spr.setTextColor(fixColor(TFT_YELLOW));
  spr.setCursor(10, 100);
  spr.print(currentArtist);

  // Draw large countdown number
  spr.setTextSize(8);
  spr.setTextColor(fixColor(TFT_GREEN));
  String countText = String(secondsLeft);
  int textWidth = countText.length() * 48;  // Approximate width
  spr.setCursor((SCREEN_W - textWidth) / 2, 150);
  spr.print(countText);

  spr.pushSprite(0, 0);
}

void drawWiFiScreen(bool connecting) {
  spr.fillSprite(fixColor(TFT_BLACK));

  // Draw title "Synchro" with shadow effect
  spr.setTextSize(4);
  spr.setTextColor(fixColor(TFT_MAGENTA));
  spr.setCursor(42, 53);
  spr.print("Synchro");
  spr.setTextColor(fixColor(TFT_WHITE));
  spr.setCursor(40, 50);
  spr.print("Synchro");

  // Draw WiFi status text
  spr.setTextSize(2);
  spr.setTextColor(fixColor(TFT_WHITE));
  if (connecting) {
    spr.setCursor(15, 200);
    spr.print("Connecting WiFi");
    // Add dots animation
    int dots = (millis() / 500) % 4;
    spr.setCursor(195, 200);
    for (int i = 0; i < dots; i++) {
      spr.print(".");
    }
  } else {
    spr.setCursor(35, 200);
    spr.print("WiFi Connected!");
  }

  spr.pushSprite(0, 0);
}

///////////////////// SD Card Functions /////////////////////

bool initSDCard() {
  Serial.println("\n=== Starting SD Card Initialization (Separate SPI Bus) ===");
  Serial.println("Pin Configuration:");
  Serial.print("  SD_CS:   GPIO ");
  Serial.println(SD_CS);
  Serial.print("  SD_SCK:  GPIO ");
  Serial.println(SD_SCK);
  Serial.print("  SD_MISO: GPIO ");
  Serial.println(SD_MISO);
  Serial.print("  SD_MOSI: GPIO ");
  Serial.println(SD_MOSI);
  
  // Set CS pin as output and high (deselected)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);
  
  // Initialize HSPI bus with explicit pins
  Serial.println("\nInitializing HSPI bus...");
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // Try toggling CS to wake up SD card
  Serial.println("Toggling CS pin...");
  for (int i = 0; i < 10; i++) {
    digitalWrite(SD_CS, LOW);
    delayMicroseconds(10);
    digitalWrite(SD_CS, HIGH);
    delayMicroseconds(10);
  }
  delay(100);
  
  bool sdOk = false;
  const int frequencies[] = {10000000,4000000}; // Start slower: 400kHz, 1MHz, 4MHz, 10MHz
  
  for (int i = 0; i < 4 && !sdOk; i++) {
    Serial.print("\nAttempt ");
    Serial.print(i + 1);
    Serial.print(" with ");
    Serial.print(frequencies[i] / 1000);
    Serial.println("kHz...");
    
    // Try with explicit SPI mode
    if (SD.begin(SD_CS, spiSD, frequencies[i], "/sd", 5, false)) {
      delay(200); // Give SD time to settle
      
      // Verify it's actually working by checking card size
      uint64_t totalBytes = SD.totalBytes();
      Serial.print("  Total bytes detected: ");
      Serial.println(totalBytes);
      
      if (totalBytes > 0) {
        sdOk = true;
        Serial.println("✓ SD initialized successfully at ");
        Serial.print(frequencies[i] / 1000);
        Serial.println("kHz!");
        break;
      } else {
        Serial.println("✗ SD mounted but card size is 0 (not formatted or bad card)");
        SD.end();
      }
    } else {
      Serial.println("✗ SD.begin() failed");
      Serial.println("  Possible causes:");
      Serial.println("  - Wiring issue (check connections)");
      Serial.println("  - Wrong CS pin");
      Serial.println("  - Card not inserted or damaged");
      Serial.println("  - HSPI pins conflict with other hardware");
    }
    delay(500);
  }
  
  // If still failed, try with default SPI (last resort diagnostic)
  if (!sdOk) {
    Serial.println("\n=== Diagnostic: Testing with default SPI ===");
    Serial.println("This helps determine if issue is HSPI-specific or SD card itself");
    SD.end();
    delay(500);
    
    if (SD.begin(SD_CS, SPI, 1000000)) {
      Serial.println("✓ SD works on default SPI!");
      Serial.println("  Issue: HSPI pin configuration or conflict");
      Serial.println("  Solution: Check HSPI pin definitions match your hardware");
      SD.end();
    } else {
      Serial.println("✗ SD also fails on default SPI");
      Serial.println("  Issue: SD card hardware or wiring problem");
      Serial.println("  Solution: Check SD card, wiring, and CS pin");
    }
  }
  
  if (!sdOk) {
    Serial.println("\n!!! SD CARD INITIALIZATION FAILED !!!");
    Serial.println("Troubleshooting steps:");
    Serial.println("  1. Check SD card is inserted properly");
    Serial.println("  2. Ensure SD card is FAT32 formatted (NOT exFAT)");
    Serial.println("  3. Check write-protect switch is OFF");
    Serial.println("  4. Try a different SD card (2GB-32GB works best)");
    Serial.println("  5. Verify wiring: CS=" + String(SD_CS) + " SCK=" + String(SD_SCK) + " MISO=" + String(SD_MISO) + " MOSI=" + String(SD_MOSI));
    Serial.println("  6. Format card with 'SD Card Formatter' tool");
    Serial.println("\nContinuing without SD card (uploads will fail)...");
    return false;
  }
  
  // Run diagnostic
  Serial.println("\n=== SD Card Diagnostic ===");
  uint8_t cardType = SD.cardType();
  
  Serial.print("Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN (" + String(cardType) + ")");
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("Card Size: ");
  Serial.print(cardSize);
  Serial.println(" MB");
  
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  Serial.print("Total Space: ");
  Serial.print(totalBytes);
  Serial.println(" MB");
  Serial.print("Used Space: ");
  Serial.print(usedBytes);
  Serial.println(" MB");
  Serial.print("Free Space: ");
  Serial.print(totalBytes - usedBytes);
  Serial.println(" MB");
  
  // Test file operations
  Serial.println("\n=== Testing File Operations ===");
  
  // Test write
  Serial.print("Write test: ");
  File testFile = SD.open("/test_write.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Test write successful at " + String(millis()));
    testFile.close();
    Serial.println("✓ PASSED");
  } else {
    Serial.println("✗ FAILED - Card may be write-protected");
    return false;
  }
  
  // Test read
  Serial.print("Read test: ");
  testFile = SD.open("/test_write.txt", FILE_READ);
  if (testFile) {
    String content = testFile.readString();
    testFile.close();
    Serial.println("✓ PASSED");
  } else {
    Serial.println("✗ FAILED");
  }
  
  // Test delete
  Serial.print("Delete test: ");
  if (SD.remove("/test_write.txt")) {
    Serial.println("✓ PASSED");
  } else {
    Serial.println("✗ FAILED");
  }
  
  // Test directory creation
  Serial.print("Directory creation test: ");
  if (SD.mkdir("/test_dir")) {
    Serial.println("✓ PASSED");
    if (SD.rmdir("/test_dir")) {
      Serial.println("  Directory cleanup: ✓ PASSED");
    }
  } else {
    Serial.println("✗ FAILED - This will cause upload failures!");
    Serial.println("  SOLUTION: Reformat SD card as FAT32");
    return false;
  }
  
  // List root directory
  Serial.println("\n=== Root Directory Contents ===");
  File root = SD.open("/");
  if (root) {
    while (true) {
      File entry = root.openNextFile();
      if (!entry) break;
      Serial.print(entry.isDirectory() ? "[DIR]  " : "[FILE] ");
      Serial.print(entry.name());
      if (!entry.isDirectory()) {
        Serial.print(" (");
        Serial.print(entry.size());
        Serial.print(" bytes)");
      }
      Serial.println();
      entry.close();
    }
    root.close();
  }
  
  Serial.println("=== SD Diagnostic Complete ===\n");
  return true;
}

///////////////////// Network Functions /////////////////////

void handleCORSPreflight(WiFiClient& client) {
  Serial.println("Handling CORS preflight");
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
  client.print(state);  // 0=menu, 1=countdown, 2=playing
  client.print(",\"songs\":[");
  
  if (sdCardAvailable) {
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
  
  Serial.print("Play request for song: ");
  Serial.println(songFolder);
  
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
  
  // Set game to countdown state
  state = 1;
  countdownStartTime = millis();
  resetScore();
  
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
  client.println("}");
  client.flush();
  
  Serial.println("Game countdown started!");
}

void handleGetResult(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
  
  if (lastResultJSON == "{}") {
    client.print("{\"songName\":\"");
    client.print(currentSongName);
    client.print("\",\"artist\":\"");
    client.print(currentArtist);
    client.print("\",\"difficulty\":");
    client.print(currentDifficulty);
    client.print(",\"result\":{\"miss\":");
    client.print(miss);
    client.print(",\"ok\":");
    client.print(ok);
    client.print(",\"great\":");
    client.print(great);
    client.print(",\"perfect\":");
    client.print(perfect);
    client.println("}}");
  } else {
    client.print(lastResultJSON);
  }
  client.flush();
}

void handleUpload(WiFiClient& client) {
  if (!sdCardAvailable) {
    Serial.println("Upload rejected - SD card not available");
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

  Serial.println("=== Upload Request Started ===");

  // Read all headers until we find empty line
  String headerBuffer = "";
  unsigned long headerTimeout = millis();
  
  while (client.connected() && (millis() - headerTimeout < 5000)) {
    if (client.available()) {
      char c = client.read();
      headerBuffer += c;
      
      // Check if we've reached end of headers (double newline)
      if (headerBuffer.endsWith("\r\n\r\n") || headerBuffer.endsWith("\n\n")) {
        break;
      }
    } else {
      delay(1);
    }
  }

  Serial.println("Raw headers received:");
  Serial.println(headerBuffer);
  Serial.println("--- End of headers ---");

  // Parse headers from buffer
  int lineStart = 0;
  int lineEnd = 0;
  
  while (lineEnd < headerBuffer.length()) {
    lineEnd = headerBuffer.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = headerBuffer.length();
    
    String line = headerBuffer.substring(lineStart, lineEnd);
    line.trim();
    
    if (line.length() > 0) {
      String lowerLine = line;
      lowerLine.toLowerCase();
      
      if (lowerLine.startsWith("content-length:")) {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
          String lengthStr = line.substring(colonPos + 1);
          lengthStr.trim();
          contentLength = lengthStr.toInt();
          hasContentLength = true;
          Serial.print("Found Content-Length: ");
          Serial.println(contentLength);
        }
      }
      
      if (lowerLine.startsWith("filename:")) {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
          filename = line.substring(colonPos + 1);
          filename.trim();
          hasFilename = true;
          Serial.print("Found Filename: ");
          Serial.println(filename);
        }
      }
    }
    
    lineStart = lineEnd + 1;
  }

  Serial.println("=== Headers parsing complete ===");

  // Validate filename
  if (!hasFilename || filename.length() == 0) {
    Serial.println("ERROR: Missing Filename header");
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Filename header required");
    client.flush();
    return;
  }

  // Validate content length
  if (!hasContentLength || contentLength <= 0) {
    Serial.println("ERROR: Invalid or missing Content-Length");
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Content-Length required");
    client.flush();
    return;
  }

  // Add leading slash if missing
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  // Extract base name and extension
  int dotIndex = filename.lastIndexOf('.');
  if (dotIndex == -1) {
    Serial.println("ERROR: No file extension found");
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
  
  // Sanitize baseName
  baseName.replace(" ", "_");
  baseName.replace("-", "_");
  String sanitized = "";
  for (int i = 0; i < baseName.length(); i++) {
    char c = baseName.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
      sanitized += c;
    }
  }
  baseName = sanitized;
  
  Serial.print("Sanitized base name: ");
  Serial.println(baseName);
  Serial.print("Extension: ");
  Serial.println(extension);

  // Create folder with retry logic
  String folderPath = "/" + baseName;
  if (!SD.exists(folderPath)) {
    Serial.print("Creating folder: ");
    Serial.println(folderPath);
    
    bool folderCreated = false;
    for (int attempt = 1; attempt <= 5; attempt++) {
      if (SD.mkdir(folderPath)) {
        folderCreated = true;
        Serial.println("Folder created successfully");
        delay(100); // Give SD time to update
        break;
      } else {
        Serial.print("Attempt ");
        Serial.print(attempt);
        Serial.println(" failed");
        delay(200);
      }
    }
    
    if (!folderCreated) {
      Serial.println("ERROR: Failed to create folder after 5 attempts");
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Connection: close");
      client.println();
      client.print("{\"error\":\"Failed to create folder. SD card may need reformatting as FAT32\",\"path\":\"");
      client.print(folderPath);
      client.println("\"}");
      client.flush();
      return;
    }
  } else {
    Serial.println("Folder already exists");
  }

  // Full file path
  String fullPath = folderPath + "/" + baseName + extension;
  Serial.print("Full path: ");
  Serial.println(fullPath);

  // Remove existing file
  if (SD.exists(fullPath)) {
    Serial.println("Removing existing file...");
    SD.remove(fullPath);
    delay(100);
  }

  // Open file for writing
  File f = SD.open(fullPath.c_str(), FILE_WRITE);
  if (!f) {
    Serial.println("ERROR: Failed to open file for writing");
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Connection: close");
    client.println();
    client.println("Error: Failed to create file on SD card");
    client.flush();
    return;
  }

  // Write data
  Serial.println("Writing file data...");
  Serial.print("Expecting ");
  Serial.print(contentLength);
  Serial.println(" bytes");
  
  int bytesWritten = 0;
  int lastProgress = 0;
  unsigned long writeStartTime = millis();
  unsigned long lastByteTime = millis();
  unsigned long lastFlush = millis();
  
  // Use buffer for faster writes
  const int BUFFER_SIZE = 512;
  uint8_t buffer[BUFFER_SIZE];
  int bufferIndex = 0;
  
  while (bytesWritten < contentLength) {
    // Check if client is still connected
    if (!client.connected()) {
      Serial.println("ERROR: Client disconnected during upload");
      f.close();
      SD.remove(fullPath);
      return;
    }
    
    if (client.available()) {
      byte b = client.read();
      buffer[bufferIndex++] = b;
      bytesWritten++;
      lastByteTime = millis();
      
      // Write buffer when full
      if (bufferIndex >= BUFFER_SIZE) {
        f.write(buffer, bufferIndex);
        bufferIndex = 0;
        
        // Flush to SD periodically (every second)
        if (millis() - lastFlush > 1000) {
          f.flush();
          lastFlush = millis();
        }
      }
      
      // Progress every 10%
      int progress = (bytesWritten * 100) / contentLength;
      if (progress >= lastProgress + 10) {
        // Flush buffer and file at progress milestones
        if (bufferIndex > 0) {
          f.write(buffer, bufferIndex);
          bufferIndex = 0;
        }
        f.flush();
        
        Serial.print("Progress: ");
        Serial.print(progress);
        Serial.print("% (");
        Serial.print(bytesWritten);
        Serial.print("/");
        Serial.print(contentLength);
        Serial.print(") - ");
        Serial.print((millis() - writeStartTime) / 1000);
        Serial.println("s elapsed");
        lastProgress = progress;
      }
    } else {
      // Timeout after 30 seconds with no data
      unsigned long timeSinceLastByte = millis() - lastByteTime;
      if (timeSinceLastByte > 30000) {
        Serial.print("ERROR: Timeout waiting for data (");
        Serial.print(timeSinceLastByte / 1000);
        Serial.println("s with no data)");
        Serial.print("Received ");
        Serial.print(bytesWritten);
        Serial.print(" of ");
        Serial.print(contentLength);
        Serial.println(" bytes");
        
        // Write remaining buffer before closing
        if (bufferIndex > 0) {
          f.write(buffer, bufferIndex);
        }
        f.close();
        SD.remove(fullPath);
        
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
  
  // Write any remaining buffered data
  if (bufferIndex > 0) {
    f.write(buffer, bufferIndex);
  }
  
  f.close();
  
  unsigned long uploadTime = millis() - writeStartTime;
  Serial.print("Upload complete! ");
  Serial.print(bytesWritten);
  Serial.print(" bytes in ");
  Serial.print(uploadTime);
  Serial.println("ms");

  // Verify file
  if (SD.exists(fullPath)) {
    File verify = SD.open(fullPath, FILE_READ);
    int fileSize = verify.size();
    verify.close();
    
    Serial.print("Verified file size: ");
    Serial.println(fileSize);
    
    if (fileSize == contentLength) {
      Serial.println("SUCCESS: File size matches!");
    } else {
      Serial.println("WARNING: File size mismatch!");
    }
  }

  // Send response
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.print("{\"success\":true,\"filename\":\"");
  client.print(filename);
  client.print("\",\"size\":");
  client.print(bytesWritten);
  client.print(",\"path\":\"");
  client.print(fullPath);
  client.println("\"}");
  client.flush();
  
  Serial.println("=== Upload Request Complete ===");
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("Client connected!");
  
  // Set longer timeout for large file uploads
  client.setTimeout(600000); // 60 second timeout

  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("Client timeout waiting for initial data");
      client.stop();
      return;
    }
    delay(1);
  }

  if (client.available()) {
    String req = client.readStringUntil('\n');
    req.trim();
    Serial.print("Request: ");
    Serial.println(req);

    if (req.indexOf("OPTIONS") >= 0) {
      handleCORSPreflight(client);
    } else if (req.indexOf("GET /handshake") >= 0 || req.indexOf("POST /handshake") >= 0) {
      Serial.println("Handling handshake");
      while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 2) break;
      }
      handleHandshake(client);
    } else if (req.indexOf("GET /play") >= 0 || req.indexOf("POST /play") >= 0) {
      Serial.println("Handling play request");
      
      // Extract song parameter from URL
      int songParamPos = req.indexOf("song=");
      String songFolder = "";
      if (songParamPos != -1) {
        int spacePos = req.indexOf(' ', songParamPos);
        int ampPos = req.indexOf('&', songParamPos);
        int endPos = spacePos;
        if (ampPos != -1 && (ampPos < endPos || endPos == -1)) {
          endPos = ampPos;
        }
        songFolder = req.substring(songParamPos + 5, endPos);
        
        // URL decode
        songFolder.replace("%20", " ");
        songFolder.replace("+", " ");
      }
      
      while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 2) break;
      }
      handlePlay(client, songFolder);
    } else if (req.indexOf("GET /getresult") >= 0 || req.indexOf("POST /getresult") >= 0) {
      Serial.println("Handling get result");
      while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 2) break;
      }
      handleGetResult(client);
    } else if (req.indexOf("POST /upload") >= 0) {
      Serial.println("Handling file upload");
      handleUpload(client);
    } else {
      Serial.println("Unknown request");
      client.println("HTTP/1.1 404 Not Found");
      client.println("Connection: close");
      client.println();
      client.flush();
    }
  }

  delay(10);
  client.stop();
  Serial.println("Client disconnected");
}

void checkWiFiStatus() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck > WIFI_CHECK_INTERVAL) {
    lastCheck = now;
    Serial.print("WiFi Status: ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
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
    Serial.print("Button pressed - Lane: ");
    Serial.print(lane);
    Serial.print(" Time: ");
    Serial.println(currentTime);
    
    // Check for tiles in hit window
    for (int i = 0; i < MAX_TILES; i++) {
      if (tiles[i].active && !tiles[i].hit && tiles[i].lane == lane) {
        int timeDiff = tiles[i].press - (currentTime + SCROLL_OFFSET);
        if (abs(timeDiff) <= HIT_WINDOW) {
          tiles[i].active = false;
          tiles[i].hit = true;
          tiles[i].hitY = SCREEN_H - (int)((tiles[i].press - currentTime + SCROLL_OFFSET + 100) * msToPixel);
          tiles[i].hitTime = millis();
          Serial.print("HIT! Tile deleted - Lane: ");
          Serial.print(lane);
          Serial.print(" Tile time: ");
          Serial.print(tiles[i].press);
          Serial.print(" Time diff: ");
          Serial.println(timeDiff);
          if (abs(timeDiff) < 50) {
            perfect += 1;
          } else if (abs(timeDiff) < 150) {
            great += 1;
          } else {
            ok += 1;
          }
          return;
        }
      }
    }
    Serial.println("MISS - No tile in hit window");
    miss += 1;
  }
}

bool checkSceneChange() {
  bool button1State = (digitalRead(buttonPins[0]) == HIGH);
  bool button2State = (digitalRead(buttonPins[1]) == HIGH);
  bool button3State = (digitalRead(buttonPins[2]) == HIGH);
  bool button4State = (digitalRead(buttonPins[3]) == HIGH);
  return button1State && button2State && button3State && button4State;
}

///////////////////// Result ////////////////////

void resetScore() {
  miss = 0;
  ok = 0;
  great = 0;
  perfect = 0;
}

void setGameResult(const String& songName, const String& artist, int difficulty, int miss, int ok, int great, int perfect) {
  lastResultJSON = "{\"songName\":\"" + songName + 
                   "\",\"artist\":\"" + artist + 
                   "\",\"difficulty\":" + String(difficulty) + 
                   ",\"result\":{\"miss\":" + String(miss) + 
                   ",\"ok\":" + String(ok) + 
                   ",\"great\":" + String(great) + 
                   ",\"perfect\":" + String(perfect) + "}}";
}

///////////////////// Setup /////////////////////

void initDisplay() {
  tft.init();
  tft.fillScreen(TFT_BLACK);

  Serial.println("Creating sprite...");
  spr.setColorDepth(8);
  tft.setRotation(2);

  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    Serial.println("Full sprite failed, trying half size...");
    if (!spr.createSprite(SCREEN_W, SCREEN_H / 2)) {
      Serial.println("Sprite failed entirely!");
    }
  } else {
    Serial.println("Sprite OK");
  }
}

void initWiFi() {
  drawWiFiScreen(true);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    drawWiFiScreen(true);
    
    if (millis() - wifiStartTime > 30000) {
      Serial.println("\nWiFi connection timeout!");
      spr.fillSprite(fixColor(TFT_BLACK));
      spr.setTextSize(2);
      spr.setTextColor(fixColor(TFT_RED));
      spr.setCursor(20, 120);
      spr.print("WiFi Failed!");
      spr.pushSprite(0, 0);
      delay(3000);
      return;
    }
  }
  
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  drawWiFiScreen(false);
  delay(1000);

  server.begin();
  Serial.println("Server started.");
  Serial.print("Listening on: http://");
  Serial.print(WiFi.localIP());
  Serial.println(":80");

  if (MDNS.begin("synchro")) {
    Serial.println("mDNS responder started!");
    Serial.println("Access at: http://synchro.local/handshake");
  } else {
    Serial.println("Error starting mDNS");
  }
}

void setup() {
  Serial.begin(9600);
  delay(2000);
  
  Serial.println("\n=== Synchro Rhythm Game - Separate SPI Bus Version ===");
  
  // Initialize buttons
  for (int i = 0; i < NUM_LANES; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Initialize display FIRST (uses default VSPI)
  Serial.println("Initializing TFT display on VSPI...");
  initDisplay();
  
  // Initialize SD card on separate HSPI bus
  Serial.println("Initializing SD card on HSPI...");
  sdCardAvailable = initSDCard();
  
  // Initialize WiFi
  Serial.println("Initializing WiFi...");
  initWiFi();
  
  // Disable WiFi sleep for better stability during uploads
  WiFi.setSleep(false);

  drawMainScreen();
  startTime = millis();
  lastUpdate = millis();
  
  Serial.println("=== Setup Complete ===\n");
}

///////////////////// Main Loop /////////////////////

void updateMainMenu() {
  unsigned long now = millis();

  // Update flicker animation
  if (now - lastUpdate >= FLICKER_INTERVAL) {
    lastUpdate = now;
    drawMainScreen();
  }
}

void updateCountdown() {
  unsigned long now = millis();
  unsigned long elapsed = now - countdownStartTime;
  
  if (elapsed >= COUNTDOWN_DURATION) {
    // Countdown finished, start game
    state = 2;
    startTime = millis();
    lastUpdate = millis();
    Serial.println("Game started!");
    return;
  }
  
  // Update countdown display every 100ms
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

  if (checkSceneChange()) {
    state = 0;
    drawMainScreen();
    Serial.println("Returned to menu");
    setGameResult(currentSongName, currentArtist, currentDifficulty, miss, ok, great, perfect);
    delay(200);  // Debounce
    return;
  }

  // Check button presses for all lanes (skip button check during scene change detection)
  bool button1State = (digitalRead(buttonPins[0]) == HIGH);
  bool button2State = (digitalRead(buttonPins[1]) == HIGH);
  bool button3State = (digitalRead(buttonPins[2]) == HIGH);
  bool button4State = (digitalRead(buttonPins[3]) == HIGH);
  
  // Only check individual button presses if not doing scene change combo
  if (!(button1State && button2State && button3State && button4State)) {
    for (int i = 0; i < NUM_LANES; i++) {
      checkButtonPress(i, currentTime);
    }
  }

  // Cleanup expired tiles
  cleanupExpiredTiles(currentTime);

  // Update game frame
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    drawGameFrame(currentTime);
  }
}

void loop() {
  handleClient();
  checkWiFiStatus();

  if (state == 0) {
    updateMainMenu();
  } else if (state == 1) {
    updateCountdown();
  } else if (state == 2) {
    updateGamePlay();
  }
}