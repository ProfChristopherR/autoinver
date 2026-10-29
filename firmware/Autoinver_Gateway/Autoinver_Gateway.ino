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
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <DNSServer.h>
#include <Preferences.h>
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
// CONFIGURACIÓN WiFi & PORTAL CAUTIVO
// ═══════════════════════════════════════════════════════════════════════════════
// Red 1: Prioridad máxima nativa
const char* WIFI_SSID_1       = "geogo_IoT";
const char* WIFI_PASS_1       = "2iot2025";

// Red 2: Segunda red de respaldo fija (Hotspot móvil)
const char* WIFI_SSID_2       = "amarox_555";
const char* WIFI_PASS_2       = "queteimporta123";

// Alias de compatibilidad
const char* WIFI_SSID_DEFAULT = WIFI_SSID_1;
const char* WIFI_PASS_DEFAULT = WIFI_PASS_1;

const char* AP_SSID           = "Autoinver_Gateway_AP";
const char* AP_PASS           = "";  // Red abierta para facilitar acceso al portal cautivo

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
#define CMD_POLL_MS     2000     // 2 segundos poll comandos (respuesta rápida desde dashboard)
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
bool          g_apMode = false;
String        g_configuredSSID = "";
String        g_configuredPass = "";
String        g_adminUser = "admin";
String        g_adminPass = "admin";
String        g_currentConnectedSSID = "";

DNSServer     g_dnsServer;
const byte    DNS_PORT = 53;
IPAddress     g_apIP(192, 168, 4, 1);
IPAddress     g_apNetmask(255, 255, 255, 0);
Preferences   g_prefs;

// ═══════════════════════════════════════════════════════════════════════════════
// DECLARACIONES
// ═══════════════════════════════════════════════════════════════════════════════

// Init & WiFi Management
void initDisplay();
void initLoRa();
void initWiFi();
void initSPIFFS();
void initWebServer();
void initNTP();
void showBootScreen();
void loadConfig();
void saveNetworkConfig(const String& ssid, const String& pass);
void saveAdminConfig(const String& user, const String& pass);
bool conectarWiFi(const char* ssid, const char* pass, int maxIntentos);
void conectarWiFiCiclo();
void iniciarPortalCautivo();
void detenerPortalCautivo();

// LoRa
void procesarPaqueteLoRa();
void enviarHoraNodo();
void enviarComandoLoRa(const String& act, const String& val);
void parsearTelemetria(const String& payload);

// Supabase / Cloud
bool enviarSupabaseDirect(const String& jsonPayload);
bool enviarSupabaseBatch(String* lineas, int count);
bool enviarTelemetriaActualSupabase();
void flushBufferToSupabase();
void guardarEnBuffer(const String& jsonLine);
void pollComandosPendientes();
void ackComando(long id);

// Web Server & Portal Cautivo
void sendCorsHeaders();
void handleRoot();
void handleApiV1Status();
void handleApiV1Data();
void handleApiV1Command();
void handleApiV1History();
void handleDashboard();
String generateDashboardHTML();
bool isPortalAuthenticated();
void handleCaptivePortalRedirect();
void handleNotFound();
void handlePortal();
void handlePortalLogin();
void handlePortalLogout();
void handlePortalScan();
void handlePortalSave();
void handlePortalAdminChange();
void handlePortalReboot();
String generatePortalHTML(bool auth, const String& err = "", const String& succ = "");

