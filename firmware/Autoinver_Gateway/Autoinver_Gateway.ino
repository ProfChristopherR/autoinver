/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║                  AUTOINVER GATEWAY v2.0 - FIRMWARE                        ║
 * ║              Estación Base LoRa-WiFi Bridge + Cloud Uplink               ║
 * ║                                                                           ║
 * ║  Hardware: Heltec LoRa32 V2 + WiFi + OLED + SPIFFS                      ║
 * ║  Autor: Agente Implementador | Licencia: MIT                              ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ═══════════════════════════════════════════════════════════════════════════════
// PINES - Heltec LoRa32 V2
// ═══════════════════════════════════════════════════════════════════════════════
#define LORA_SCK    5
#define LORA_MISO   19
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_RST    14
#define LORA_IRQ    26

#define OLED_SDA    4
#define OLED_SCL    15
#define OLED_RST    16
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN WiFi
// ═══════════════════════════════════════════════════════════════════════════════
const char* WIFI_SSID     = "geogo_IoT";
const char* WIFI_PASS     = "2iot2025";

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN SUPABASE (COMPLETAR CON TUS CREDENCIALES)
// ═══════════════════════════════════════════════════════════════════════════════
const char* SUPABASE_URL  = "https://xntyohpdmvsmxpcrgiik.supabase.co";
const char* SUPABASE_KEY  = "sb_publishable_4NAst3SPAzM6LdbdCfKO4g_erAJAeHd";
const char* SUPABASE_TABLE = "autoinver_data";

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN LoRa
// ═══════════════════════════════════════════════════════════════════════════════
#define LORA_FREQ       915E6
#define LORA_SF         7
#define LORA_BW         125E3
#define LORA_CR         5
#define LORA_TXPOWER    17

// ═══════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN SISTEMA
// ═══════════════════════════════════════════════════════════════════════════════
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_SEC  (-4 * 3600)  // GMT-4 Chile

// Intervalos
#define NTP_SYNC_MS     300000   // 5 minutos
#define SUPABASE_SEND_MS 30000   // 30 segundos
#define CMD_POLL_MS     15000    // 15 segundos poll comandos
#define DISPLAY_UPD_MS  1000     // 1 segundo
#define WIFI_RETRY_MS   30000    // 30 segundos reintento WiFi

// Buffer
#define BUFFER_FILE     "/buffer.jsonl"
#define MAX_BUFFER_LINES 500

// ═══════════════════════════════════════════════════════════════════════════════
// OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════════════════════
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RST);
ESP32Time rtc(0);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, GMT_OFFSET_SEC, 60000);
WebServer server(80);
WiFiClientSecure wifiSecure;

// ═══════════════════════════════════════════════════════════════════════════════
// ESTADOS DEL SISTEMA
// ═══════════════════════════════════════════════════════════════════════════════
enum ConnState {
  CONN_NONE,
  CONN_WIFI_OK,
  CONN_INTERNET_OK,
  CONN_SUPABASE_OK
};

// ═══════════════════════════════════════════════════════════════════════════════
// DATOS DEL NODO (último paquete recibido)
// ═══════════════════════════════════════════════════════════════════════════════
struct NodeData {
  String   nodeID     = "--";
  String   timestamp  = "--";
  float    temp       = 0;
  float    hum        = 0;
  float    soilPct    = 0;
  float    waterPct   = 0;
  int      soilADC    = 0;
  int      waterADC   = 0;
  int      s1         = 0;
  int      s2         = 0;
  bool     pump       = false;
  String   mode       = "AUTO";
  int      rssi       = 0;
  int      packetCnt  = 0;
  bool     valid      = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ═══════════════════════════════════════════════════════════════════════════════
NodeData      g_node;
ConnState     g_connState = CONN_NONE;
unsigned long g_lastPacketMs = 0;
unsigned long g_lastNtpSync  = 0;
unsigned long g_lastSupabaseSend = 0;
unsigned long g_lastCmdPoll  = 0;
unsigned long g_lastDisplay  = 0;
unsigned long g_lastWifiRetry = 0;
int           g_totalRxPackets = 0;
int           g_totalTxCommands = 0;
int           g_bufferLines = 0;
bool          g_wifiConnected = false;
bool          g_ntpSynced = false;

// ═══════════════════════════════════════════════════════════════════════════════
// DECLARACIONES
// ═══════════════════════════════════════════════════════════════════════════════

// Init
void initDisplay();
void initLoRa();
void initWiFi();
void initSPIFFS();
void initWebServer();
void initNTP();
void showBootScreen();

// LoRa
void procesarPaqueteLoRa();
void enviarHoraNodo();
void enviarComandoLoRa(const String& act, const String& val);
void parsearTelemetria(const String& payload);

// Supabase / Cloud
bool enviarSupabaseDirect(const String& jsonPayload);
void flushBufferToSupabase();
void guardarEnBuffer(const String& jsonLine);
void pollComandosPendientes();

// Web Server
void handleRoot();
void handleApiV1Status();
void handleApiV1Data();
void handleApiV1Command();
void handleApiV1History();
void handleDashboard();
String generateDashboardHTML();

// Display
void actualizarDisplay();
void mostrarBoot();
void mostrarWiFiStatus(int intento);
void mostrarEstadoOK();
void mostrarError(const char* msg);

// Utilidades
String getTimestampStr();
String escapeJson(const String& s);
bool   checkInternetConnection();

// ═══════════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n╔══════════════════════════════════════════╗"));
  Serial.println(F("║    AUTOINVER GATEWAY v2.0 - BOOT        ║"));
  Serial.println(F("╚══════════════════════════════════════════╝"));

