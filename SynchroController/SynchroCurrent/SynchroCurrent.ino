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
IPAddress local_IP(192, 168, 1, 177);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
#define SD_CS 4
#define SCREEN_W 240
#define SCREEN_H 320
#define NUM_LANES 4
#define UPDATE_INTERVAL 10
#define SCROLL_WINDOW 2000
#define SCROLL_OFFSET 100
#define TILE_SPACING 500
#define FLICKER_INTERVAL 1000
#define WIFI_CHECK_INTERVAL 25000
#define HIT_WINDOW 400  // +- 200ms hit window
#define TILE_EXPIRE_TIME 1500  // Tiles expire 1 second after their press time
#define MAX_TILES 50
// -------------------

int state = 0;
bool buttonPressed[NUM_LANES] = {false, false, false, false};
int buttonPins[NUM_LANES] = {button1, button2, button3, button4};
float msToPixel = (float)SCREEN_W / (SCROLL_WINDOW + SCROLL_OFFSET);
unsigned long startTime = 0;
unsigned long lastUpdate = 0;
int laneWidth = SCREEN_W / NUM_LANES;
int flickerMain = 0;

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
int nextTileIndex = 0;

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
  nextTileIndex = 0;
  for (int i = 0; i < MAX_TILES; i++) {
    tiles[i].active = false;
    tiles[i].hit = false;
  }
  // Generate initial tiles
  for (int i = 0; i < 20; i++) {
    tiles[i].press = i * TILE_SPACING;
    tiles[i].lane = i % NUM_LANES;
    tiles[i].active = true;
    tiles[i].hit = false;
    tileCount = i + 1;
    nextTileIndex = i + 1;
  }
}

void generateNewTile() {
  // Find the last active tile's press time
  int maxPress = 0;
  for (int i = 0; i < MAX_TILES; i++) {
    if (tiles[i].active && tiles[i].press > maxPress) {
      maxPress = tiles[i].press;
    }
  }
  
  int newPress = maxPress + TILE_SPACING;
  
  // Reuse slot or create new
  int slot = -1;
  for (int i = 0; i < MAX_TILES; i++) {
    if (!tiles[i].active && !tiles[i].hit) {
      slot = i;
      break;
    }
  }
  
  if (slot == -1 && tileCount < MAX_TILES) {
    slot = tileCount;
    tileCount++;
  }
  
  if (slot != -1) {
    tiles[slot].press = newPress;
    tiles[slot].lane = (newPress / TILE_SPACING) % NUM_LANES;
    tiles[slot].active = true;
    tiles[slot].hit = false;
  }
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
      generateNewTile();
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
  int hitLineY = SCREEN_H - 20;
  spr.drawLine(0, hitLineY, SCREEN_W, hitLineY, TFT_BLACK);

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

///////////////////// Network Functions /////////////////////

void handleCORSPreflight(WiFiClient& client) {
  Serial.println("Handling CORS preflight");
  client.println("HTTP/1.1 204 No Content");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
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

  client.print("{\"status\":\"Arduino ready\",\"songs\":[");
  client.println("]}");
}

void handleGetResult(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
    client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
  client.print(lastResultJSON);
  client.flush();
  //Reset game here
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("Client connected!");

  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 1000) {
      Serial.println("Client timeout");
      client.stop();
      return;
    }
    delay(1);
  }

  if (client.available()) {
    String req = client.readStringUntil('\n');
    Serial.println("Request: " + req);

    if (req.indexOf("OPTIONS") >= 0) {
      handleCORSPreflight(client);
    } else if (req.indexOf("/handshake") >= 0) {
      Serial.println("Handling handshake");
      handleHandshake(client);
    }else if (req.indexOf("/getresult") >= 0) {
      Serial.println("Handling fetch result");
      handleGetResult(client);
    }
  }

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
        int timeDiff = tiles[i].press - (currentTime+SCROLL_OFFSET);
        if (abs(timeDiff) <= HIT_WINDOW) {
          tiles[i].active = false;
          tiles[i].hit = true;
          tiles[i].hitY = SCREEN_H - (int)((tiles[i].press - currentTime + SCROLL_OFFSET+100) * msToPixel);
          tiles[i].hitTime = millis();
          Serial.print("HIT! Tile deleted - Lane: ");
          Serial.print(lane);
          Serial.print(" Tile time: ");
          Serial.print(tiles[i].press);
          Serial.print(" Time diff: ");
          Serial.println(timeDiff);
          if (timeDiff < 50){
            perfect +=1;
          }else if (timeDiff < 150){
            great += 1;
          }else{
            ok += 1;
          }
          generateNewTile();
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
  return button1State && button4State;
}

///////////////////// Result ////////////////////

void resetScore(){
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
  // Show connecting screen
  drawWiFiScreen(true);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Update screen with animated dots
    drawWiFiScreen(true);
    
    // Timeout after 30 seconds
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

  // Show success screen briefly
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
  for (int i = 0; i < NUM_LANES; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  delay(2000);

  initDisplay();
  initWiFi();

  drawMainScreen();
  startTime = millis();
  lastUpdate = millis();
}

///////////////////// Main Loop /////////////////////

void updateMainMenu() {
  unsigned long now = millis();

  if (checkSceneChange()) {
    state = 1;
    startTime = millis();
    lastUpdate = millis();
    initializeTiles();
    Serial.println("Game started!");
    delay(200);  // Debounce
    return;
  }

  // Update flicker animation
  if (now - lastUpdate >= FLICKER_INTERVAL) {
    lastUpdate = now;
    drawMainScreen();
  }
}

void updateGamePlay() {
  unsigned long now = millis();
  int currentTime = now - startTime;

  if (checkSceneChange()) {
    state = 0;
    drawMainScreen();
    Serial.println("Returned to menu");
    setGameResult("SomeSung","Flameworkguy",2,miss,ok,great,perfect);
    resetScore();
    delay(200);  // Debounce
    return;
  }

  // Check button presses for all lanes (skip button check during scene change detection)
  bool button1Down = (digitalRead(buttonPins[0]) == HIGH);
  bool button4Down = (digitalRead(buttonPins[3]) == HIGH);
  
  // Only check individual button presses if not doing scene change combo
  if (!(button1Down && button4Down)) {
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
    updateGamePlay();
  }
}