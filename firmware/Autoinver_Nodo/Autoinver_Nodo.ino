/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║                    AUTOINVER NODO v2.0 - FIRMWARE                         ║
 * ║         Sistema Autónomo de Invernadero Móvil con LoRa 915MHz             ║
 * ║                                                                           ║
 * ║  Hardware: Heltec LoRa32 V2 + DHT22 + HumSuelo + Sharp IR + PCA9685      ║
 * ║  Autor: Agente Implementador | Licencia: MIT                              ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Time.h>
#include <Adafruit_PWMServoDriver.h>
#include <Preferences.h>

#include "config.h"

// ═══════════════════════════════════════════════════════════════════════════════
// OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════════════════════
DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, PIN_OLED_RST);
ESP32Time rtc(GMT_OFFSET_SEC);
// PCA9685 comparte el bus I2C del OLED (Wire, pines 4/15).
// En Heltec LoRa32 V2 el GPIO21 controla Vext, no se puede usar como SDA.
// OLED (0x3C) y PCA9685 (0x40) tienen direcciones distintas: pueden coexistir.
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(PCA9685_ADDR, Wire);
Preferences prefs;

// ═══════════════════════════════════════════════════════════════════════════════
// ESTRUCTURAS Y ENUMERACIONES
// ═══════════════════════════════════════════════════════════════════════════════

enum SystemMode {
  MODE_AUTO,
  MODE_MANUAL,
  MODE_SLEEP
};

enum SyncState {
  SYNC_NONE,
  SYNC_WAIT,
  SYNC_OK,
  SYNC_FAIL
};

enum VentState {
  VENT_CLOSED = 0,
  VENT_HALF   = 45,
  VENT_OPEN   = 90
};

struct SensorData {
  float temperature = -999.0f;
  float humidity    = -999.0f;
  float soilPct     = 0.0f;
  float waterPct    = 0.0f;
  int   soilADC     = 0;
  int   waterADC    = 0;
  bool  valid       = false;
};

struct ActuatorState {
  int   servo1Pos   = 0;
  int   servo2Pos   = 0;
  bool  pumpOn      = false;
  bool  pumpAuto    = false;
  unsigned long pumpStartMs = 0;
};

struct TelemetryFrame {
  uint32_t packetCounter = 0;
  int      lastRSSI      = 0;
  String   lastACK       = "";
};

// ═══════════════════════════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ═══════════════════════════════════════════════════════════════════════════════

ConfigData    g_config;

SensorData    g_sensors;
ActuatorState g_actuators;
TelemetryFrame g_telemetry;

SystemMode    g_mode        = MODE_AUTO;
SyncState     g_syncState   = SYNC_NONE;
bool          g_pcaOk       = false;  // PCA9685 detectado en I2C

// Timestamps no-bloqueantes
unsigned long g_lastSensorRead  = 0;
unsigned long g_lastTx          = 0;
unsigned long g_lastDisplayUpd  = 0;
unsigned long g_lastSyncAttempt = 0;
unsigned long g_manualOverrideUntil = 0;

// Control de override manual
String        g_lastCommand     = "NINGUNO";
int           g_syncAttempts    = 0;

// Buffer suavizado para ADC (filtro media móvil)
static const int ADC_SAMPLES = 8;
int g_soilSamples[ADC_SAMPLES]  = {0};
int g_waterSamples[ADC_SAMPLES] = {0};
int g_sampleIndex = 0;
bool g_sampleBufFull = false;

// ═══════════════════════════════════════════════════════════════════════════════
// DECLARACIONES DE FUNCIONES
// ═══════════════════════════════════════════════════════════════════════════════

// Setup & Init
void initDisplay();
void initLoRa();
void initSensors();
void initActuators();
void initRTC();
void loadCalibration();
void saveCalibration();
void showBootScreen();

// Sensores
void readAllSensors();
float smoothADC(int* buf, int len);
float adcToSoilPercent(int adc);
float adcToWaterPercent(int adc);
bool  checkSensorsHealthy();