  initDisplay();
  showBootScreen();
  delay(1000);

  initSPIFFS();
  initLoRa();
  initWiFi();
  wifiSecure.setInsecure();   // Aceptar cualquier certificado HTTPS (Supabase)
  initNTP();
  initWebServer();

  mostrarEstadoOK();
  delay(2000);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // ── 1. Recibir paquetes LoRa ──
  int pktSize = LoRa.parsePacket();
  if (pktSize) {
    procesarPaqueteLoRa();
  }

  // ── 2. Reconexión WiFi automática ──
  if (!g_wifiConnected && (now - g_lastWifiRetry > WIFI_RETRY_MS)) {
    g_lastWifiRetry = now;
    Serial.println(F("[WIFI] Reintentando conexión..."));
    WiFi.reconnect();
  }
  
  if (WiFi.status() == WL_CONNECTED && !g_wifiConnected) {
    g_wifiConnected = true;
    g_connState = CONN_WIFI_OK;
    Serial.print(F("[WIFI] Conectado. IP: "));
    Serial.println(WiFi.localIP());
  } else if (WiFi.status() != WL_CONNECTED && g_wifiConnected) {
    g_wifiConnected = false;
    g_connState = CONN_NONE;
  }

  // ── 3. Sincronización NTP ──
  if (g_wifiConnected && (now - g_lastNtpSync > NTP_SYNC_MS)) {
    g_lastNtpSync = now;
    if (timeClient.update()) {
      rtc.setTime(timeClient.getEpochTime());
      g_ntpSynced = true;
      Serial.println(F("[NTP] Hora sincronizada"));
    }
  }

  // ── 4. Enviar datos a Supabase cada 30s ──
  if (g_wifiConnected && g_ntpSynced && (now - g_lastSupabaseSend > SUPABASE_SEND_MS)) {
    g_lastSupabaseSend = now;
    flushBufferToSupabase();
    
    // También enviar el dato actual si es reciente (< 60s)
    if (g_node.valid && (now - g_lastPacketMs < 60000)) {
      StaticJsonDocument<512> doc;
      doc["device_id"] = "AUTOINVER";
      doc["temp_c"] = g_node.temp;
      doc["hum_pct"] = g_node.hum;
      doc["soil_adc"] = g_node.soilADC;
      doc["soil_pct"] = g_node.soilPct;
      doc["water_adc"] = g_node.waterADC;
      doc["water_pct"] = g_node.waterPct;
      doc["servo1_pos"] = g_node.s1;
      doc["servo2_pos"] = g_node.s2;
      doc["pump_state"] = g_node.pump;
      doc["rssi"] = g_node.rssi;
      doc["raw_payload"] = "live";
      
      String jsonStr;
      serializeJson(doc, jsonStr);
      if (!enviarSupabaseDirect(jsonStr)) {
        guardarEnBuffer(jsonStr);
      }
    }
  }

  // ── 5. Poll comandos pendientes desde Supabase ──
  if (g_wifiConnected && g_ntpSynced && (now - g_lastCmdPoll > CMD_POLL_MS)) {
    g_lastCmdPoll = now;
    pollComandosPendientes();
  }

