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
const int button1 = 25;
const char* ssid = "Guh";
const char* password = "Nuhuhwhysolong";
IPAddress local_IP(192, 168, 1, 177);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
#define SCREEN_W 240
#define SCREEN_H 320
#define NUM_LANES 4
#define UPDATE_INTERVAL 10
#define SCROLL_WINDOW 2000
#define SCROLL_OFFSET 0
#define TILE_SPACING 500
#define FLICKER_INTERVAL 1000
#define WIFI_CHECK_INTERVAL 25000
// -------------------

int state = 0;
bool buttonPressed = false;
float msToPixel = (float)SCREEN_W / (SCROLL_WINDOW + SCROLL_OFFSET);
unsigned long startTime = 0;
unsigned long lastUpdate = 0;
int laneWidth = SCREEN_W / NUM_LANES;
int flickerMain = 0;

struct Tile {
  int press;
  int lane;
};

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

Tile generateTile(int i) {
  Tile t;
  t.press = i * TILE_SPACING;
  t.lane = i % NUM_LANES;
  return t;
}

int timeToY(int tileTime, int currentTime) {
  int delta = tileTime - currentTime;
  return SCREEN_H - (int)((delta + SCROLL_OFFSET) * msToPixel);
}




///////////////////// Drawing Functions /////////////////////

void drawTile(TFT_eSprite* gfx, int y, int lane) {
  int laneWidth = SCREEN_W / NUM_LANES;
  int x = lane * laneWidth;  // lane horizontal position

  uint16_t tileColor;
  switch (lane) {
    case 0: tileColor = fixColor(TFT_RED); break;
    case 1: tileColor = fixColor(TFT_GREEN); break;
    case 2: tileColor = fixColor(TFT_CYAN); break;
    case 3: tileColor = fixColor(TFT_MAGENTA); break;
    default: tileColor = tft.color565(0, 0, 0);
  }
  gfx->fillRect(x + 5, y - 8, laneWidth - 10, 16, tileColor);
}



void drawGameFrame(int currentTime) {
  spr.fillSprite(fixColor(TFT_BLACK));

  // Draw vertical lanes
  for (int i = 1; i < NUM_LANES; i++) {
    spr.drawLine(i * laneWidth, 0, i * laneWidth, SCREEN_H, TFT_DARKGREY);
  }

  // Draw tiles
  for (int i = 0; i < 40; i++) {
    Tile t = generateTile(i);
    int y = SCREEN_H - (int)((t.press - currentTime + SCROLL_OFFSET) * msToPixel);  // top → bottom
    if (y >= 0 && y <= SCREEN_H) {
      drawTile(&spr, y, t.lane);
    }
  }

  // Draw hit line near bottom
  int hitLineY = SCREEN_H - 10;
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
  // SD card reading commented out for now
  client.println("]}");
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("Client connected!");

  // Wait for data with timeout
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

bool checkButtonPress() {
  bool currentButtonState = (digitalRead(button1) == HIGH);
  bool buttonJustPressed = currentButtonState && !buttonPressed;
  buttonPressed = currentButtonState;
  return buttonJustPressed;
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
  pinMode(button1, INPUT_PULLUP);
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

  if (checkButtonPress()) {
    state = 1;
    startTime = millis();
    lastUpdate = millis();
    Serial.println("Game started!");
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

  if (checkButtonPress()) {
    state = 0;
    drawMainScreen();
    Serial.println("Returned to menu");
    return;
  }

  // Update game frame
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    int currentTime = (now - startTime) % (TILE_SPACING * 40);
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