// Lógica de control autónomo
void runControlLogic();
void controlTemperature();
void controlIrrigation();
void activatePump(bool on, bool automatic);
void managePumpTimeout(unsigned long now);

// Comunicación LoRa
void transmitTelemetry();
void listenForCommands();
void processCommand(const String& cmd);
void requestTimeSync();
void handleTimeSync(unsigned long now);
void applySyncedTime(const String& payload);
void sendACK(const String& act);

// Display OLED
void updateDisplay();
void drawGauge(int x, int y, int w, int h, float value, float maxVal, const char* label);
void drawBar(int x, int y, int w, int h, float pct, uint16_t color);
void showErrorScreen(const char* msg);

// Utilidades
String getTimestampStr();
void setServoAngle(uint8_t channel, uint16_t angle);
void handleWatchdog();
void enterSleepMode();
void logMessage(const char* level, const char* msg);

// ═══════════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println(F("\n╔══════════════════════════════════════════╗"));
  Serial.println(F("║      AUTOINVER NODO v2.0 - BOOT         ║"));
  Serial.println(F("╚══════════════════════════════════════════╝"));

  // Inicializar NVS para calibración
  prefs.begin("autoinver", false);
  loadCalibration();

  initDisplay();
  showBootScreen();
  delay(1500);

  initSensors();
  initActuators();
  initRTC();
  initLoRa();

  Serial.println(F("[BOOT] Sistema iniciado correctamente"));
  Serial.println(F("[BOOT] Modo: AUTO | Sync: PENDIENTE"));

  // Primer sync de hora
  requestTimeSync();
  g_syncState = SYNC_WAIT;
  g_lastSyncAttempt = millis();
  g_syncAttempts = 1;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LOOP PRINCIPAL - MÁQUINA DE ESTADOS NO-BLOQUEANTE
// ═══════════════════════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // ── 1. Lectura de sensores cada SENSOR_INTERVAL_MS ──
  if (now - g_lastSensorRead >= SENSOR_INTERVAL_MS) {
    g_lastSensorRead = now;
    readAllSensors();
    
    if (g_mode == MODE_AUTO && now > g_manualOverrideUntil) {
      runControlLogic();
    }
  }

  // ── 2. Gestión timeout bomba (seguridad) ──
  managePumpTimeout(now);

  // ── 3. Transmisión LoRa cada TX_INTERVAL_MS ──
  if (now - g_lastTx >= TX_INTERVAL_MS) {
    g_lastTx = now;
    transmitTelemetry();
  }

  // ── 4. Sincronización de hora ──
  if (g_syncState != SYNC_OK) {
    handleTimeSync(now);
  }

  // ── 5. Escucha de comandos del gateway ──
  listenForCommands();

  // ── 6. Actualización OLED cada DISPLAY_INTERVAL_MS ──
  if (now - g_lastDisplayUpd >= DISPLAY_INTERVAL_MS) {
    g_lastDisplayUpd = now;
    updateDisplay();
  }

  // ── 7. Watchdog implícito (yield) ──
  delay(1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: INICIALIZACIÓN
// ═══════════════════════════════════════════════════════════════════════════════

void initDisplay() {
  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(50);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(50);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("[ERR] OLED init failed"));
    while (true) { delay(1000); }
  }
  display.clearDisplay();
  display.display();
}

void initLoRa() {
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_IRQ);
  
  if (!LoRa.begin(LORA_FREQ)) {
    showErrorScreen("LoRa FAIL");
    Serial.println(F("[ERR] LoRa init failed"));
    while (true) { delay(1000); }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TXPOWER);
  LoRa.enableCrc();

  Serial.println(F("[OK] LoRa 915MHz iniciado"));
  Serial.print(F("       SF=")); Serial.print(LORA_SF);
  Serial.print(F(" BW=")); Serial.print(LORA_BW / 1000);
  Serial.print(F("kHz CR=4/")); Serial.println(LORA_CR);
}

