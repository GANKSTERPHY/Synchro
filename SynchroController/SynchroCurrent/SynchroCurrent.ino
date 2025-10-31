#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFiS3.h>
#include <SD.h>
#include <esp_heap_caps.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// ----- CONFIG -----
const int button1 = 25;
const char* ssid = "SSID";
const char* password = "Password";
IPAddress local_IP(192, 168, 1, 177);  // desired static IP
IPAddress subnet(255, 255, 255, 0);    // subnet mask
#define SCREEN_W 320
#define SCREEN_H 240
#define NUM_LANES 4
#define UPDATE_INTERVAL 10  // ms per frame
#define SCROLL_WINDOW 2000  // ms visible ahead
#define SCROLL_OFFSET 0     // ms behind current
#define TILE_SPACING 500    // mock tile every 500 ms
// -------------------

int state = 0;  //0 - Start, 1 - Play

float msToPixel = (float)SCREEN_W / (SCROLL_WINDOW + SCROLL_OFFSET);
unsigned long startTime = 0;
unsigned long lastUpdate = 0;

int laneHeight = SCREEN_H / NUM_LANES;

struct Tile {
  int press;
  int lane;
};

//////////////////Screen Func////////////////////////

uint16_t fixColor(uint16_t c) {
  switch (c) {
    case TFT_BLACK: return TFT_WHITE;
    case TFT_WHITE: return TFT_BLACK;
    case TFT_RED: return TFT_CYAN;
    case TFT_CYAN: return TFT_RED;
    case TFT_GREEN: return TFT_MAGENTA;  // if needed
    case TFT_MAGENTA: return TFT_GREEN;
    // add others if needed
    default: return c;
  }
}


Tile generateTile(int i) {
  Tile t;
  t.press = i * TILE_SPACING;
  t.lane = i % NUM_LANES;
  return t;
}

int timeToX(int tileTime, int currentTime) {
  int delta = tileTime - currentTime;
  return (int)((delta + SCROLL_OFFSET) * msToPixel);  // left → right
}

void drawTile(TFT_eSPI *gfx, int x, int lane) {
  int y = lane * laneHeight;
  uint16_t tileColor;
  switch (lane) {
    case 0: tileColor = fixColor(TFT_RED); break;
    case 1: tileColor = fixColor(TFT_GREEN); break;
    case 2: tileColor = fixColor(TFT_CYAN); break;
    case 3: tileColor = fixColor(TFT_MAGENTA); break;
    default: tileColor = tft.color565(0, 0, 0);
  }
  gfx->fillRect(x - 8, y + 5, 16, laneHeight - 10, tileColor);
}

void drawFrame(TFT_eSPI *gfx, int currentTime) {
  gfx->fillScreen(fixColor(TFT_BLACK));

  // lanes
  for (int i = 1; i < NUM_LANES; i++) {
    gfx->drawLine(0, i * laneHeight, SCREEN_W, i * laneHeight, TFT_DARKGREY);
  }

  // tiles
  for (int i = 0; i < 40; i++) {
    Tile t = generateTile(i);
    int x = timeToX(t.press, currentTime);
    if (x >= 0 && x <= SCREEN_W) {
      drawTile(gfx, x, t.lane);
    }
  }

  // hit line
  gfx->drawLine(10, 0, 10, SCREEN_H, TFT_BLACK);
}
int flickerMain = 0;
void displayMainScreen() {
  if (flickerMain){
    flickerMain = 0;
    tft.fillScreen(fixColor(TFT_BLACK));
    return;
  }
  tft.setRotation(2);
  tft.fillScreen(fixColor(TFT_BLACK));
  tft.setTextSize(4);
  tft.setTextColor(fixColor(TFT_MAGENTA));
  tft.setCursor(42, 53);  // Adjust position for portrait mode
  tft.print("Synchro");
  tft.setTextColor(fixColor(TFT_WHITE));
  tft.setCursor(40, 50);  // Adjust position for portrait mode
  tft.print("Synchro");

  tft.setTextSize(2);
  tft.setTextColor(fixColor(TFT_WHITE));
  tft.setCursor(22, 200);  // Adjust position for portrait mode
  tft.print("Waiting to Start");
  flickerMain = 1;
}
////////////////////////Screen Func////////////////////////////////

////////////////////////Network Func////////////////////////////////
void handleHandshake(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();

  client.print("{\"status\":\"Arduino ready\",\"songs\":[");

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
/////////////////////Network Func////////////////////////////


void setup() {
  tft.init();

  tft.fillScreen(TFT_BLACK);

  Serial.begin(9600);
  pinMode(button1, INPUT_PULLUP);
  delay(2000);
  Serial.println("Creating sprite...");
  spr.setColorDepth(8);
  tft.setRotation(1);  // try 3 if upside-down
  // Try to allocate full-screen sprite
  if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
    Serial.println("\nFull sprite failed, trying half size...");
    if (!spr.createSprite(SCREEN_W, SCREEN_H / 2)) {
      Serial.println("\nSprite failed entirely — using direct draw.");
    }
  } else {
    spr.setColorDepth(8);
    Serial.println("Sprite OK");
  }

  // Configure static IP
  WiFi.config(local_IP, gateway, subnet);

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

  displayMainScreen();
  startTime = millis();
}

void loop() {
  unsigned long now = millis();

  // ---Memory Check---
  // Serial.print("Free heap: ");
  // Serial.println(ESP.getFreeHeap());

  client = server.available();
  if (!client) return;

  String req = client.readStringUntil('\n');
  if (req.startsWith("GET /handshake")) {
    handleHandshake(client);
  }

  Serial.println(state);

  if (state == 0) {
    
    if (digitalRead(button1) == HIGH) {
      state = 1;
      delay(200);
      return;
    }
    if (now - lastUpdate < 1500) return;
    lastUpdate = millis();
    if (flickerMain){
      lastUpdate = millis()-1000;
    }
    
    displayMainScreen();
  } else if (state == 1) {
    if (digitalRead(button1) == HIGH) {
      state = 0;
      displayMainScreen();
      delay(200);
      return;
    }
    
    if (now - lastUpdate < UPDATE_INTERVAL) return;

    lastUpdate = now;

    tft.setRotation(1);

    int currentTime = (now - startTime) % (TILE_SPACING * 40);

    if (spr.created()) {
      spr.fillSprite(TFT_BLACK);
      drawFrame(&spr, currentTime);
      spr.pushSprite(0, 0);
    } else {
      drawFrame(&tft, currentTime);
    }
  }
  client.stop();
}