  // ── 6. Web server ──
  server.handleClient();

  // ── 7. Display OLED ──
  if (now - g_lastDisplay > DISPLAY_UPD_MS) {
    g_lastDisplay = now;
    actualizarDisplay();
  }

  delay(1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// INICIALIZACIÓN
// ═══════════════════════════════════════════════════════════════════════════════

void initDisplay() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(50);
  digitalWrite(OLED_RST, HIGH); delay(50);
  Wire.begin(OLED_SDA, OLED_SCL);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[ERR] OLED init failed"));
    while (true) delay(1000);
  }
  display.clearDisplay();
}

void initLoRa() {
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_FREQ)) {
    mostrarError("LoRa FAIL");
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TXPOWER);
  LoRa.enableCrc();
  Serial.println(F("[OK] LoRa iniciado"));
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  Serial.print(F("[WIFI] Conectando a "));
  Serial.println(WIFI_SSID);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 40) {
    delay(500);
    intentos++;
    mostrarWiFiStatus(intentos);
  }
  
  g_wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (g_wifiConnected) {
    g_connState = CONN_WIFI_OK;
    Serial.print(F("[OK] WiFi conectado. IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WARN] WiFi no conectado, operando offline"));
  }
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println(F("[WARN] SPIFFS init failed"));
  } else {
    // Contar líneas existentes en buffer
    File f = SPIFFS.open(BUFFER_FILE, "r");
    if (f) {
      while (f.available()) {
        if (f.read() == '\n') g_bufferLines++;
      }
      f.close();
    }
    Serial.print(F("[OK] SPIFFS listo. Buffer: "));
    Serial.print(g_bufferLines);
    Serial.println(F(" líneas"));
  }
}

void initNTP() {
  if (g_wifiConnected) {
    timeClient.begin();
    if (timeClient.update()) {
      rtc.setTime(timeClient.getEpochTime());
      g_ntpSynced = true;
      g_lastNtpSync = millis();
      Serial.println(F("[OK] NTP sincronizado"));
    }
  }
}