void initSensors() {
  dht.begin();
  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_SHARP, INPUT);
  
  // Inicializar buffers de suavizado
  for (int i = 0; i < ADC_SAMPLES; i++) {
    g_soilSamples[i] = analogRead(PIN_SOIL);
    g_waterSamples[i] = analogRead(PIN_SHARP);
    delay(5);
  }
  g_sampleBufFull = true;
  
  Serial.println(F("[OK] Sensores iniciados"));
}

void initActuators() {
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, LOW);

  // PCA9685 comparte el bus I2C del OLED (Wire, pines 4/15).
  // NOTA: GPIO21 en Heltec LoRa32 V2 controla Vext, no sirve como SDA.

  // Escáner I2C de diagnóstico: lista dispositivos en el bus (esperado: 0x3C OLED, 0x40 PCA9685)
  Serial.println(F("[I2C] Escaneando bus pines 4/15..."));
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("[I2C] Dispositivo en 0x"));
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("[I2C] NINGUN dispositivo encontrado -> revisar cableado SDA/SCL/VCC"));
  }

  if (!pca.begin()) {
    Serial.println(F("[ERR] PCA9685 no detectado en I2C (bus pines 4/15). Servos deshabilitados."));
    g_pcaOk = false;
  } else {
    // Clones del PCA9685 suelen tener oscilador de 27MHz (original: 25MHz).
    // Si no se calibra, los pulsos salen ~8% más cortos y el servo no se mueve.
    pca.setOscillatorFrequency(27000000);
    pca.setPWMFreq(50);  // 50Hz para servos
    g_pcaOk = true;

    // Prueba de barrido al arranque: el servo debe moverse 0->90->0
    Serial.println(F("[SERVO] Prueba de barrido en PWM0..."));
    setServoAngle(CHANNEL_SERVO_A, 0);
    delay(800);
    setServoAngle(CHANNEL_SERVO_A, 90);
    delay(800);
    setServoAngle(CHANNEL_SERVO_A, 0);
    delay(800);

    // Posición segura inicial
    setServoAngle(CHANNEL_SERVO_A, VENT_CLOSED);
    setServoAngle(CHANNEL_SERVO_B, VENT_CLOSED);

    g_actuators.servo1Pos = VENT_CLOSED;
    g_actuators.servo2Pos = VENT_CLOSED;
  }

  Serial.println(F("[OK] Actuadores iniciados (posición segura)"));
}