// Display
void actualizarDisplay();
void mostrarBoot();
void mostrarWiFiStatus(const char* ssid, int intento);
void mostrarAPStatus();
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

  // ── 0. Procesar peticiones DNS en modo Portal Cautivo ──
  if (g_apMode) {
    g_dnsServer.processNextRequest();
  }

  // ── 1. Recibir paquetes LoRa ──
  int pktSize = LoRa.parsePacket();
  if (pktSize) {
    procesarPaqueteLoRa();
  }

  // ── 2. Reconexión WiFi automática (prioridad geogo_IoT primero) ──
  if (!g_apMode && !g_wifiConnected && (now - g_lastWifiRetry > WIFI_RETRY_MS)) {
    g_lastWifiRetry = now;
    Serial.println(F("[WIFI] Reintentando conexión (prioridad geogo_IoT)..."));
    conectarWiFiCiclo();
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

  // ── 3. Sincronización NTP (reintento cada 10s si aún no sincroniza, o cada 5min si ya sincronizó) ──
  unsigned long ntpInterval = g_ntpSynced ? NTP_SYNC_MS : 10000;
  if (g_wifiConnected && (now - g_lastNtpSync > ntpInterval)) {
    g_lastNtpSync = now;
    if (timeClient.update()) {
      rtc.setTime(timeClient.getEpochTime());
      g_ntpSynced = true;
      Serial.println(F("[NTP] ✅ Hora sincronizada con éxito"));
    } else {
      Serial.println(F("[NTP] ⚠️ Intento de sincronización falló (esperando respuesta NTP...)"));
    }
  }

  // ── 4. Enviar datos a Supabase cada 30s con DIAGNÓSTICO COMPLETO ──
  if (now - g_lastSupabaseSend > SUPABASE_SEND_MS) {
    g_lastSupabaseSend = now;

    Serial.println(F("\n══════════ [DIAGNOSTICO UPLINK SUPABASE] ══════════"));
    Serial.print(F(" WiFi Conectado : "));
    if (g_wifiConnected) {
      Serial.print(F("SI -> IP: ")); Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("NO (Modo Desconectado / AP)"));
    }

    Serial.print(F(" NTP Sincronizado: "));
    Serial.println(g_ntpSynced ? F("SI") : F("NO (No impide enviar a Supabase)"));

    Serial.print(F(" Paquete LoRa Nodo: "));
    if (g_node.valid) {
      unsigned long segs = (now - g_lastPacketMs) / 1000;
      Serial.print(F("SI -> Recibido hace ")); Serial.print(segs); Serial.println(F(" s"));
      Serial.print(F("    [T:")); Serial.print(g_node.temp, 1);
      Serial.print(F("C | H:")); Serial.print(g_node.hum, 1);
      Serial.print(F("% | Suelo:")); Serial.print(g_node.soilPct, 0);
      Serial.print(F("% | Agua:")); Serial.print(g_node.waterPct, 0);
      Serial.print(F("% | Bomba:")); Serial.print(g_node.pump ? F("ON") : F("OFF"));
      Serial.print(F(" | RSSI:")); Serial.print(g_node.rssi); Serial.println(F("dBm]"));
    } else {
      Serial.println(F("NO (¡AUN NO SE HA RECIBIDO NINGUN PAQUETE LoRa DEL NODO!)"));
      Serial.println(F("    -> Verifica que el Heltec Nodo este encendido y transmitiendo en 915MHz."));
    }

    Serial.print(F(" Buffer local (SPIFFS): "));
    Serial.print(g_bufferLines); Serial.println(F(" registros"));

    if (!g_wifiConnected) {
      Serial.println(F("[SUPA] ⏸ OMITIDO: El Gateway no tiene conexion WiFi"));
    } else {
      // 1. Enviar buffer acumulado si existe
      if (g_bufferLines > 0) {
        Serial.println(F("[SUPA] 📦 Vaciando registros acumulados en buffer hacia Supabase..."));
        flushBufferToSupabase();
      }

      // 2. Enviar dato actual del nodo si es reciente
      if (g_node.valid && (now - g_lastPacketMs < 60000)) {
        enviarTelemetriaActualSupabase();
      } else {
        if (!g_node.valid) {
          Serial.println(F("[SUPA] ⏸ OMITIDO ENVIO EN VIVO: 'g_node.valid' es FALSE"));
          Serial.println(F("       CAUSA: El Gateway todavia no ha recibido ningun dato por LoRa desde el Nodo."));
        } else {
          Serial.println(F("[SUPA] ⏸ OMITIDO ENVIO EN VIVO: El ultimo paquete LoRa es de hace mas de 60s"));
        }
      }
    }
    Serial.println(F("═══════════════════════════════════════════════════\n"));
  }

  // ── 5. Poll comandos pendientes desde Supabase (cada 5s) ──
  if (g_wifiConnected && (now - g_lastCmdPoll > CMD_POLL_MS)) {
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

void loadConfig() {
  g_prefs.begin("autoinver_gw", false);
  g_configuredSSID = g_prefs.getString("wifi_ssid", "");
  g_configuredPass = g_prefs.getString("wifi_pass", "");
  g_adminUser      = g_prefs.getString("adm_user", "admin");
  g_adminPass      = g_prefs.getString("adm_pass", "admin");
  g_prefs.end();

  Serial.println(F("[CFG] Configuración cargada desde NVS"));
  Serial.print(F("      Red secundaria: "));
  Serial.println(g_configuredSSID.length() > 0 ? g_configuredSSID : F("(Ninguna)"));
  Serial.print(F("      Admin User: "));
  Serial.println(g_adminUser);
}

void saveNetworkConfig(const String& ssid, const String& pass) {
  g_prefs.begin("autoinver_gw", false);
  g_prefs.putString("wifi_ssid", ssid);
  g_prefs.putString("wifi_pass", pass);
  g_prefs.end();

  g_configuredSSID = ssid;
  g_configuredPass = pass;
  Serial.print(F("[CFG] Red secundaria guardada en NVS: "));
  Serial.println(ssid);
}

void saveAdminConfig(const String& user, const String& pass) {
  g_prefs.begin("autoinver_gw", false);
  g_prefs.putString("adm_user", user);
  g_prefs.putString("adm_pass", pass);
  g_prefs.end();

  g_adminUser = user;
  g_adminPass = pass;
  Serial.println(F("[CFG] Credenciales de admin actualizadas en NVS"));
}

int     g_targetChannel = 0;
uint8_t g_targetBSSID[6] = {0};
bool    g_targetFound   = false;

void resetearWiFiModem() {
  esp_wifi_disconnect();
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(150);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(false);                 // CRÍTICO para Hotspots móviles: evita pérdida de balizas
  esp_wifi_set_country_code("CL", true); // Habilitar canales 1 al 13 comunes en celulares
  WiFi.setAutoReconnect(true);
}

void escanearRedes(const char* buscarSSID) {
  resetearWiFiModem();
  g_targetFound = false;

  Serial.print(F("[SCAN] Buscando redes 2.4GHz en el aire..."));
  int n = WiFi.scanNetworks(false, false, false, 300);
  if (n <= 0) {
    Serial.println(F("\n[SCAN] ⚠️ No se detectó ninguna red 2.4GHz."));
  } else {
    Serial.print(F(" (Se detectaron ")); Serial.print(n); Serial.println(F(" redes):"));
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      int ch = WiFi.channel(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);

      Serial.print(F("  • '")); Serial.print(s);
      Serial.print(F("' | Ch: ")); Serial.print(ch);
      Serial.print(F(" | Señal: ")); Serial.print(rssi); Serial.print(F("dBm | Auth: "));
      if (auth == WIFI_AUTH_OPEN) Serial.print(F("Abierta"));
      else if (auth == WIFI_AUTH_WPA_PSK) Serial.print(F("WPA"));
      else if (auth == WIFI_AUTH_WPA2_PSK) Serial.print(F("WPA2"));
      else if (auth == WIFI_AUTH_WPA_WPA2_PSK) Serial.print(F("WPA/WPA2"));
      else if (auth == WIFI_AUTH_WPA3_PSK) Serial.print(F("WPA3 (Incompatible ESP32)"));
      else Serial.print(F("Otro"));
      Serial.println();

      if (s == buscarSSID) {
        g_targetFound = true;
        g_targetChannel = ch;
        memcpy(g_targetBSSID, WiFi.BSSID(i), 6);
      }
    }
    if (g_targetFound) {
      Serial.print(F("[SCAN] ✅ '")); Serial.print(buscarSSID);
      Serial.print(F("' SÍ está transmitiendo en Canal ")); Serial.println(g_targetChannel);
    } else {
      Serial.print(F("[SCAN] ❌ '")); Serial.print(buscarSSID); Serial.println(F("' NO fue detectada en el escaneo 2.4GHz."));
    }
  }
}

