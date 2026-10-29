/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║                    AUTOINVER NODO v2.0 - FIRMWARE                         ║
 * ║         Sistema Autónomo de Invernadero Móvil con LoRa 915MHz             ║
 * ║                                                                           ║
 * ║  Hardware: Heltec LoRa32 V2 + DHT22 + HumSuelo + Sharp IR + PCA9685       ║
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
unsigned long g_lastSensorRead    = 0;
unsigned long g_lastTx            = 0;
unsigned long g_lastDisplayUpd    = 0;
unsigned long g_lastSyncAttempt   = 0;
unsigned long g_manualOverrideUntil = 0;
unsigned long g_lastPumpStopMs    = 0;

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

// Calibración Serial USB
void handleSerialConsole();
void printCalibrationStatus();
void printHelpMenu();

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

  // Mostrar menú y estado de calibración en Serial
  printHelpMenu();
  printCalibrationStatus();

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
    now = millis();
  }

  // ── 2. Control automático (riego por pulsos y compuertas) ──
  // Si expiró el tiempo de override manual temporal, volver automáticamente a AUTO
  if (g_mode == MODE_MANUAL && g_manualOverrideUntil > 0 && now >= g_manualOverrideUntil) {
    g_mode = MODE_AUTO;
    g_manualOverrideUntil = 0;
    Serial.println(F("[MODO] ⏰ Fin del override manual (10 min). Retornando automáticamente a modo AUTO."));
  }

  if (g_mode == MODE_AUTO && now > g_manualOverrideUntil) {
    runControlLogic();
  }

  // ── 3. Gestión timeout bomba (fin de pulso de 1s y cortes de seguridad) ──
  managePumpTimeout(now);

  // ── 4. Transmisión LoRa cada TX_INTERVAL_MS ──
  if (now - g_lastTx >= TX_INTERVAL_MS) {
    g_lastTx = now;
    transmitTelemetry();
  }

  // ── 5. Sincronización de hora ──
  if (g_syncState != SYNC_OK) {
    handleTimeSync(now);
  }

  // ── 5. Escucha de comandos del gateway por LoRa ──
  listenForCommands();

  // ── 5b. Escucha de comandos de calibración por Serial USB ──
  handleSerialConsole();

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

    // Posición segura inicial: compuertas cerradas (0°)
    setServoAngle(CHANNEL_SERVO_A, g_config.servo1Min);
    setServoAngle(CHANNEL_SERVO_B, g_config.servo2Min);

    g_actuators.servo1Pos = g_config.servo1Min;
    g_actuators.servo2Pos = g_config.servo2Min;
  }

  Serial.println(F("[OK] Actuadores iniciados (compuertas en 0° cerradas)"));
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
  // Cargar calibración desde NVS, o usar defaults seguros
  g_config.sharpMinADC  = prefs.getInt("sharp_min", 800);
  g_config.sharpMaxADC  = prefs.getInt("sharp_max", 2500);
  g_config.soilMinADC   = prefs.getInt("soil_min", 1400);
  g_config.soilMaxADC   = prefs.getInt("soil_max", 3200);
  g_config.servo1Min    = prefs.getInt("s1_min", 0);
  g_config.servo1Max    = prefs.getInt("s1_max", 90);
  g_config.servo2Min    = prefs.getInt("s2_min", 0);
  g_config.servo2Max    = prefs.getInt("s2_max", 90);
  g_config.tempThresh1  = prefs.getFloat("temp_t1", 28.0f);
  g_config.tempThresh2  = prefs.getFloat("temp_t2", 32.0f);
  g_config.soilThresh   = prefs.getFloat("soil_thr", 40.0f);
  g_config.pumpDuration = prefs.getInt("pump_dur", 1000);

  // Sanitizar si NVS tenía guardados los valores antiguos incorrectos
  if (g_config.soilMaxADC <= g_config.soilMinADC || g_config.soilMaxADC == 4095) {
    g_config.soilMinADC = 1400;
    g_config.soilMaxADC = 3200;
  }
  if (g_config.sharpMaxADC <= g_config.sharpMinADC || g_config.sharpMaxADC == 3500) {
    g_config.sharpMinADC = 800;
    g_config.sharpMaxADC = 2500;
  }
  if (g_config.pumpDuration < 500) {
    g_config.pumpDuration = 1000;
    prefs.putInt("pump_dur", 1000);
  }

  Serial.println(F("[OK] Calibración cargada desde NVS"));
}