void initRTC() {
  // Fecha de compilación como fallback
  rtc.setTime(0, 0, 12, __DATE__[4] == ' ' ? __DATE__[5]-'0' : (__DATE__[4]-'0')*10 + (__DATE__[5]-'0'),
              String(__DATE__).substring(0,3) == "Jan" ? 1 : String(__DATE__).substring(0,3) == "Feb" ? 2 :
              String(__DATE__).substring(0,3) == "Mar" ? 3 : String(__DATE__).substring(0,3) == "Apr" ? 4 :
              String(__DATE__).substring(0,3) == "May" ? 5 : String(__DATE__).substring(0,3) == "Jun" ? 6 :
              String(__DATE__).substring(0,3) == "Jul" ? 7 : String(__DATE__).substring(0,3) == "Aug" ? 8 :
              String(__DATE__).substring(0,3) == "Sep" ? 9 : String(__DATE__).substring(0,3) == "Oct" ? 10 :
              String(__DATE__).substring(0,3) == "Nov" ? 11 : 12,
              (__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100 + (__DATE__[9]-'0')*10 + (__DATE__[10]-'0'));
  
  Serial.println(F("[OK] RTC iniciado (fallback compilación)"));
}

void loadCalibration() {
  // Cargar calibración desde NVS, o usar defaults
  g_config.sharpMinADC  = prefs.getInt("sharp_min", 500);
  g_config.sharpMaxADC  = prefs.getInt("sharp_max", 3500);
  g_config.tempThresh1  = prefs.getFloat("temp_t1", 28.0f);
  g_config.tempThresh2  = prefs.getFloat("temp_t2", 32.0f);
  g_config.soilThresh   = prefs.getFloat("soil_thr", 40.0f);
  g_config.pumpDuration = prefs.getInt("pump_dur", 10000);
  
  Serial.println(F("[OK] Calibración cargada desde NVS"));
}

void saveCalibration() {
  prefs.putInt("sharp_min", g_config.sharpMinADC);
  prefs.putInt("sharp_max", g_config.sharpMaxADC);
  prefs.putFloat("temp_t1", g_config.tempThresh1);
  prefs.putFloat("temp_t2", g_config.tempThresh2);
  prefs.putFloat("soil_thr", g_config.soilThresh);
  prefs.putInt("pump_dur", g_config.pumpDuration);
  
  Serial.println(F("[OK] Calibración guardada en NVS"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: SENSORES
// ═══════════════════════════════════════════════════════════════════════════════

void readAllSensors() {
  // ── DHT22 ──
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (!isnan(t) && !isnan(h)) {
    g_sensors.temperature = t;
    g_sensors.humidity    = h;
    g_sensors.valid       = true;
  } else {
    Serial.println(F("[WARN] DHT22 lectura fallida"));
  }

  // ── ADC con suavizado (media móvil) ──
  g_soilSamples[g_sampleIndex]  = analogRead(PIN_SOIL);
  g_waterSamples[g_sampleIndex] = analogRead(PIN_SHARP);
  g_sampleIndex = (g_sampleIndex + 1) % ADC_SAMPLES;
  if (g_sampleIndex == 0) g_sampleBufFull = true;

  int samplesToUse = g_sampleBufFull ? ADC_SAMPLES : g_sampleIndex;
  g_sensors.soilADC  = (int)smoothADC(g_soilSamples, samplesToUse);
  g_sensors.waterADC = (int)smoothADC(g_waterSamples, samplesToUse);

  // ── Conversión a porcentajes ──
  g_sensors.soilPct  = adcToSoilPercent(g_sensors.soilADC);
  g_sensors.waterPct = adcToWaterPercent(g_sensors.waterADC);

  // Debug
  #ifdef DEBUG_SENSORS
  Serial.print(F("[SENS] T=")); Serial.print(g_sensors.temperature, 1);
  Serial.print(F("C H=")); Serial.print(g_sensors.humidity, 1);
  Serial.print(F("% Soil=")); Serial.print(g_sensors.soilPct, 1);
  Serial.print(F("% Water=")); Serial.print(g_sensors.waterPct, 1);
  Serial.println(F("%"));
  #endif
}

float smoothADC(int* buf, int len) {
  if (len == 0) return 0;
  long sum = 0;
  for (int i = 0; i < len; i++) sum += buf[i];
  return (float)sum / len;
}

float adcToSoilPercent(int adc) {
  // Sensor capacitivo: típicamente seco=4095, mojado=0 (o viceversa)
  // Ajustar según calibración física
  float pct = map(constrain(adc, 0, 4095), 0, 4095, 0, 100);
  return constrain(pct, 0.0f, 100.0f);
}

float adcToWaterPercent(int adc) {
  // Sharp IR: mayor proximidad = mayor ADC = más agua
  // Calibrar con tanque lleno/vacío
  int minA = g_config.sharpMinADC;
  int maxA = g_config.sharpMaxADC;
  
  if (minA >= maxA) return 50.0f;  // Sanity check
  
  float pct = map(constrain(adc, minA, maxA), maxA, minA, 0, 100);
  return constrain(pct, 0.0f, 100.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: LÓGICA DE CONTROL AUTÓNOMO
// ═══════════════════════════════════════════════════════════════════════════════

void runControlLogic() {
  controlTemperature();
  controlIrrigation();
}

void controlTemperature() {
  float t = g_sensors.temperature;
  if (t < -100.0f) return;  // Sensor inválido

  VentState targetS1 = VENT_CLOSED;
  VentState targetS2 = VENT_CLOSED;

  if (t > g_config.tempThresh2) {
    targetS1 = VENT_OPEN;
    targetS2 = VENT_OPEN;
  } else if (t > g_config.tempThresh1) {
    targetS1 = VENT_OPEN;
    targetS2 = VENT_CLOSED;
  } else if (t < (g_config.tempThresh1 - TEMP_HYSTERESIS)) {
    targetS1 = VENT_CLOSED;
    targetS2 = VENT_CLOSED;
  }

  // Aplicar solo si cambió (evitar vibración de servos)
  if (targetS1 != g_actuators.servo1Pos) {
    setServoAngle(CHANNEL_SERVO_A, targetS1);
    g_actuators.servo1Pos = targetS1;
  }
  if (targetS2 != g_actuators.servo2Pos) {
    setServoAngle(CHANNEL_SERVO_B, targetS2);
    g_actuators.servo2Pos = targetS2;
  }
}

void controlIrrigation() {
  bool soilDry   = g_sensors.soilPct < g_config.soilThresh;
  bool hasWater  = g_sensors.waterPct > WATER_MIN_PCT;
  bool pumpIdle  = !g_actuators.pumpOn;

  // Iniciar riego automático
  if (pumpIdle && soilDry && hasWater) {
    activatePump(true, true);
    Serial.println(F("[AUTO] Riego iniciado (suelo seco + agua disponible)"));
  }

  // Detener riego automático
  if (g_actuators.pumpOn && g_actuators.pumpAuto) {
    bool soilWetEnough = g_sensors.soilPct > (g_config.soilThresh + SOIL_HYSTERESIS);
    if (soilWetEnough) {
      activatePump(false, true);
      Serial.println(F("[AUTO] Riego detenido (suelo humedecido)"));
    }
  }
}

void activatePump(bool on, bool automatic) {
  // Protección: nunca encender sin agua suficiente (evita quemar la bomba)
  if (on && g_sensors.waterPct < WATER_MIN_PCT) {
    Serial.print(F("[PUMP] BLOQUEADO: agua "));
    Serial.print(g_sensors.waterPct, 1);
    Serial.print(F("% < mínimo "));
    Serial.print(WATER_MIN_PCT, 0);
    Serial.println(F("%"));
    return;
  }

  g_actuators.pumpOn = on;
  g_actuators.pumpAuto = automatic;
  digitalWrite(PIN_RELE, on ? HIGH : LOW);

  if (on) {
    g_actuators.pumpStartMs = millis();
    Serial.println(F("[PUMP] Bomba ENCENDIDA"));
  } else {
    Serial.println(F("[PUMP] Bomba APAGADA"));
  }
}

void managePumpTimeout(unsigned long now) {
  if (!g_actuators.pumpOn) return;

  // Corte de emergencia: el agua bajó del mínimo mientras regaba
  if (g_sensors.waterPct < WATER_MIN_PCT) {
    activatePump(false, g_actuators.pumpAuto);
    Serial.println(F("[PUMP] CORTE DE EMERGENCIA: nivel de agua bajo"));
    return;
  }

  unsigned long elapsed = now - g_actuators.pumpStartMs;

  // Timeout de seguridad (evitar inundación)
  if (elapsed >= (unsigned long)g_config.pumpDuration) {
    activatePump(false, g_actuators.pumpAuto);
    Serial.println(F("[PUMP] Timeout de seguridad activado"));
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: SERVOS PCA9685
// ═══════════════════════════════════════════════════════════════════════════════

void setServoAngle(uint8_t channel, uint16_t angle) {
  if (!g_pcaOk) {
    Serial.println(F("[SERVO] Ignorado: PCA9685 no disponible"));
    return;
  }
  uint16_t pulse = map(constrain(angle, 0, 180), 0, 180, SERVO_PULSE_MIN, SERVO_PULSE_MAX);
  pca.setPWM(channel, 0, pulse);
  
  Serial.print(F("[SERVO] Canal ")); Serial.print(channel);
  Serial.print(F(" -> ")); Serial.print(angle);
  Serial.println(F("°"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: COMUNICACIÓN LORA
// ═══════════════════════════════════════════════════════════════════════════════

void transmitTelemetry() {
  g_telemetry.packetCounter++;

  String ts = getTimestampStr();
  String payload = "ID:AUTOINVER|";
  payload += "TS:" + ts + "|";
  payload += "TEMP:" + String(g_sensors.temperature, 1) + "|";
  payload += "HUM:" + String(g_sensors.humidity, 1) + "|";
  payload += "SOIL:" + String(g_sensors.soilADC) + "|";
  payload += "WATER:" + String(g_sensors.waterADC) + "|";
  payload += "WLVL:" + String(g_sensors.waterPct, 1) + "|";
  payload += "S1:" + String(g_actuators.servo1Pos) + "|";
  payload += "S2:" + String(g_actuators.servo2Pos) + "|";
  payload += "PUMP:" + String(g_actuators.pumpOn ? 1 : 0) + "|";
  payload += "MODE:" + String(g_mode == MODE_AUTO ? "AUTO" : g_mode == MODE_MANUAL ? "MANUAL" : "SLEEP") + "|";
  payload += "RSSI:" + String(g_telemetry.lastRSSI) + "|";
  payload += "CNT:" + String(g_telemetry.packetCounter);
  
  // Incluir ACK de último comando si existe
  if (g_telemetry.lastACK.length() > 0) {
    payload += "|ACK:" + g_telemetry.lastACK;
    g_telemetry.lastACK = "";  // Limpiar después de enviar
  }

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  Serial.print(F("[TX] ")); Serial.println(payload);
}

void listenForCommands() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  String rx = "";
  while (LoRa.available()) {
    rx += (char)LoRa.read();
  }

  // Actualizar RSSI del gateway
  g_telemetry.lastRSSI = LoRa.packetRssi();

  // ── Solicitud de hora (desde gateway) ──
  if (rx.indexOf("SYNC:HORA") >= 0) {
    applySyncedTime(rx);
    return;
  }

  // ── Comando dirigido a este nodo ──
  if (rx.indexOf("CMD:AUTOINVER") >= 0) {
    Serial.print(F("[RX-CMD] ")); Serial.println(rx);
    processCommand(rx);
  }
}

void processCommand(const String& cmd) {
  // Formato: CMD:AUTOINVER|ACT:XXXX|VAL:xx|TS:YYYY-MM-DD_HH:MM:SS
  
  int actIdx = cmd.indexOf("ACT:") + 4;
  int actEnd = cmd.indexOf("|", actIdx);
  if (actIdx < 4 || actEnd < 0) return;
  String act = cmd.substring(actIdx, actEnd);

  int valIdx = cmd.indexOf("VAL:") + 4;
  int valEnd = cmd.indexOf("|", valIdx);
  if (valEnd < 0) valEnd = cmd.length();
  String valStr = cmd.substring(valIdx, valEnd);

  g_lastCommand = act + ":" + valStr;
  
  // Activar override manual por 10 minutos
  g_manualOverrideUntil = millis() + MANUAL_OVERRIDE_MS;
  g_mode = MODE_MANUAL;

  if (act == "BOMBA") {
    bool on = (valStr == "1");
    activatePump(on, false);
    sendACK("BOMBA");
    
  } else if (act == "S1") {
    int angle = constrain(valStr.toInt(), 0, 180);
    setServoAngle(CHANNEL_SERVO_A, angle);
    g_actuators.servo1Pos = angle;
    sendACK("S1");
    
  } else if (act == "S2") {
    int angle = constrain(valStr.toInt(), 0, 180);
    setServoAngle(CHANNEL_SERVO_B, angle);
    g_actuators.servo2Pos = angle;
    sendACK("S2");
    
  } else if (act == "MODE") {
    if (valStr == "AUTO") {
      g_mode = MODE_AUTO;
      g_manualOverrideUntil = 0;
      Serial.println(F("[CMD] Modo cambiado a AUTO"));
    } else if (valStr == "SLEEP") {
      g_mode = MODE_SLEEP;
      Serial.println(F("[CMD] Modo cambiado a SLEEP"));
    }
    sendACK("MODE");
    
  } else if (act == "CAL") {
    // Comando de calibración: CAL:SHARP_MIN:500
    // Futuro: implementar calibración remota
    Serial.println(F("[CMD] Calibración recibida (pendiente implementar)"));
    sendACK("CAL");
  }
}

void sendACK(const String& act) {
  g_telemetry.lastACK = act;
  Serial.print(F("[ACK] Comando confirmado: ")); Serial.println(act);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: SINCRONIZACIÓN DE HORA
// ═══════════════════════════════════════════════════════════════════════════════

void requestTimeSync() {
  LoRa.beginPacket();
  LoRa.print("REQ:HORA|ID:AUTOINVER");
  LoRa.endPacket();
  
  Serial.println(F("[SYNC] Solicitando hora al gateway..."));
}

void handleTimeSync(unsigned long now) {
  if (g_syncState == SYNC_NONE) {
    if (g_syncAttempts < MAX_SYNC_ATTEMPTS) {
      requestTimeSync();
      g_syncState = SYNC_WAIT;
      g_lastSyncAttempt = now;
      g_syncAttempts++;
    } else {
      g_syncState = SYNC_FAIL;
      Serial.println(F("[SYNC] Máximo de intentos alcanzado, usando RTC local"));
    }
  } else if (g_syncState == SYNC_WAIT) {
    if (now - g_lastSyncAttempt > SYNC_TIMEOUT_MS) {
      g_syncState = SYNC_NONE;  // Timeout, reintentar
      Serial.println(F("[SYNC] Timeout, reintentando..."));
    }
  }
}

void applySyncedTime(const String& payload) {
  // Formato: SYNC:HORA|YYYY-MM-DD_HH:MM:SS
  int p = payload.indexOf("SYNC:HORA|") + 10;
  if (p < 10) return;
  
  String h = payload.substring(p);
  if (h.length() < 19) return;

  int y  = h.substring(0, 4).toInt();
  int mo = h.substring(5, 7).toInt();
  int d  = h.substring(8, 10).toInt();
  int hr = h.substring(11, 13).toInt();
  int mn = h.substring(14, 16).toInt();
  int sc = h.substring(17, 19).toInt();

  // Validación de rango
  if (y < 2024 || y > 2035 || mo < 1 || mo > 12 || d < 1 || d > 31) {
    Serial.println(F("[SYNC] Timestamp inválido recibido"));
    return;
  }

  rtc.setTime(sc, mn, hr, d, mo, y);
  g_syncState = SYNC_OK;
  
  Serial.print(F("[SYNC] Hora sincronizada: "));
  Serial.println(getTimestampStr());
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: DISPLAY OLED PROFESIONAL
// ═══════════════════════════════════════════════════════════════════════════════

void showBootScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Dibujar marco
  display.drawRect(0, 0, SCREEN_W, SCREEN_H, SSD1306_WHITE);
  
  display.setCursor(18, 8);
  display.setTextSize(1);
  display.print(F("AUTOINVER"));
  
  display.setCursor(14, 20);
  display.print(F("NODO v2.0"));

  display.drawLine(4, 32, SCREEN_W - 4, 32, SSD1306_WHITE);

  display.setCursor(8, 38);
  display.print(F("DHT22 + HumSuelo"));
  display.setCursor(8, 48);
  display.print(F("Sharp IR + PCA9685"));
  display.setCursor(8, 58);
  display.print(F("LoRa 915MHz"));

  display.display();
}

void updateDisplay() {
  display.clearDisplay();

  // ── Barra superior: título + hora ──
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print(F("AUTOINVER"));
  
  // Indicador de sync
  display.setCursor(62, 1);
  if (g_syncState == SYNC_OK) display.print(F("[S]"));
  else if (g_syncState == SYNC_WAIT) display.print(F("[?]"));
  else display.print(F("[L]"));

  // Hora
  display.setCursor(86, 1);
  int h = rtc.getHour(true);
  int m = rtc.getMinute();
  if (h < 10) display.print("0"); display.print(h);
  display.print(":");
  if (m < 10) display.print("0"); display.print(m);

  // ── Fila 2: Temperatura y Humedad ──
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.print(F("T:"));
  if (g_sensors.temperature > -100) {
    display.print(g_sensors.temperature, 1);
    display.print(F("C"));
  } else {
    display.print(F("--"));
  }

  display.setCursor(64, 12);
  display.print(F("H:"));
  if (g_sensors.humidity >= 0) {
    display.print(g_sensors.humidity, 1);
    display.print(F("%"));
  } else {
    display.print(F("--"));
  }

  // ── Barras de nivel: Suelo y Agua ──
  // Barra suelo
  drawBar(0, 24, 60, 8, g_sensors.soilPct / 100.0f, SSD1306_WHITE);
  display.setCursor(2, 25);
  display.setTextColor(SSD1306_BLACK);
  display.print(F("SL"));
  display.print((int)g_sensors.soilPct);
  display.print("%");

  // Barra agua
  display.setTextColor(SSD1306_WHITE);
  drawBar(66, 24, 62, 8, g_sensors.waterPct / 100.0f, SSD1306_WHITE);
  display.setCursor(68, 25);
  display.setTextColor(SSD1306_BLACK);
  display.print(F("WT"));
  display.print((int)g_sensors.waterPct);
  display.print("%");

  // ── Fila: Compuertas y Bomba ──
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 36);
  display.print(F("V1:"));
  display.print(g_actuators.servo1Pos);
  display.print(F(" V2:"));
  display.print(g_actuators.servo2Pos);

  display.setCursor(0, 46);
  display.print(F("BOMBA:"));
  if (g_actuators.pumpOn) {
    display.print(F("ON"));
    // Parpadeo si está on
    if ((millis() / 500) % 2 == 0) {
      display.fillRect(40, 46, 20, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(42, 46);
      display.print(F("ON"));
      display.setTextColor(SSD1306_WHITE);
    }
  } else {
    display.print(F("OFF"));
  }

  // Modo
  display.setCursor(70, 46);
  display.print(F("M:"));
  if (g_mode == MODE_AUTO) display.print(F("AUTO"));
  else if (g_mode == MODE_MANUAL) display.print(F("MAN"));
  else display.print(F("SLP"));

  // ── Fila inferior: RSSI + Comando ──
  display.setCursor(0, 56);
  display.print(F("GW:"));
  display.print(g_telemetry.lastRSSI);
  display.print(F("dBm"));

  display.setCursor(64, 56);
  if (g_lastCommand != "NINGUNO") {
    display.print(g_lastCommand.substring(0, 10));
  }

  display.display();
}

void drawBar(int x, int y, int w, int h, float pct, uint16_t color) {
  int fillW = (int)(pct * (w - 2));
  if (fillW < 0) fillW = 0;
  if (fillW > w - 2) fillW = w - 2;
  
  display.drawRect(x, y, w, h, color);
  if (fillW > 0) {
    display.fillRect(x + 1, y + 1, fillW, h - 2, color);
  }
}

void showErrorScreen(const char* msg) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print(F("ERROR CRITICO:"));
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

void logMessage(const char* level, const char* msg) {
  Serial.print("["); Serial.print(level); Serial.print("] ");
  Serial.println(msg);
}