bool conectarWiFi(const char* ssid, const char* pass, int maxIntentos) {
  resetearWiFiModem();

  // Si fue detectada en el escaneo previo, conectar directamente a su Canal y BSSID
  if (g_targetFound && String(ssid) == WIFI_SSID_2) {
    Serial.print(F("[WIFI] Conexión DIRIGIDA rápida a Canal "));
    Serial.print(g_targetChannel);
    Serial.println(F("..."));
    WiFi.begin(ssid, pass, g_targetChannel, g_targetBSSID);
  } else {
    WiFi.begin(ssid, pass);
  }

  Serial.print(F("[WIFI] Conectando a '"));
  Serial.print(ssid);
  Serial.println(F("'..."));

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < maxIntentos) {
    delay(500);
    intentos++;
    mostrarWiFiStatus(ssid, intentos);
  }

  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    Serial.print(F("[WIFI] ✅ Conectado con éxito a '"));
    Serial.print(ssid);
    Serial.print(F("' | IP: "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.print(F("[WIFI] ❌ No conectó a '"));
    Serial.print(ssid);
    Serial.print(F("'. Estado: "));
    if (st == WL_NO_SSID_AVAIL)   Serial.println(F("Red no encontrada (revisa nombre o 2.4GHz)"));
    else if (st == WL_CONNECT_FAILED) Serial.println(F("Clave/Seguridad rechazada"));
    else if (st == WL_DISCONNECTED)   Serial.println(F("Desconectado (Esperando IP/Auth del teléfono)"));
    else { Serial.print(F("Código ")); Serial.println(st); }
    return false;
  }
}

void conectarWiFiCiclo() {
  Serial.println(F("[WIFI] Evaluando conexión con jerarquía de redes..."));

  // 1. Siempre intentar primero con geogo_IoT (prioridad 1 nativa)
  Serial.print(F("[WIFI] Prioridad 1 (geogo_IoT): "));
  Serial.println(WIFI_SSID_1);

  if (conectarWiFi(WIFI_SSID_1, WIFI_PASS_1, 15)) {
    g_wifiConnected = true;
    g_connState = CONN_WIFI_OK;
    g_currentConnectedSSID = WIFI_SSID_1;
    detenerPortalCautivo();
    Serial.print(F("[OK] Conectado a geogo_IoT. IP: "));
    Serial.println(WiFi.localIP());
    return;
  }

  // 2. Si falla geogo_IoT, diagnosticar y probar el hotspot móvil (amarox_555)
  Serial.print(F("[WIFI] Prioridad 2 (Hotspot móvil): "));
  Serial.println(WIFI_SSID_2);
  escanearRedes(WIFI_SSID_2);

  if (conectarWiFi(WIFI_SSID_2, WIFI_PASS_2, 25)) {
    g_wifiConnected = true;
    g_connState = CONN_WIFI_OK;
    g_currentConnectedSSID = WIFI_SSID_2;
    detenerPortalCautivo();
    Serial.print(F("[OK] Conectado a "));
    Serial.print(WIFI_SSID_2);
    Serial.print(F(". IP: "));
    Serial.println(WiFi.localIP());
    return;
  }

  // 3. Si ambas fijas fallan, intentar con la red guardada en NVS desde el portal (prioridad 3)
  if (g_configuredSSID.length() > 0 && g_configuredSSID != WIFI_SSID_1 && g_configuredSSID != WIFI_SSID_2) {
    Serial.print(F("[WIFI] Prioridad 3 (Portal NVS): "));
    Serial.println(g_configuredSSID);

    if (conectarWiFi(g_configuredSSID.c_str(), g_configuredPass.c_str(), 20)) {
      g_wifiConnected = true;
      g_connState = CONN_WIFI_OK;
      g_currentConnectedSSID = g_configuredSSID;
      detenerPortalCautivo();
      Serial.print(F("[OK] Conectado a red NVS: "));
      Serial.println(g_configuredSSID);
      Serial.print(F("     IP: "));
      Serial.println(WiFi.localIP());
      return;
    }
  } else {
    Serial.println(F("[WIFI] Sin red adicional en memoria NVS"));
  }

  // 4. Si ninguna red conecta, iniciar portal cautivo AP
  g_wifiConnected = false;
  g_connState = CONN_NONE;
  g_currentConnectedSSID = "";
  Serial.println(F("[WARN] Ninguna red WiFi disponible. Iniciando Portal Cautivo AP..."));
  iniciarPortalCautivo();
}

void iniciarPortalCautivo() {
  if (g_apMode) return;
  g_apMode = true;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(g_apIP, g_apIP, g_apNetmask);
  WiFi.softAP(AP_SSID, AP_PASS);

  g_dnsServer.start(DNS_PORT, "*", g_apIP);

  Serial.println(F("╔══════════════════════════════════════════╗"));
  Serial.print(F("║ PORTAL CAUTIVO INICIADO: ")); Serial.print(AP_SSID); Serial.println(F(" ║"));
  Serial.print(F("║ IP: ")); Serial.print(WiFi.softAPIP()); Serial.println(F(" | User: admin / Pass: admin ║"));
  Serial.println(F("╚══════════════════════════════════════════╝"));

  mostrarAPStatus();
}

void detenerPortalCautivo() {
  if (!g_apMode) return;
  g_dnsServer.stop();
  WiFi.softAPdisconnect(true);
  g_apMode = false;
  Serial.println(F("[PORTAL] Modo AP detenido"));
}

