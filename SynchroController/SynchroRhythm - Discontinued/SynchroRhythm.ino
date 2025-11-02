#include <WiFiS3.h>
#include <SD.h>

#define SD_CS 5
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASS";

IPAddress local_IP(192, 168, 1, 177);  // desired static IP
IPAddress gateway(192, 168, 1, 1);     // router IP
IPAddress subnet(255, 255, 255, 0);    // subnet mask

WiFiServer server(80);
WiFiClient client;

void setup() {
  Serial.begin(115200);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed!");
    while (1)
      ;
  }
  Serial.println("SD ready.");

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
}

void loop() {
  client = server.available();
  if (!client) return;

  String req = client.readStringUntil('\n');
  if (req.startsWith("POST /upload_wav")) {
    handleUpload(client);
  } else if (req.startsWith("POST /upload_json")) {
    handleUpload(client);
  } else if (req.startsWith("GET /handshake")) {
    handleHandshake(client);
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  }
  delay(10);
  client.stop();
}

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


void handleUpload(WiFiClient& client) {
  String filename = "upload.bin";  // default fallback
  int contentLength = 0;

  // Read headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();  // remove \r and whitespace

    if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
    }

    if (line.startsWith("Filename:")) {
      filename = line.substring(9);
      filename.trim();  // clean up
      if (!filename.startsWith("/")) filename = "/" + filename;
    }

    if (line == "") break;  // end of headers
  }

  // --- Extract base name and extension ---
  int dotIndex = filename.lastIndexOf('.');
  String baseName = filename.substring(1, dotIndex);  // remove leading '/' and extension
  String extension = filename.substring(dotIndex);    // keep dot, e.g., ".wav"

  // --- Create folder if it doesn't exist ---
  String folderPath = "/" + baseName;
  if (!SD.exists(folderPath)) {
    SD.mkdir(folderPath);
  }

  // --- Full file path ---
  String fullPath = folderPath + "/" + baseName + extension;

  // --- Overwrite old file if exists ---
  if (SD.exists(fullPath)) {
    SD.remove(fullPath);
  }

  // --- Open file for writing ---
  File f = SD.open(fullPath.c_str(), FILE_WRITE);
  if (!f) {
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println();
    return;
  }

  // Read file body
  for (int i = 0; i < contentLength; i++) {
    while (!client.available()) delay(1);
    f.write(client.read());
  }

  f.close();

  client.println("HTTP/1.1 200 OK");
  client.println();
  client.println("Upload complete: " + filename);
}