void initWebServer() {
  // API REST v1
  server.on("/", HTTP_GET, handleRoot);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/api/v1/status", HTTP_GET, handleApiV1Status);
  server.on("/api/v1/data", HTTP_GET, handleApiV1Data);
  server.on("/api/v1/command", HTTP_POST, handleApiV1Command);
  server.on("/api/v1/history", HTTP_GET, handleApiV1History);
  
  server.begin();
  Serial.println(F("[OK] Web server iniciado en puerto 80"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// LORA - PROCESAMIENTO DE PAQUETES
// ═══════════════════════════════════════════════════════════════════════════════

void procesarPaqueteLoRa() {
  String rx = "";
  while (LoRa.available()) {
    rx += (char)LoRa.read();
  }
  
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  Serial.print(F("[RX] RSSI=")); Serial.print(rssi);
  Serial.print(F(" SNR=")); Serial.println(snr);
  Serial.println(rx);

  // ── Solicitud de hora del nodo ──
  if (rx.indexOf("REQ:HORA") >= 0) {
    enviarHoraNodo();
    return;
  }

  // ── Telemetría del nodo ──
  if (rx.indexOf("ID:AUTOINVER") >= 0) {
    g_totalRxPackets++;
    g_lastPacketMs = millis();
    g_node.rssi = rssi;
    parsearTelemetria(rx);
    g_node.valid = true;

    // Guardar en buffer local
    StaticJsonDocument<512> doc;
    doc["device_id"] = "AUTOINVER";
    doc["temp_c"] = g_node.temp;
    doc["hum_pct"] = g_node.hum;
    doc["soil_adc"] = g_node.soilADC;
    doc["soil_pct"] = g_node.soilPct;
    doc["water_adc"] = g_node.waterADC;
    doc["water_pct"] = g_node.waterPct;
    doc["servo1_pos"] = g_node.s1;
    doc["servo2_pos"] = g_node.s2;
    doc["pump_state"] = g_node.pump;
    doc["rssi"] = rssi;
    doc["raw_payload"] = rx;

    String jsonLine;
    serializeJson(doc, jsonLine);
    guardarEnBuffer(jsonLine);
  }
}

void enviarHoraNodo() {
  String ts = getTimestampStr();
  String payload = "SYNC:HORA|" + ts;
  
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  
  Serial.print(F("[TX-SYNC] "));
  Serial.println(payload);
}

void enviarComandoLoRa(const String& act, const String& val) {
  String ts = getTimestampStr();
  String cmd = "CMD:AUTOINVER|ACT:" + act + "|VAL:" + val + "|TS:" + ts;
  
  LoRa.beginPacket();
  LoRa.print(cmd);
  LoRa.endPacket();
  
  g_totalTxCommands++;
  Serial.print(F("[TX-CMD] "));
  Serial.println(cmd);
}

void parsearTelemetria(const String& p) {
  // Helper lambda para extraer campo
  auto getField = [&](const char* key) -> String {
    String search = String(key) + ":";
    int i1 = p.indexOf(search);
    if (i1 < 0) return "";
    i1 += search.length();
    int i2 = p.indexOf("|", i1);
    if (i2 < 0) i2 = p.length();
    return p.substring(i1, i2);
  };

  g_node.nodeID    = getField("ID");
  g_node.timestamp = getField("TS");
  
  String t = getField("TEMP");   if (t.length()) g_node.temp = t.toFloat();
  String h = getField("HUM");    if (h.length()) g_node.hum = h.toFloat();
  String s = getField("SOIL");   if (s.length()) g_node.soilADC = s.toInt();
  String w = getField("WATER");  if (w.length()) g_node.waterADC = w.toInt();
  String wl = getField("WLVL");  if (wl.length()) g_node.waterPct = wl.toFloat();
  String s1 = getField("S1");    if (s1.length()) g_node.s1 = s1.toInt();
  String s2 = getField("S2");    if (s2.length()) g_node.s2 = s2.toInt();
  String pm = getField("PUMP");  if (pm.length()) g_node.pump = (pm == "1");
  String md = getField("MODE");  if (md.length()) g_node.mode = md;
  String cnt = getField("CNT");  if (cnt.length()) g_node.packetCnt = cnt.toInt();
  
  // Calcular soil_pct si no viene en payload (fallback)
  g_node.soilPct = map(constrain(g_node.soilADC, 0, 4095), 0, 4095, 0, 100);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SUPABASE / CLOUD UPLINK
// ═══════════════════════════════════════════════════════════════════════════════

bool enviarSupabaseDirect(const String& jsonPayload) {
  if (!g_wifiConnected) {
    Serial.println(F("[SUPA] Skip: no WiFi"));
    return false;
  }

  String payload = jsonPayload;
  // Limpiar 'ts_local' de líneas antiguas del buffer (columna no existe en Supabase)
  if (payload.indexOf("\"ts_local\"") >= 0) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      doc.remove("ts_local");
      payload = "";
      serializeJson(doc, payload);
    }
  }

  HTTPClient https;
  String url = String(SUPABASE_URL) + "/rest/v1/" + SUPABASE_TABLE;

  https.setTimeout(15000);  // 15 seg timeout
  if (!https.begin(wifiSecure, url)) {
    Serial.println(F("[SUPA] Error: no se pudo iniciar conexion HTTPS"));
    return false;
  }

  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");

  int httpCode = https.POST(payload);

  if (httpCode == 201 || httpCode == 200) {
    Serial.println(F("[SUPA] Dato enviado OK"));
    https.end();
    return true;
  } else {
    Serial.print(F("[SUPA] Error HTTP "));
    Serial.print(httpCode);
    if (httpCode == -1) Serial.println(F(" (-1 = fallo SSL/TLS o DNS)"));
    else if (httpCode == -11) Serial.println(F(" (timeout)"));
    else {
      // Imprimir respuesta del servidor (PostgREST devuelve el motivo en JSON)
      String resp = https.getString();
      Serial.print(F(" -> "));
      Serial.println(resp);
      // También imprimir el payload enviado, para comparar
      Serial.print(F("[SUPA] Payload: "));
      Serial.println(payload);
    }
    https.end();
    return false;
  }
}

void guardarEnBuffer(const String& jsonLine) {
  if (g_bufferLines >= MAX_BUFFER_LINES) {
    // Rotación: eliminar primera línea
    File f = SPIFFS.open(BUFFER_FILE, "r");
    if (!f) return;
    
    String resto = "";
    bool primera = true;
    while (f.available()) {
      String linea = f.readStringUntil('\n');
      if (primera) { primera = false; continue; }
      resto += linea + "\n";
    }
    f.close();
    
    SPIFFS.remove(BUFFER_FILE);
    File f2 = SPIFFS.open(BUFFER_FILE, "w");
    if (f2) { f2.print(resto); f2.close(); }
    g_bufferLines--;
  }

  File f = SPIFFS.open(BUFFER_FILE, FILE_APPEND);
  if (f) {
    f.println(jsonLine);
    f.close();
    g_bufferLines++;
  }
}

void flushBufferToSupabase() {
  if (!g_wifiConnected || g_bufferLines == 0) return;

  File f = SPIFFS.open(BUFFER_FILE, "r");
  if (!f) return;

  String nuevoBuffer = "";
  int enviados = 0;
  int fallidos = 0;

  while (f.available()) {
    String linea = f.readStringUntil('\n');
    if (linea.length() < 10) continue;

    if (enviarSupabaseDirect(linea)) {
      enviados++;
      delay(50);  // Rate limiting amigable
    } else {
      fallidos++;
      nuevoBuffer += linea + "\n";
    }
  }
  f.close();

  // Reescribir buffer con los fallidos
  SPIFFS.remove(BUFFER_FILE);
  if (nuevoBuffer.length() > 0) {
    File f2 = SPIFFS.open(BUFFER_FILE, "w");
    if (f2) { f2.print(nuevoBuffer); f2.close(); }
  }
  g_bufferLines = fallidos;

  Serial.print(F("[BUF] Flush: "));
  Serial.print(enviados);
  Serial.print(F(" enviados, "));
  Serial.print(fallidos);
  Serial.println(F(" pendientes"));
}

void pollComandosPendientes() {
  // NOTA: Implementar cuando se cree la tabla commands_queue en Supabase
  // Por ahora, placeholder para futura expansión
  // El gateway puede hacer GET a una vista que filtre comandos no ejecutados
  // y marcarlos como ejecutados vía PATCH
}

// ═══════════════════════════════════════════════════════════════════════════════
// WEB SERVER - ENDPOINTS
// ═══════════════════════════════════════════════════════════════════════════════

void handleRoot() {
  server.sendHeader("Location", "/dashboard");
  server.send(302, "text/plain", "Redirecting to dashboard...");
}

void handleDashboard() {
  String html = generateDashboardHTML();
  server.send(200, "text/html", html);
}

void handleApiV1Status() {
  StaticJsonDocument<512> doc;
  doc["gateway_uptime_ms"] = millis();
  doc["wifi_connected"] = g_wifiConnected;
  doc["ntp_synced"] = g_ntpSynced;
  doc["local_ip"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["buffer_lines"] = g_bufferLines;
  doc["total_rx_packets"] = g_totalRxPackets;
  doc["total_tx_commands"] = g_totalTxCommands;
  doc["lora_freq"] = LORA_FREQ / 1E6;
  doc["lora_sf"] = LORA_SF;
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiV1Data() {
  StaticJsonDocument<512> doc;
  if (g_node.valid) {
    doc["node_id"] = g_node.nodeID;
    doc["timestamp"] = g_node.timestamp;
    doc["temp_c"] = g_node.temp;
    doc["hum_pct"] = g_node.hum;
    doc["soil_pct"] = g_node.soilPct;
    doc["water_pct"] = g_node.waterPct;
    doc["servo1"] = g_node.s1;
    doc["servo2"] = g_node.s2;
    doc["pump"] = g_node.pump;
    doc["mode"] = g_node.mode;
    doc["rssi"] = g_node.rssi;
    doc["packet_count"] = g_node.packetCnt;
    doc["last_seen_sec"] = (millis() - g_lastPacketMs) / 1000;
    doc["has_data"] = true;
  } else {
    doc["has_data"] = false;
  }
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiV1Command() {
  if (!server.hasArg("act") || !server.hasArg("val")) {
    server.send(400, "application/json", "{\"error\":\"Missing act/val\"}");
    return;
  }
  
  String act = server.arg("act");
  String val = server.arg("val");
  
  // Validar acciones permitidas
  if (act != "BOMBA" && act != "S1" && act != "S2" && act != "MODE") {
    server.send(400, "application/json", "{\"error\":\"Invalid act\"}");
    return;
  }
  
  enviarComandoLoRa(act, val);
  
  StaticJsonDocument<256> doc;
  doc["status"] = "sent";
  doc["action"] = act;
  doc["value"] = val;
  doc["timestamp"] = getTimestampStr();
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiV1History() {
  // Devolver últimas líneas del buffer local
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
  if (limit > 200) limit = 200;
  
  String result = "[\n";
  File f = SPIFFS.open(BUFFER_FILE, "r");
  if (f) {
    int count = 0;
    while (f.available() && count < limit) {
      String line = f.readStringUntil('\n');
      if (line.length() < 5) continue;
      if (count > 0) result += ",\n";
      result += line;
      count++;
    }
    f.close();
  }
  result += "\n]";
  
  server.send(200, "application/json", result);
}

// ═══════════════════════════════════════════════════════════════════════════════
// WEB SERVER - DASHBOARD HTML EMBEBIDO PROFESIONAL
// ═══════════════════════════════════════════════════════════════════════════════

String generateDashboardHTML() {
  unsigned long sinceLast = g_node.valid ? (millis() - g_lastPacketMs) / 1000 : 999;
  String connColor = sinceLast < 30 ? "#10B981" : sinceLast < 120 ? "#F59E0B" : "#EF4444";
  String connText = sinceLast < 60 ? String(sinceLast) + "s" : String(sinceLast / 60) + "m";
  
  // Pre-calcular valores condicionales para evitar ternarios dentro del raw literal
  String pumpClass = g_node.pump ? "pump-on" : "pump-off";
  String pumpText  = g_node.pump ? "ON" : "OFF";
  String wifiText  = g_wifiConnected ? "Conectado" : "Offline";
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Autoinver Gateway</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
    color: #e2e8f0;
    min-height: 100vh;
    padding: 20px;
  }
  .container { max-width: 1200px; margin: 0 auto; }
  header {
    text-align: center;
    margin-bottom: 24px;
  }
  header h1 {
    font-size: 2rem;
    background: linear-gradient(90deg, #10B981, #3B82F6);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    margin-bottom: 8px;
  }
  .status-bar {
    display: flex;
    justify-content: center;
    gap: 16px;
    flex-wrap: wrap;
    margin-bottom: 20px;
  }
  .status-pill {
    background: rgba(255,255,255,0.08);
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 20px;
    padding: 6px 16px;
    font-size: 0.85rem;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: )rawliteral" + connColor + R"rawliteral(;
    animation: pulse 2s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 16px;
    margin-bottom: 24px;
  }
  .card {
    background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 16px;
    padding: 20px;
    backdrop-filter: blur(10px);
    transition: transform 0.2s, box-shadow 0.2s;
  }
  .card:hover {
    transform: translateY(-2px);
    box-shadow: 0 8px 32px rgba(0,0,0,0.2);
  }
  .card-label {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #94a3b8;
    margin-bottom: 8px;
  }
  .card-value {
    font-size: 2rem;
    font-weight: 700;
    color: #f8fafc;
  }
  .card-unit {
    font-size: 0.9rem;
    color: #64748b;
    margin-left: 4px;
  }
  .temp { color: #f59e0b; }
  .hum { color: #3b82f6; }
  .soil { color: #10b981; }
  .water { color: #06b6d4; }
  .pump-on { color: #ef4444; }
  .pump-off { color: #64748b; }
  
  .controls {
    background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 16px;
    padding: 20px;
    margin-bottom: 24px;
  }
  .controls h3 {
    margin-bottom: 16px;
    color: #f8fafc;
  }
  .btn-row {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
    margin-bottom: 12px;
  }
  button {
    background: linear-gradient(135deg, #10B981, #059669);
    color: white;
    border: none;
    padding: 12px 24px;
    border-radius: 10px;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
  }
  button:hover {
    transform: scale(1.05);
    box-shadow: 0 4px 16px rgba(16, 185, 129, 0.3);
  }
  button.danger {
    background: linear-gradient(135deg, #EF4444, #DC2626);
  }
  button.secondary {
    background: linear-gradient(135deg, #3B82F6, #2563EB);
  }
  .info-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 12px;
    font-size: 0.85rem;
    color: #94a3b8;
  }
  .refresh-hint {
    text-align: center;
    color: #64748b;
    font-size: 0.8rem;
    margin-top: 20px;
  }
  @media (max-width: 480px) {
    .grid { grid-template-columns: 1fr; }
    header h1 { font-size: 1.4rem; }
  }
</style>
</head>
<body>
<div class="container">
  <header>
    <h1>🌿 Autoinver Gateway</h1>
    <p style="color:#64748b;">Panel de control local</p>
  </header>
  
  <div class="status-bar">
    <div class="status-pill">
      <span class="status-dot"></span>
      <span>Último paquete: )rawliteral" + connText + R"rawliteral(</span>
    </div>
    <div class="status-pill">
      <span>📶 RSSI: )rawliteral" + String(g_node.rssi) + R"rawliteral(dBm</span>
    </div>
    <div class="status-pill">
      <span>📦 Paquetes: )rawliteral" + String(g_totalRxPackets) + R"rawliteral(</span>
    </div>
    <div class="status-pill">
      <span>🌐 IP: )rawliteral" + WiFi.localIP().toString() + R"rawliteral(</span>
    </div>
  </div>

  <div class="grid">
    <div class="card">
      <div class="card-label">Temperatura</div>
      <div class="card-value temp">)rawliteral" + String(g_node.temp, 1) + R"rawliteral(<span class="card-unit">°C</span></div>
    </div>
    <div class="card">
      <div class="card-label">Humedad</div>
      <div class="card-value hum">)rawliteral" + String(g_node.hum, 1) + R"rawliteral(<span class="card-unit">%</span></div>
    </div>
    <div class="card">
      <div class="card-label">Suelo</div>
      <div class="card-value soil">)rawliteral" + String(g_node.soilPct, 0) + R"rawliteral(<span class="card-unit">%</span></div>
    </div>
    <div class="card">
      <div class="card-label">Nivel Agua</div>
      <div class="card-value water">)rawliteral" + String(g_node.waterPct, 0) + R"rawliteral(<span class="card-unit">%</span></div>
    </div>
    <div class="card">
      <div class="card-label">Bomba</div>
      <div class="card-value )rawliteral" + pumpClass + R"rawliteral(">)rawliteral" + pumpText + R"rawliteral(</span></div>
    </div>
    <div class="card">
      <div class="card-label">Modo</div>
      <div class="card-value" style="font-size:1.4rem;">)rawliteral" + g_node.mode + R"rawliteral(</div>
    </div>
  </div>

  <div class="controls">
    <h3>🎮 Control Manual</h3>
    <div class="btn-row">
      <button onclick="sendCommand('BOMBA','1')">💧 Bomba ON</button>
      <button onclick="sendCommand('BOMBA','0')" class="danger">🛑 Bomba OFF</button>
      <button onclick="sendCommand('MODE','AUTO')" class="secondary">🤖 Auto</button>
    </div>
    <div class="btn-row">
      <button onclick="sendCommand('S1','0')">🔒 Cerrar V1</button>
      <button onclick="sendCommand('S1','90')">🔓 Abrir V1</button>
      <button onclick="sendCommand('S2','0')">🔒 Cerrar V2</button>
      <button onclick="sendCommand('S2','90')">🔓 Abrir V2</button>
    </div>
  </div>
  <div id="toast" style="position:fixed;bottom:24px;right:24px;background:#10b981;color:#fff;padding:14px 22px;border-radius:12px;font-weight:600;box-shadow:0 8px 24px rgba(0,0,0,0.3);opacity:0;transform:translateY(20px);transition:all .3s ease;z-index:9999;">Comando enviado ✅</div>

  <div class="controls">
    <h3>📊 Información del Sistema</h3>
    <div class="info-grid">
      <div>📡 LoRa: 915MHz SF7</div>
      <div>💾 Buffer: )rawliteral" + String(g_bufferLines) + R"rawliteral( líneas</div>
      <div>🕐 Hora: )rawliteral" + getTimestampStr() + R"rawliteral(</div>
      <div>🔧 Heap libre: )rawliteral" + String(ESP.getFreeHeap()) + R"rawliteral(</div>
      <div>📤 Comandos enviados: )rawliteral" + String(g_totalTxCommands) + R"rawliteral(</div>
      <div>🔌 WiFi: )rawliteral" + wifiText + R"rawliteral(</div>
    </div>
  </div>

  <p class="refresh-hint">La página se actualiza automáticamente cada 5 segundos</p>
</div>
<script>
  async function sendCommand(act, val) {
    try {
      const r = await fetch('/api/v1/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'act=' + encodeURIComponent(act) + '&val=' + encodeURIComponent(val)
      });
      const data = await r.json();
      if (data.status === 'sent') showToast('Comando enviado ✅');
      else showToast('Error al enviar ❌');
    } catch(e) {
      showToast('Error de red ❌');
    }
  }
  function showToast(msg) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.style.opacity = '1';
    t.style.transform = 'translateY(0)';
    setTimeout(() => { t.style.opacity='0'; t.style.transform='translateY(20px)'; }, 3000);
  }
  setTimeout(() => location.reload(), 5000);
</script>
</body>
</html>
)rawliteral";

  return html;
}
// DISPLAY OLED
// ═══════════════════════════════════════════════════════════════════════════════

void actualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Barra superior
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(F("GW-Autoinver"));
  
  // Indicadores de conexión
  display.setCursor(75, 1);
  display.print(g_wifiConnected ? "W" : "-");
  display.print(g_ntpSynced ? "N" : "-");
  display.print(g_bufferLines > 0 ? "B" : "-");
  
  // Hora
  display.setCursor(96, 1);
  int h = rtc.getHour(true);
  int m = rtc.getMinute();
  if (h < 10) display.print("0"); display.print(h);
  display.print(":");
  if (m < 10) display.print("0"); display.print(m);

  // Datos nodo
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.print(F("Nodo:"));
  display.print(g_node.nodeID);

  display.setCursor(0, 22);
  display.print(F("T:"));
  display.print(g_node.temp, 1);
  display.print(F("C H:"));
  display.print(g_node.hum, 1);
  display.print("%");

  display.setCursor(0, 32);
  display.print(F("SL:"));
  display.print(g_node.soilPct, 0);
  display.print(F("% WT:"));
  display.print(g_node.waterPct, 0);
  display.print("%");

  display.setCursor(0, 42);
  display.print(F("B:"));
  display.print(g_node.pump ? "ON " : "OFF ");
  display.print(F("M:"));
  display.print(g_node.mode);

  display.setCursor(0, 52);
  display.print(F("R:"));
  display.print(g_node.rssi);
  display.print(F("dBm "));
  
  if (g_node.valid) {
    unsigned long s = (millis() - g_lastPacketMs) / 1000;
    if (s < 60) { display.print(s); display.print("s"); }
    else { display.print(s / 60); display.print("m"); }
  } else {
    display.print(F("SIN RX"));
  }

  display.display();
}

void showBootScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 10);
  display.print(F("AUTOINVER GATEWAY"));
  display.setCursor(28, 25);
  display.print(F("v2.0"));
  display.drawLine(4, 38, SCREEN_W - 4, 38, SSD1306_WHITE);
  display.setCursor(8, 44);
  display.print(F("WiFi + LoRa + Web"));
  display.setCursor(8, 55);
  display.print(F("Supabase Bridge"));
  display.display();
}

void mostrarWiFiStatus(int intento) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Conectando WiFi"));
  display.setCursor(0, 20);
  display.print(WIFI_SSID);
  display.setCursor(0, 40);
  display.print(F("Intento "));
  display.print(intento);
  display.display();
}

void mostrarEstadoOK() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("GW OK"));
  display.setCursor(0, 15);
  display.print(g_wifiConnected ? F("WiFi:OK") : F("WiFi:--"));
  display.setCursor(0, 28);
  display.print(g_ntpSynced ? F("NTP:OK") : F("NTP:--"));
  display.setCursor(0, 41);
  display.print(F("IP:"));
  display.print(WiFi.localIP());
  display.setCursor(0, 54);
  display.print(F("Web:80 LoRa:915M"));
  display.display();
}

void mostrarError(const char* msg) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print(F("ERROR:"));
  display.setCursor(0, 35);
  display.print(msg);
  display.display();
}

// ═══════════════════════════════════════════════════════════════════════════════
// UTILIDADES
// ═══════════════════════════════════════════════════════════════════════════════

String getTimestampStr() {
  String t = String(rtc.getYear()) + "-";
  int mo = rtc.getMonth() + 1;
  int d = rtc.getDay();
  int h = rtc.getHour(true);
  int mi = rtc.getMinute();
  int s = rtc.getSecond();

  if (mo < 10) t += "0"; t += String(mo) + "-";
  if (d < 10)  t += "0"; t += String(d) + "_";
  if (h < 10)  t += "0"; t += String(h) + ":";
  if (mi < 10) t += "0"; t += String(mi) + ":";
  if (s < 10)  t += "0"; t += String(s);
  
  return t;
}