void initWiFi() {
  loadConfig();
  conectarWiFiCiclo();
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
  static const char* headerKeys[] = {"User-Agent", "Cookie"};
  server.collectHeaders(headerKeys, 2);

  // API REST v1
  server.on("/", HTTP_GET, handleRoot);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/api/v1/status", HTTP_GET, handleApiV1Status);
  server.on("/api/v1/data", HTTP_GET, handleApiV1Data);
  server.on("/api/v1/command", HTTP_POST, handleApiV1Command);
  server.on("/api/v1/history", HTTP_GET, handleApiV1History);

  // Preflight CORS OPTIONS
  server.on("/api/v1/status", HTTP_OPTIONS, []() { sendCorsHeaders(); server.send(204); });
  server.on("/api/v1/data", HTTP_OPTIONS, []() { sendCorsHeaders(); server.send(204); });
  server.on("/api/v1/command", HTTP_OPTIONS, []() { sendCorsHeaders(); server.send(204); });
  server.on("/api/v1/history", HTTP_OPTIONS, []() { sendCorsHeaders(); server.send(204); });

  // Portal Cautivo & Configuración WiFi
  server.on("/portal", HTTP_GET, handlePortal);
  server.on("/portal/login", HTTP_POST, handlePortalLogin);
  server.on("/portal/logout", HTTP_GET, handlePortalLogout);
  server.on("/portal/scan", HTTP_GET, handlePortalScan);
  server.on("/portal/save", HTTP_POST, handlePortalSave);
  server.on("/portal/admin", HTTP_POST, handlePortalAdminChange);
  server.on("/portal/reboot", HTTP_POST, handlePortalReboot);

  // Detección automática de Portal Cautivo (Android / iOS / Windows)
  server.on("/generate_204", handleCaptivePortalRedirect);
  server.on("/gen_204", handleCaptivePortalRedirect);
  server.on("/hotspot-detect.html", handleCaptivePortalRedirect);
  server.on("/ncsi.txt", handleCaptivePortalRedirect);
  server.on("/connecttest.txt", handleCaptivePortalRedirect);
  server.on("/canonical.html", handleCaptivePortalRedirect);

  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println(F("[OK] Web server iniciado en puerto 80 (API + Portal Cautivo)"));
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
    doc["mode"] = g_node.mode;
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
  String sp = getField("SOILPCT");
  if (sp.length()) {
    g_node.soilPct = sp.toFloat();
  } else {
    // Fallback si no viene en payload: seco=3200 -> 0%, mojado=1400 -> 100%
    g_node.soilPct = constrain(map(constrain(g_node.soilADC, 1400, 3200), 3200, 1400, 0, 100), 0, 100);
  }
  String w = getField("WATER");  if (w.length()) g_node.waterADC = w.toInt();
  String wl = getField("WLVL");  if (wl.length()) g_node.waterPct = wl.toFloat();
  String s1 = getField("S1");    if (s1.length()) g_node.s1 = s1.toInt();
  String s2 = getField("S2");    if (s2.length()) g_node.s2 = s2.toInt();
  String pm = getField("PUMP");  if (pm.length()) g_node.pump = (pm == "1");
  String md = getField("MODE");  if (md.length()) g_node.mode = md;
  String cnt = getField("CNT");  if (cnt.length()) g_node.packetCnt = cnt.toInt();
}

// ═══════════════════════════════════════════════════════════════════════════════
// SUPABASE / CLOUD UPLINK
// ═══════════════════════════════════════════════════════════════════════════════