void saveCalibration() {
  prefs.putInt("sharp_min", g_config.sharpMinADC);
  prefs.putInt("sharp_max", g_config.sharpMaxADC);
  prefs.putInt("soil_min", g_config.soilMinADC);
  prefs.putInt("soil_max", g_config.soilMaxADC);
  prefs.putInt("s1_min", g_config.servo1Min);
  prefs.putInt("s1_max", g_config.servo1Max);
  prefs.putInt("s2_min", g_config.servo2Min);
  prefs.putInt("s2_max", g_config.servo2Max);
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

  // ── ADC con promedio rápido de 5 muestras (lectura inmediata sin lag de 20s) ──
  long soilSum = 0;
  long waterSum = 0;
  for (int i = 0; i < 5; i++) {
    soilSum += analogRead(PIN_SOIL);
    waterSum += analogRead(PIN_SHARP);
    delay(4);
  }
  g_sensors.soilADC  = (int)(soilSum / 5);
  g_sensors.waterADC = (int)(waterSum / 5);

  // ── Conversión a porcentajes calibrados ──
  g_sensors.soilPct  = adcToSoilPercent(g_sensors.soilADC);
  g_sensors.waterPct = adcToWaterPercent(g_sensors.waterADC);

  // Monitor serie en tiempo real
  Serial.print(F("[SENS] Suelo: ADC=")); Serial.print(g_sensors.soilADC);
  Serial.print(F(" (")); Serial.print(g_sensors.soilPct, 1);
  Serial.print(F("%) | Agua: ADC=")); Serial.print(g_sensors.waterADC);
  Serial.print(F(" (")); Serial.print(g_sensors.waterPct, 1);
  Serial.println(F("%)"));
}

float smoothADC(int* buf, int len) {
  if (len == 0) return 0;
  long sum = 0;
  for (int i = 0; i < len; i++) sum += buf[i];
  return (float)sum / len;
}

float adcToSoilPercent(int adc) {
  // Sensor capacitivo:
  // soilMaxADC = Seco (al aire) -> 0% humedad
  // soilMinADC = Mojado (en agua) -> 100% humedad
  int wet = g_config.soilMinADC;
  int dry = g_config.soilMaxADC;

  if (dry <= wet) {
    dry = 3200;
    wet = 1400;
  }

  int val = constrain(adc, wet, dry);
  float pct = (float)(dry - val) * 100.0f / (float)(dry - wet);
  return constrain(pct, 0.0f, 100.0f);
}