bool enviarSupabaseDirect(const String& jsonPayload) {
  if (!g_wifiConnected) {
    Serial.println(F("[SUPA] Skip: Sin conexion WiFi"));
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

  Serial.print(F("[SUPA] Destino HTTPS: ")); Serial.println(url);
  Serial.print(F("[SUPA] Payload: ")); Serial.println(payload);

  https.setTimeout(15000);  // 15 seg timeout
  if (!https.begin(wifiSecure, url)) {
    Serial.println(F("[SUPA] ❌ Error: no se pudo iniciar conexion HTTPS (falla al inicializar cliente TLS)"));
    return false;
  }

  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");

  int httpCode = https.POST(payload);

  if (httpCode == 201 || httpCode == 200) {
    Serial.print(F("[SUPA] ✅ Dato enviado OK (HTTP "));
    Serial.print(httpCode);
    Serial.println(F(")"));
    https.end();
    return true;
  } else {
    Serial.print(F("[SUPA] ❌ Error HTTP "));
    Serial.print(httpCode);
    if (httpCode == -1) {
      Serial.println(F(" (-1 = Fallo SSL/TLS o DNS: el ESP32 no pudo resolver el dominio de Supabase o fallo el handshake TLS)"));
    } else if (httpCode == -11) {
      Serial.println(F(" (-11 = Timeout en handshake HTTPS con Supabase)"));
    } else {
      String resp = https.getString();
      Serial.print(F(" -> Respuesta Supabase: "));
      Serial.println(resp);
      if (httpCode == 401 || httpCode == 403) {
        Serial.println(F("[SUPA] ⚠️ Posible error de permisos / RLS o SUPABASE_KEY incorrecta."));
      } else if (httpCode == 404) {
        Serial.println(F("[SUPA] ⚠️ La tabla autoinver_data no existe en tu proyecto Supabase."));
      }
    }
    https.end();
    return false;
  }
}

bool enviarTelemetriaActualSupabase() {
  if (!g_wifiConnected) {
    Serial.println(F("[SUPA] Skip envio: Sin conexion WiFi"));
    return false;
  }
  if (!g_node.valid) {
    Serial.println(F("[SUPA] Skip envio: Sin datos validos de nodo"));
    return false;
  }

  Serial.println(F("[SUPA] 🚀 Enviando telemetria en vivo a Supabase..."));
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
  doc["mode"] = g_node.mode;
  doc["rssi"] = g_node.rssi;
  doc["raw_payload"] = "live";

  String jsonStr;
  serializeJson(doc, jsonStr);
  bool ok = enviarSupabaseDirect(jsonStr);
  if (!ok) {
    Serial.println(F("[SUPA] ⚠️ No se pudo enviar directo; guardando en buffer SPIFFS..."));
    guardarEnBuffer(jsonStr);
  } else {
    g_lastSupabaseSend = millis();
  }
  return ok;
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

// Envía un lote de registros en UN solo POST (array JSON). PostgREST lo soporta nativamente.
// Esto evita un handshake TLS por cada línea (cada uno toma 1-2s en el ESP32).
bool enviarSupabaseBatch(String* lineas, int count) {
  if (!g_wifiConnected || count == 0) return false;

  // Construir array JSON, limpiando ts_local de líneas antiguas
  String payload = "[";
  int validas = 0;
  for (int i = 0; i < count; i++) {
    String linea = lineas[i];
    if (linea.indexOf("\"ts_local\"") >= 0) {
      StaticJsonDocument<1024> doc;
      if (deserializeJson(doc, linea) == DeserializationError::Ok) {
        doc.remove("ts_local");
        linea = "";
        serializeJson(doc, linea);
      }
    }
    if (linea.length() < 10) continue;
    if (validas > 0) payload += ",";
    payload += linea;
    validas++;
  }
  payload += "]";

  if (validas == 0) return true;  // nada que enviar

  HTTPClient https;
  String url = String(SUPABASE_URL) + "/rest/v1/" + SUPABASE_TABLE;

  https.setTimeout(20000);
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
    Serial.print(F("[SUPA] Lote enviado OK ("));
    Serial.print(validas);
    Serial.println(F(" registros)"));
    https.end();
    return true;
  }

  Serial.print(F("[SUPA] Error batch HTTP "));
  Serial.print(httpCode);
  if (httpCode == -1) Serial.println(F(" (-1 = fallo SSL/TLS o DNS)"));
  else if (httpCode == -11) Serial.println(F(" (timeout)"));
  else {
    Serial.print(F(" -> "));
    Serial.println(https.getString());
  }
  https.end();
  return false;
}

void flushBufferToSupabase() {
  if (!g_wifiConnected || g_bufferLines == 0) return;

  File f = SPIFFS.open(BUFFER_FILE, "r");
  if (!f) return;

  const int BATCH_SIZE = 20;  // registros por POST
  String lineas[BATCH_SIZE];
  int pendientes = 0;
  int enviados = 0;

  while (f.available()) {
    String linea = f.readStringUntil('\n');
    if (linea.length() < 10) continue;

    lineas[pendientes++] = linea;

    if (pendientes == BATCH_SIZE) {
      if (enviarSupabaseBatch(lineas, pendientes)) {
        enviados += pendientes;
        pendientes = 0;
      } else {
        // Fallo de red: conservar lo que queda y salir
        String nuevoBuffer = "";
        for (int i = 0; i < pendientes; i++) nuevoBuffer += lineas[i] + "\n";
        while (f.available()) {
          String resto = f.readStringUntil('\n');
          if (resto.length() >= 10) nuevoBuffer += resto + "\n";
        }
        f.close();
        SPIFFS.remove(BUFFER_FILE);
        File f2 = SPIFFS.open(BUFFER_FILE, "w");
        if (f2) { f2.print(nuevoBuffer); f2.close(); }
        g_bufferLines = enviados > 0 ? (g_bufferLines - enviados) : g_bufferLines;
        Serial.print(F("[BUF] Flush parcial: "));
        Serial.print(enviados);
        Serial.println(F(" enviados, resto queda en buffer"));
        return;
      }
    }
  }
  f.close();

  // Enviar el último lote (menor a BATCH_SIZE)
  int fallidos = 0;
  if (pendientes > 0) {
    if (enviarSupabaseBatch(lineas, pendientes)) {
      enviados += pendientes;
    } else {
      fallidos = pendientes;
    }
  }

  // Reescribir buffer solo con los fallidos
  SPIFFS.remove(BUFFER_FILE);
  if (fallidos > 0) {
    File f2 = SPIFFS.open(BUFFER_FILE, "w");
    if (f2) {
      for (int i = 0; i < fallidos; i++) f2.println(lineas[i]);
      f2.close();
    }
  }
  g_bufferLines = fallidos;

  Serial.print(F("[BUF] Flush: "));
  Serial.print(enviados);
  Serial.print(F(" enviados, "));
  Serial.print(fallidos);
  Serial.println(F(" pendientes"));
}

void pollComandosPendientes() {
  if (!g_wifiConnected) return;

  HTTPClient https;
  String url = String(SUPABASE_URL) + "/rest/v1/commands_queue"
             + "?device_id=eq.AUTOINVER&status=eq.PENDING"
             + "&order=created_at.asc&limit=5"
             + "&select=id,action,value";

  https.setTimeout(10000);
  if (!https.begin(wifiSecure, url)) {
    Serial.println(F("[CMD] Error: no se pudo iniciar conexion HTTPS"));
    return;
  }
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  int httpCode = https.GET();
  if (httpCode != 200) {
    Serial.print(F("[CMD] Error HTTP ")); Serial.println(httpCode);
    https.end();
    return;
  }

  String body = https.getString();
  https.end();

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return;

  bool comandoEjecutado = false;

  for (JsonObject cmd : doc.as<JsonArray>()) {
    long id = cmd["id"] | 0;
    String action = cmd["action"] | "";
    String value = cmd["value"] | "";

    if (id == 0 || action.length() == 0) continue;

    Serial.print(F("[CMD] Ejecutando: "));
    Serial.print(action); Serial.print(F(" = ")); Serial.println(value);

    // Enviar al nodo por LoRa
    enviarComandoLoRa(action, value);

    String actionUpper = action;
    actionUpper.trim();
    actionUpper.toUpperCase();

    String valueUpper = value;
    valueUpper.trim();
    valueUpper.toUpperCase();

    // Actualizar estado local inmediatamente para reflejar el cambio
    if (actionUpper == "BOMBA" || actionUpper == "PUMP") {
      g_node.pump = (valueUpper == "1");
      g_node.mode = "MANUAL";
    } else if (actionUpper == "S1") {
      g_node.s1 = value.toInt();
      g_node.mode = "MANUAL";
    } else if (actionUpper == "S2") {
      g_node.s2 = value.toInt();
      g_node.mode = "MANUAL";
    } else if (actionUpper == "MODE") {
      g_node.mode = valueUpper;
    }

    // Marcar como ACK en Supabase
    ackComando(id);
    comandoEjecutado = true;
  }

  // Sincronizar inmediatamente la telemetría con Supabase tras ejecutar comandos
  // para que el Dashboard vea el cambio al instante sin esperar los 30s.
  if (comandoEjecutado && g_node.valid) {
    Serial.println(F("[CMD] ⚡ Sincronizando nuevo estado inmediatamente con Supabase..."));
    enviarTelemetriaActualSupabase();
  }
}

void ackComando(long id) {
  if (!g_wifiConnected) return;

  HTTPClient https;
  String url = String(SUPABASE_URL) + "/rest/v1/commands_queue?id=eq." + String(id);

  https.setTimeout(10000);
  if (!https.begin(wifiSecure, url)) return;
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");

  int httpCode = https.PATCH("{\"status\":\"ACK\",\"ack_at\":\"now()\"}");
  if (httpCode != 200 && httpCode != 204) {
    Serial.print(F("[CMD] Error ACK HTTP ")); Serial.println(httpCode);
  }
  https.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
// WEB SERVER - ENDPOINTS
// ═══════════════════════════════════════════════════════════════════════════════

void handleRoot() {
  if (g_apMode) {
    server.sendHeader("Location", "/portal");
    server.send(302, "text/plain", "Redirecting to portal...");
  } else {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "Redirecting to dashboard...");
  }
}

void handleNotFound() {
  if (g_apMode) {
    handleCaptivePortalRedirect();
  } else {
    server.send(404, "text/plain", "404 Not Found");
  }
}

void handleCaptivePortalRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/portal", true);
  server.send(302, "text/plain", "");
}

bool isPortalAuthenticated() {
  String cookie = server.header("Cookie");
  if (cookie.indexOf("AUTOSESS=ok") >= 0) return true;
  if (server.hasArg("token") && server.arg("token") == "autoinver_ok") return true;
  return false;
}

void handlePortal() {
  bool auth = isPortalAuthenticated();
  String html = generatePortalHTML(auth);
  server.send(200, "text/html", html);
}

void handlePortalLogin() {
  String u = server.arg("user");
  String p = server.arg("pass");
  u.trim();
  p.trim();

  if (u == g_adminUser && p == g_adminPass) {
    server.sendHeader("Set-Cookie", "AUTOSESS=ok; Path=/; Max-Age=3600");
    server.sendHeader("Location", "/portal");
    server.send(302, "text/plain", "");
  } else {
    String html = generatePortalHTML(false, "Usuario o contraseña inválidos. (Por defecto: admin / admin)");
    server.send(200, "text/html", html);
  }
}

void handlePortalLogout() {
  server.sendHeader("Set-Cookie", "AUTOSESS=; Path=/; Max-Age=0");
  server.sendHeader("Location", "/portal");
  server.send(302, "text/plain", "");
}

void handlePortalScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + escapeJson(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"sec\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handlePortalSave() {
  if (!isPortalAuthenticated()) {
    server.send(403, "text/plain", "No autenticado");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  pass.trim();

  if (ssid.length() == 0) {
    String html = generatePortalHTML(true, "El nombre de red (SSID) no puede estar vacío.");
    server.send(200, "text/html", html);
    return;
  }

  saveNetworkConfig(ssid, pass);
  String succMsg = "Red '" + ssid + "' guardada. Conectando con jerarquía: 1) geogo_IoT, 2) " + ssid;
  String html = generatePortalHTML(true, "", succMsg);
  server.send(200, "text/html", html);

  delay(600);
  conectarWiFiCiclo();
}

void handlePortalAdminChange() {
  if (!isPortalAuthenticated()) {
    server.send(403, "text/plain", "No autenticado");
    return;
  }

  String u = server.arg("adm_user");
  String p = server.arg("adm_pass");
  u.trim();
  p.trim();

  if (u.length() > 0 && p.length() > 0) {
    saveAdminConfig(u, p);
    String html = generatePortalHTML(true, "", "Credenciales de administrador actualizadas con éxito ✅");
    server.send(200, "text/html", html);
  } else {
    String html = generatePortalHTML(true, "El usuario y contraseña no pueden estar vacíos.");
    server.send(200, "text/html", html);
  }
}

void handlePortalReboot() {
  if (!isPortalAuthenticated()) {
    server.send(403, "text/plain", "No autenticado");
    return;
  }
  server.send(200, "text/plain", "Reiniciando Gateway...");
  delay(1000);
  ESP.restart();
}

String generatePortalHTML(bool authenticated, const String& errorMsg, const String& successMsg) {
  String html = "";
  html.reserve(7000);

  html += F("<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
  html += F("<title>Autoinver Gateway - Portal WiFi</title>");
  html += F("<style>");
  html += F(":root{--bg:#0b1120;--card:rgba(30,41,59,0.85);--border:rgba(255,255,255,0.1);");
  html += F("--accent:#10b981;--blue:#3b82f6;--red:#ef4444;--text:#f8fafc;--muted:#94a3b8;}");
  html += F("*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}");
  html += F("body{background:radial-gradient(circle at top,#1e293b,#0b1120);color:var(--text);min-height:100vh;padding:16px;display:flex;justify-content:center;}");
  html += F(".container{width:100%;max-width:480px;margin:16px auto;}");
  html += F(".header{text-align:center;margin-bottom:20px;}");
  html += F(".header h1{font-size:1.5rem;font-weight:800;background:linear-gradient(90deg,#10b981,#3b82f6);-webkit-background-clip:text;-webkit-text-fill-color:transparent;}");
  html += F(".header p{font-size:0.85rem;color:var(--muted);margin-top:4px;}");
  html += F(".card{background:var(--card);backdrop-filter:blur(10px);border:1px solid var(--border);border-radius:16px;padding:20px;margin-bottom:16px;box-shadow:0 8px 30px rgba(0,0,0,0.3);}");
  html += F(".card-title{font-size:0.95rem;font-weight:700;margin-bottom:12px;display:flex;align-items:center;gap:8px;}");
  html += F(".rule-box{background:rgba(16,185,129,0.08);border-left:3px solid var(--accent);padding:10px 12px;border-radius:0 8px 8px 0;font-size:0.8rem;margin-bottom:10px;line-height:1.4;}");
  html += F(".alert{padding:12px;border-radius:10px;font-size:0.85rem;margin-bottom:14px;}");
  html += F(".alert-err{background:rgba(239,68,68,0.15);border:1px solid var(--red);color:#fca5a5;}");
  html += F(".alert-succ{background:rgba(16,185,129,0.15);border:1px solid var(--accent);color:#6ee7b7;}");
  html += F(".form-group{margin-bottom:14px;}");
  html += F("label{display:block;font-size:0.8rem;color:var(--muted);margin-bottom:6px;font-weight:500;}");
  html += F("input[type='text'],input[type='password']{width:100%;padding:10px 12px;border-radius:10px;background:rgba(15,23,42,0.6);border:1px solid var(--border);color:var(--text);font-size:0.95rem;outline:none;}");
  html += F("input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(16,185,129,0.2);}");
  html += F(".btn{width:100%;padding:11px;border-radius:10px;border:none;font-size:0.9rem;font-weight:600;cursor:pointer;transition:all .2s;}");
  html += F(".btn-pri{background:linear-gradient(135deg,#10b981,#059669);color:#fff;}");
  html += F(".btn-pri:hover{opacity:.95;}");
  html += F(".btn-sec{background:rgba(255,255,255,0.1);color:var(--text);}");
  html += F(".btn-danger{background:rgba(239,68,68,0.15);color:#fca5a5;border:1px solid var(--red);}");
  html += F(".wifi-item{background:rgba(15,23,42,0.5);border:1px solid var(--border);padding:10px 12px;border-radius:10px;margin-bottom:8px;display:flex;justify-content:space-between;align-items:center;cursor:pointer;}");
  html += F(".wifi-item:hover{border-color:var(--accent);background:rgba(16,185,129,0.08);}");
  html += F(".wifi-list{max-height:200px;overflow-y:auto;margin-bottom:10px;}");
  html += F("</style></head><body>");

  html += F("<div class=\"container\"><div class=\"header\">");
  html += F("<h1>🌿 AUTOINVER GATEWAY</h1>");
  html += F("<p>Portal Cautivo de Configuración WiFi</p></div>");

  if (errorMsg.length() > 0) {
    html += "<div class=\"alert alert-err\">⚠️ " + errorMsg + "</div>";
  }
  if (successMsg.length() > 0) {
    html += "<div class=\"alert alert-succ\">✅ " + successMsg + "</div>";
  }

  if (!authenticated) {
    html += F("<div class=\"card\"><div class=\"card-title\">🔐 Acceso al Portal</div>");
    html += F("<p style=\"font-size:0.8rem;color:var(--muted);margin-bottom:14px;\">Ingresa las credenciales administrativas para gestionar las redes WiFi.</p>");
    html += F("<form action=\"/portal/login\" method=\"POST\">");
    html += F("<div class=\"form-group\"><label>Usuario</label><input type=\"text\" name=\"user\" value=\"admin\" required autofocus></div>");
    html += F("<div class=\"form-group\"><label>Contraseña</label><input type=\"password\" name=\"pass\" placeholder=\"••••••••\" required></div>");
    html += F("<button type=\"submit\" class=\"btn btn-pri\">Ingresar al Portal</button></form>");
    html += F("<p style=\"font-size:0.75rem;color:var(--muted);text-align:center;margin-top:14px;\">Credenciales por defecto: <strong>admin</strong> / <strong>admin</strong></p></div>");
  } else {
    // Tarjeta Jerarquía de Conexión
    html += F("<div class=\"card\"><div class=\"card-title\">📶 Jerarquía de Conexión</div>");
    html += "<div class=\"rule-box\"><strong>🥇 Prioridad 1 (Nativa):</strong> <code>" + String(WIFI_SSID_1) + "</code><br>";
    html += F("Se intenta siempre primero de forma automática.</div>");

    html += F("<div class=\"rule-box\" style=\"border-left-color:var(--blue);background:rgba(59,130,246,0.08);\">");
    html += "<strong style=\"color:var(--blue);\">🥈 Prioridad 2 (Respaldo):</strong> <code>" + String(WIFI_SSID_2) + "</code><br>";
    html += F("Se intenta si geogo_IoT no está disponible.</div>");

    html += F("<div class=\"rule-box\" style=\"border-left-color:#8b5cf6;background:rgba(139,92,246,0.08);\">");
    html += "<strong style=\"color:#a78bfa;\">🥉 Prioridad 3 (Personalizada):</strong> <code>" + (g_configuredSSID.length() > 0 ? g_configuredSSID : String("(Ninguna)")) + "</code><br>";
    html += F("Red adicional guardada en memoria flash desde este portal.</div>");
    html += "</div>";

    // Tarjeta Escáner
    html += F("<div class=\"card\"><div class=\"card-title\">🔍 Redes WiFi Disponibles");
    html += F("<button class=\"btn btn-sec\" style=\"width:auto;padding:4px 10px;font-size:0.75rem;margin-left:auto;\" onclick=\"scanWifi()\">Escanear</button></div>");
    html += F("<div id=\"scanLoad\" style=\"display:none;text-align:center;font-size:0.8rem;color:var(--muted);padding:8px;\">Escaneando redes cercanas... ⏳</div>");
    html += F("<div id=\"wifiList\" class=\"wifi-list\"><div style=\"text-align:center;font-size:0.8rem;color:var(--muted);padding:10px;\">Presiona 'Escanear' para buscar redes.</div></div></div>");

    // Tarjeta Configurar Red Secundaria
    html += F("<div class=\"card\"><div class=\"card-title\">⚙️ Configurar Red Secundaria</div>");
    html += F("<form action=\"/portal/save\" method=\"POST\">");
    html += "<div class=\"form-group\"><label>Nombre de Red (SSID)</label><input type=\"text\" id=\"ssidIn\" name=\"ssid\" value=\"" + g_configuredSSID + "\" placeholder=\"Nombre de red WiFi\" required></div>";
    html += "<div class=\"form-group\"><label>Contraseña</label><input type=\"password\" id=\"passIn\" name=\"pass\" value=\"" + g_configuredPass + "\" placeholder=\"Clave WiFi\"></div>";
    html += F("<div style=\"margin-bottom:14px;font-size:0.8rem;\"><label style=\"display:flex;align-items:center;gap:6px;cursor:pointer;\"><input type=\"checkbox\" onchange=\"document.getElementById('passIn').type=this.checked?'text':'password'\"> Mostrar contraseña</label></div>");
    html += F("<button type=\"submit\" class=\"btn btn-pri\">💾 Guardar y Conectar</button></form></div>");

    // Tarjeta Credenciales Admin
    html += F("<div class=\"card\"><div class=\"card-title\">🔒 Seguridad del Portal</div>");
    html += F("<form action=\"/portal/admin\" method=\"POST\">");
    html += "<div class=\"form-group\"><label>Usuario Administrador</label><input type=\"text\" name=\"adm_user\" value=\"" + g_adminUser + "\" required></div>";
    html += "<div class=\"form-group\"><label>Nueva Contraseña</label><input type=\"password\" name=\"adm_pass\" placeholder=\"Nueva clave\" required></div>";
    html += F("<button type=\"submit\" class=\"btn btn-sec\">Actualizar Clave Admin</button></form></div>");

    // Acciones y Salida
    html += F("<div class=\"card\" style=\"display:flex;gap:10px;\">");
    html += F("<button onclick=\"rebootGw()\" class=\"btn btn-danger\" style=\"flex:1;\">⚡ Reiniciar Gateway</button>");
    html += F("<a href=\"/portal/logout\" class=\"btn btn-sec\" style=\"flex:1;text-decoration:none;display:flex;align-items:center;justify-content:center;\">🚪 Salir</a></div>");
  }

  html += F("</div>");

  if (authenticated) {
    html += F("<script>");
    html += F("async function scanWifi(){");
    html += F("const b=document.getElementById('wifiList');const l=document.getElementById('scanLoad');");
    html += F("l.style.display='block';b.innerHTML='';");
    html += F("try{const r=await fetch('/portal/scan');const d=await r.json();l.style.display='none';");
    html += F("if(!d.length){b.innerHTML='<div style=\"text-align:center;font-size:0.8rem;color:#94a3b8;padding:8px;\">Sin redes detectadas.</div>';return;}");
    html += F("b.innerHTML=d.map(n=>`<div class=\"wifi-item\" onclick=\"setNet('${n.ssid.replace(/'/g,\"\\\\'\")}')\"><div><div style=\"font-weight:600;font-size:0.85rem;\">${n.ssid}</div><div style=\"font-size:0.75rem;color:#94a3b8;\">${n.sec?'🔒 Protegida':'🔓 Abierta'} · RSSI: ${n.rssi} dBm</div></div><div style=\"color:#10b981;font-size:0.8rem;font-weight:600;\">Seleccionar ➔</div></div>`).join('');");
    html += F("}catch(e){l.style.display='none';b.innerHTML='<div style=\"color:#ef4444;font-size:0.8rem;text-align:center;padding:8px;\">Error al escanear.</div>';}}");
    html += F("function setNet(s){document.getElementById('ssidIn').value=s;document.getElementById('passIn').focus();}");
    html += F("async function rebootGw(){if(confirm('¿Reiniciar Gateway ahora?')){await fetch('/portal/reboot',{method:'POST'});alert('Reiniciando... Espera 10 segundos.');}}");
    html += F("</script>");
  }

  html += F("</body></html>");
  return html;
}

void handleDashboard() {
  String html = generateDashboardHTML();
  server.send(200, "text/html", html);
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

void handleApiV1Status() {
  sendCorsHeaders();
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
  sendCorsHeaders();
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
  sendCorsHeaders();
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
  
  // Actualizar estado local
  if (act == "BOMBA" || act == "PUMP") {
    g_node.pump = (val == "1");
    g_node.mode = "MANUAL";
  } else if (act == "S1") {
    g_node.s1 = val.toInt();
    g_node.mode = "MANUAL";
  } else if (act == "S2") {
    g_node.s2 = val.toInt();
    g_node.mode = "MANUAL";
  } else if (act == "MODE") {
    g_node.mode = val;
  }

  if (g_node.valid && g_wifiConnected) {
    enviarTelemetriaActualSupabase();
  }
  
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
  sendCorsHeaders();
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
      <span>🌐 IP: )rawliteral" + (g_wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + R"rawliteral(</span>
    </div>
    <a href="/portal" style="text-decoration:none; color:#10b981;" class="status-pill">
      <span>⚙️ Config WiFi</span>
    </a>
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
  display.print(g_wifiConnected ? "W" : (g_apMode ? "A" : "-"));
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

void mostrarWiFiStatus(const char* ssid, int intento) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Conectando WiFi..."));
  display.setCursor(0, 16);
  display.print(F("Red: "));
  display.print(ssid);
  display.setCursor(0, 34);
  display.print(F("Intento: "));
  display.print(intento);
  display.setCursor(0, 50);
  display.print(F("1:geogo 2:amarox"));
  display.display();
}

void mostrarAPStatus() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(F("PORTAL CAUTIVO AP"));

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14);
  display.print(F("SSID: "));
  display.print(AP_SSID);
  display.setCursor(0, 28);
  display.print(F("IP:   192.168.4.1"));
  display.setCursor(0, 42);
  display.print(F("User: "));
  display.print(g_adminUser);
  display.setCursor(0, 54);
  display.print(F("Pass: "));
  display.print(g_adminPass);
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

String escapeJson(const String& s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

bool checkInternetConnection() {
  return g_wifiConnected;
}