float adcToWaterPercent(int adc) {
  // Sensor Sharp IR:
  // sharpMinADC = Vacío (nivel de agua bajo / distancia lejana al sensor) -> 0%
  // sharpMaxADC = Lleno (nivel de agua alto / distancia cercana al sensor) -> 100%
  int empty = g_config.sharpMinADC;
  int full  = g_config.sharpMaxADC;

  if (full <= empty) {
    empty = 800;
    full  = 2500;
  }

  int val = constrain(adc, empty, full);
  float pct = (float)(val - empty) * 100.0f / (float)(full - empty);
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

  // Usar límites calibrados de cada compuerta
  int s1Closed = g_config.servo1Min;
  int s1Open   = g_config.servo1Max;
  int s2Closed = g_config.servo2Min;
  int s2Open   = g_config.servo2Max;

  int targetS1 = s1Closed;
  int targetS2 = s2Closed;

  if (t > g_config.tempThresh2) {
    targetS1 = s1Open;
    targetS2 = s2Open;
  } else if (t > g_config.tempThresh1) {
    targetS1 = s1Open;
    targetS2 = s2Closed;
  } else if (t < (g_config.tempThresh1 - TEMP_HYSTERESIS)) {
    targetS1 = s1Closed;
    targetS2 = s2Closed;
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
  bool soilDry     = g_sensors.soilPct < g_config.soilThresh;
  bool hasWater    = g_sensors.waterPct > WATER_MIN_PCT;
  bool pumpIdle    = !g_actuators.pumpOn;
  bool cooldownOk  = (millis() - g_lastPumpStopMs > PUMP_COOLDOWN_MS);  // Cooldown definido en config.h

  // Iniciar pulso de riego automático
  if (pumpIdle && soilDry && hasWater && cooldownOk) {
    activatePump(true, true);
    Serial.println(F("[AUTO] Pulso de riego iniciado (1s activo)"));
  }

  // Detener riego automático si el suelo ya se humedeció
  if (g_actuators.pumpOn && g_actuators.pumpAuto) {
    bool soilWetEnough = g_sensors.soilPct >= g_config.soilThresh;
    if (soilWetEnough) {
      activatePump(false, true);
      Serial.println(F("[AUTO] Riego detenido (humedad de suelo alcanzada)"));
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
    g_lastPumpStopMs = millis();
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

  // Obtener tiempo fresco para evitar desincronización con el inicio de la bomba
  unsigned long currentMs = millis();
  if (currentMs < g_actuators.pumpStartMs) return; // Protección ante desfase/underflow

  unsigned long elapsed = currentMs - g_actuators.pumpStartMs;

  // Duración del pulso de riego fijado en config.h (1 segundo)
  unsigned long dur = PUMP_PULSE_MS;

  if (elapsed >= dur) {
    activatePump(false, g_actuators.pumpAuto);
    Serial.print(F("[PUMP] Pulso apagado ("));
    Serial.print(elapsed / 1000.0, 1);
    Serial.print(F("s). Pausa de absorción ("));
    Serial.print(PUMP_COOLDOWN_MS / 1000);
    Serial.println(F("s)..."));
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IMPLEMENTACIÓN: SERVOS PCA9685
// ═══════════════════════════════════════════════════════════════════════════════

void setServoAngle(uint8_t channel, int angle) {
  if (!g_pcaOk) {
    Serial.println(F("[SERVO] Ignorado: PCA9685 no disponible"));
    return;
  }
  int clamped = constrain(angle, 0, 180);
  uint16_t pulse = map(clamped, 0, 180, SERVO_PULSE_MIN, SERVO_PULSE_MAX);
  pca.setPWM(channel, 0, pulse);
  
  Serial.print(F("[SERVO] Canal ")); Serial.print(channel);
  Serial.print(F(" -> ")); Serial.print(clamped);
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
  payload += "SOILPCT:" + String(g_sensors.soilPct, 1) + "|";
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
  
  if (act != "MODE") {
    // Activar override manual temporal por 10 minutos para comandos de actuadores
    g_manualOverrideUntil = millis() + MANUAL_OVERRIDE_MS;
    g_mode = MODE_MANUAL;
  }

  if (act == "BOMBA") {
    bool on = (valStr == "1");
    activatePump(on, false);
    sendACK("BOMBA");
    transmitTelemetry();

  } else if (act == "S1") {
    // Restringir al rango calibrado
    int angle = constrain(valStr.toInt(), g_config.servo1Min, g_config.servo1Max);
    setServoAngle(CHANNEL_SERVO_A, angle);
    g_actuators.servo1Pos = angle;
    sendACK("S1");
    transmitTelemetry();

  } else if (act == "S2") {
    int angle = constrain(valStr.toInt(), g_config.servo2Min, g_config.servo2Max);
    setServoAngle(CHANNEL_SERVO_B, angle);
    g_actuators.servo2Pos = angle;
    sendACK("S2");
    transmitTelemetry();

  } else if (act == "MODE") {
    if (valStr == "AUTO") {
      g_mode = MODE_AUTO;
      g_manualOverrideUntil = 0;
      Serial.println(F("[CMD] Modo cambiado a AUTO"));
      runControlLogic();
    } else if (valStr == "MANUAL") {
      g_mode = MODE_MANUAL;
      g_manualOverrideUntil = 0;
      Serial.println(F("[CMD] Modo cambiado a MANUAL"));
    } else if (valStr == "SLEEP") {
      g_mode = MODE_SLEEP;
      Serial.println(F("[CMD] Modo cambiado a SLEEP"));
    }
    sendACK("MODE");
    transmitTelemetry();

  } else if (act == "CAL") {
    // Formato VAL: PARAMETRO:valor  (ej. S1MIN:5, SOILMAX:3900)
    // Si valor = "NOW", captura la lectura ADC actual del sensor
    int sep = valStr.indexOf(':');
    if (sep > 0) {
      String param = valStr.substring(0, sep);
      String valorStr = valStr.substring(sep + 1);
      int valor = valorStr.toInt();

      if      (param == "S1MIN")    g_config.servo1Min = valor;
      else if (param == "S1MAX")    g_config.servo1Max = valor;
      else if (param == "S2MIN")    g_config.servo2Min = valor;
      else if (param == "S2MAX")    g_config.servo2Max = valor;
      else if (param == "SOILMIN")  g_config.soilMinADC  = (valorStr == "NOW") ? g_sensors.soilADC  : valor;
      else if (param == "SOILMAX")  g_config.soilMaxADC  = (valorStr == "NOW") ? g_sensors.soilADC  : valor;
      else if (param == "SHARPMIN") g_config.sharpMinADC = (valorStr == "NOW") ? g_sensors.waterADC : valor;
      else if (param == "SHARPMAX") g_config.sharpMaxADC = (valorStr == "NOW") ? g_sensors.waterADC : valor;
      else {
        Serial.print(F("[CAL] Parámetro desconocido: "));
        Serial.println(param);
        sendACK("CAL_ERR");
        return;
      }

      saveCalibration();
      Serial.print(F("[CAL] "));
      Serial.print(param);
      Serial.print(F(" = "));
      Serial.println(valorStr == "NOW" ? String((param.startsWith("SOIL") ? g_sensors.soilADC : g_sensors.waterADC)) : String(valor));
      sendACK("CAL");
    }
  } else if (act == "S1RAW") {
    // Movimiento libre (sin límite calibrado) — solo para menú de calibración
    int angle = constrain(valStr.toInt(), 0, 180);
    setServoAngle(CHANNEL_SERVO_A, angle);
    g_actuators.servo1Pos = angle;
    sendACK("S1RAW");

  } else if (act == "S2RAW") {
    int angle = constrain(valStr.toInt(), 0, 180);
    setServoAngle(CHANNEL_SERVO_B, angle);
    g_actuators.servo2Pos = angle;
    sendACK("S2RAW");
  }
}

void sendACK(const String& act) {
  g_telemetry.lastACK = act;
  Serial.print(F("[ACK] Comando confirmado: ")); Serial.println(act);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONSOLA SERIAL DE CALIBRACIÓN INTERACTIVA
// ═══════════════════════════════════════════════════════════════════════════════

void printCalibrationStatus() {
  Serial.println(F("\n══════════ [ESTADO Y CALIBRACIÓN ACTUAL] ══════════"));
  Serial.print(F(" ADC Suelo actual : ")); Serial.print(g_sensors.soilADC);
  Serial.print(F(" -> Humedad calculada: ")); Serial.print(g_sensors.soilPct, 1); Serial.println(F("%"));
  Serial.print(F("   [Calibración Suelo] SECO (0%): ")); Serial.print(g_config.soilMaxADC);
  Serial.print(F(" | MOJADO (100%): ")); Serial.println(g_config.soilMinADC);

  Serial.print(F(" ADC Agua actual  : ")); Serial.print(g_sensors.waterADC);
  Serial.print(F(" -> Nivel calculado: ")); Serial.print(g_sensors.waterPct, 1); Serial.println(F("%"));
  Serial.print(F("   [Calibración Agua]  VACÍO (0%): ")); Serial.print(g_config.sharpMinADC);
  Serial.print(F(" | LLENO (100%): ")); Serial.println(g_config.sharpMaxADC);
  
  Serial.print(F(" Temperatura: ")); Serial.print(g_sensors.temperature, 1);
  Serial.print(F(" °C | Humedad aire: ")); Serial.print(g_sensors.humidity, 1); Serial.println(F(" %"));
  Serial.print(F(" Bomba: ")); Serial.print(g_actuators.pumpOn ? F("ON") : F("OFF"));
  Serial.print(F(" | Compuertas V1: ")); Serial.print(g_actuators.servo1Pos);
  Serial.print(F("° | V2: ")); Serial.print(g_actuators.servo2Pos); Serial.println(F("°"));
  Serial.println(F("═══════════════════════════════════════════════════\n"));
}

void printHelpMenu() {
  Serial.println(F("\n╔══════════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║           COMANDOS DE CALIBRACIÓN POR PUERTO SERIAL              ║"));
  Serial.println(F("╠══════════════════════════════════════════════════════════════════╣"));
  Serial.println(F("║  'seco'   ó 'dry'   -> Calibra el suelo actual como SECO (0%)    ║"));
  Serial.println(F("║  'mojado' ó 'wet'   -> Calibra el suelo actual como MOJADO (100%)║"));
  Serial.println(F("║  'vacio'  ó 'empty' -> Calibra el agua actual como VACÍO (0%)    ║"));
  Serial.println(F("║  'lleno'  ó 'full'  -> Calibra el agua actual como LLENO (100%)  ║"));
  Serial.println(F("║  'auto'             -> Activa modo autónomo (riego automático)   ║"));
  Serial.println(F("║  'manual'           -> Activa modo manual (riego pausado)        ║"));
  Serial.println(F("║  'bomba on'/'off'   -> Prueba encendido manual de la bomba       ║"));
  Serial.println(F("║  'abrir' / 'cerrar' -> Prueba compuertas abiertas / cerradas     ║"));
  Serial.println(F("║  's1 90' / 's2 0'   -> Mueve un servo a cualquier ángulo (0-180) ║"));
  Serial.println(F("║  'status'           -> Muestra los valores ADC y calibración     ║"));
  Serial.println(F("║  'dur:10'           -> Ajusta segundos de riego (ej: dur:15)     ║"));
  Serial.println(F("║  'reset'            -> Restaura valores por defecto seguros      ║"));
  Serial.println(F("║  O manual: 's1_closed:90', 's1_open:0', 's2_closed:90', etc.     ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════════════════╝\n"));
}

void handleSerialConsole() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  String cmd = line;
  cmd.toLowerCase();

  if (cmd == "seco" || cmd == "dry" || cmd == "cal soil dry") {
    g_config.soilMaxADC = g_sensors.soilADC;
    saveCalibration();
    Serial.print(F("✅ [CAL] Suelo SECO (0%) calibrado a ADC = "));
    Serial.println(g_config.soilMaxADC);
    printCalibrationStatus();

  } else if (cmd == "mojado" || cmd == "wet" || cmd == "cal soil wet") {
    g_config.soilMinADC = g_sensors.soilADC;
    saveCalibration();
    Serial.print(F("✅ [CAL] Suelo MOJADO (100%) calibrado a ADC = "));
    Serial.println(g_config.soilMinADC);
    printCalibrationStatus();

  } else if (cmd == "vacio" || cmd == "empty" || cmd == "cal water empty") {
    g_config.sharpMinADC = g_sensors.waterADC;
    saveCalibration();
    Serial.print(F("✅ [CAL] Estanque VACÍO (0%) calibrado a ADC = "));
    Serial.println(g_config.sharpMinADC);
    printCalibrationStatus();

  } else if (cmd == "lleno" || cmd == "full" || cmd == "cal water full") {
    g_config.sharpMaxADC = g_sensors.waterADC;
    saveCalibration();
    Serial.print(F("✅ [CAL] Estanque LLENO (100%) calibrado a ADC = "));
    Serial.println(g_config.sharpMaxADC);
    printCalibrationStatus();

  } else if (cmd == "auto") {
    g_mode = MODE_AUTO;
    g_manualOverrideUntil = 0;
    Serial.println(F("🤖 [MODO] Cambiado a AUTO (Riego automático activado)"));
    printCalibrationStatus();

  } else if (cmd == "manual") {
    g_mode = MODE_MANUAL;
    activatePump(false, false);
    Serial.println(F("🖐️ [MODO] Cambiado a MANUAL (Riego automático pausado)"));
    printCalibrationStatus();

  } else if (cmd == "bomba on" || cmd == "pump on" || cmd == "regar") {
    activatePump(true, false);
    Serial.println(F("⚡ [BOMBA] Encendida manualmente"));

  } else if (cmd == "bomba off" || cmd == "pump off" || cmd == "parar") {
    activatePump(false, false);
    Serial.println(F("⚡ [BOMBA] Apagada"));

  } else if (cmd.startsWith("s1 ")) {
    int ang = cmd.substring(3).toInt();
    setServoAngle(CHANNEL_SERVO_A, ang);
    Serial.print(F("🎮 [TEST] Servo 1 movido a ")); Serial.print(ang); Serial.println(F("°"));

  } else if (cmd.startsWith("s2 ")) {
    int ang = cmd.substring(3).toInt();
    setServoAngle(CHANNEL_SERVO_B, ang);
    Serial.print(F("🎮 [TEST] Servo 2 movido a ")); Serial.print(ang); Serial.println(F("°"));

  } else if (cmd == "abrir") {
    setServoAngle(CHANNEL_SERVO_A, g_config.servo1Max);
    setServoAngle(CHANNEL_SERVO_B, g_config.servo2Max);
    g_actuators.servo1Pos = g_config.servo1Max;
    g_actuators.servo2Pos = g_config.servo2Max;
    Serial.println(F("🔓 Compuertas ABIERTAS"));

  } else if (cmd == "cerrar") {
    setServoAngle(CHANNEL_SERVO_A, g_config.servo1Min);
    setServoAngle(CHANNEL_SERVO_B, g_config.servo2Min);
    g_actuators.servo1Pos = g_config.servo1Min;
    g_actuators.servo2Pos = g_config.servo2Min;
    Serial.println(F("🔒 Compuertas CERRADAS"));

  } else if (cmd == "status" || cmd == "cal" || cmd == "estado") {
    printCalibrationStatus();

  } else if (cmd == "help" || cmd == "ayuda" || cmd == "?") {
    printHelpMenu();

  } else if (cmd == "reset") {
    g_config.soilMinADC  = 1400;
    g_config.soilMaxADC  = 3200;
    g_config.sharpMinADC = 800;
    g_config.sharpMaxADC = 2500;
    saveCalibration();
    Serial.println(F("🔄 [CAL] Calibración restablecida a valores seguros de fábrica."));
    printCalibrationStatus();

  } else if (cmd.indexOf(':') > 0) {
    int sep = cmd.indexOf(':');
    String k = cmd.substring(0, sep);
    int val = cmd.substring(sep + 1).toInt();
    if (k == "soil_dry")        { g_config.soilMaxADC  = val; saveCalibration(); Serial.println(F("✅ Suelo seco actualizado")); }
    else if (k == "soil_wet")   { g_config.soilMinADC  = val; saveCalibration(); Serial.println(F("✅ Suelo mojado actualizado")); }
    else if (k == "water_empty"){ g_config.sharpMinADC = val; saveCalibration(); Serial.println(F("✅ Agua vacía actualizada")); }
    else if (k == "water_full") { g_config.sharpMaxADC = val; saveCalibration(); Serial.println(F("✅ Agua llena actualizada")); }
    else if (k == "s1_closed")  { g_config.servo1Min   = val; saveCalibration(); Serial.println(F("✅ S1 Cerrada actualizado")); }
    else if (k == "s1_open")    { g_config.servo1Max   = val; saveCalibration(); Serial.println(F("✅ S1 Abierta actualizado")); }
    else if (k == "s2_closed")  { g_config.servo2Min   = val; saveCalibration(); Serial.println(F("✅ S2 Cerrada actualizado")); }
    else if (k == "s2_open")    { g_config.servo2Max   = val; saveCalibration(); Serial.println(F("✅ S2 Abierta actualizado")); }
    else if (k == "dur" || k == "pump_dur") {
      g_config.pumpDuration = (val < 100) ? (val * 1000) : val;
      saveCalibration();
      Serial.print(F("✅ Duración de riego actualizada a "));
      Serial.print(g_config.pumpDuration / 1000);
      Serial.println(F(" segundos (guardado en NVS)"));
    }
    else { Serial.println(F("❌ Comando manual no reconocido. Escribe 'ayuda'.")); }
    printCalibrationStatus();

  } else {
    Serial.print(F("Comando no reconocido: '")); Serial.print(line); Serial.println(F("'. Escribe 'ayuda' para ver opciones."));
  }
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
