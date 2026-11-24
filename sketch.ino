/*
 * Smart Bin IoT — Cesto de Residuos Inteligente con Deteccion de Fallas
 * Alumno: Ricardo Gabriel Diaz | Comision ACM5AV
 * Materia: PP: Integracion Tecnologica (S0D53) — Parcial 2 (Noviembre 2026)
 * Docente: Sebastian Suarez Cores
 *
 * Componentes:
 * - ESP32 DevKit v1 (WiFi Wokwi-GUEST)
 * - Sensor 1: HC-SR04 externo (apertura sin contacto, <= 20 cm)
 * - Sensor 2: HC-SR04 interno (medicion de nivel de llenado)
 * - Actuador 1: Servo SG90 (tapa: 0° cerrada / 90° abierta)
 * - Actuador 2: LCD 16x2 I2C (direccion 0x27)
 * - Actuador 3: Semaforo 3 LEDs (Verde, Amarillo, Rojo con R=220 ohm)
 * - Actuador 4: Buzzer (avisos sonoros)
 * - Simulacion de fallas: 2 slide switches (sensor desconectado y servo trabado)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// --- Configuracion de red y Telegram ---
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const int   WIFI_CANAL = 6;

// Credenciales para el bot de Telegram (se cargan antes de probar)
const char* TELEGRAM_TOKEN   = "PEGAR_TOKEN_AQUI";
const char* TELEGRAM_CHAT_ID = "PEGAR_CHAT_ID_AQUI";
bool telegramHabilitado = false;

// --- Pines ESP32 ---
// HC-SR04 externo (deteccion de mano)
const int PIN_TRIG_MANO  = 13;
const int PIN_ECHO_MANO  = 14;

// HC-SR04 interno (nivel de residuos)
const int PIN_TRIG_NIVEL = 26;
const int PIN_ECHO_NIVEL = 27;

// Actuadores
const int PIN_SERVO      = 18;
const int PIN_BUZZER     = 19;
const int PIN_LED_VERDE  = 25;
const int PIN_LED_AMAR   = 33;
const int PIN_LED_ROJO   = 32;

// Interruptores para simular fallas en Wokwi (activos en LOW con INPUT_PULLUP)
const int PIN_SW_FALLA_SENSOR = 4;  // Simula sensor desconectado
const int PIN_SW_FALLA_TAPA   = 5;  // Simula servo atascado

// --- Parametros y calibracion del cesto ---
const float DISTANCIA_APERTURA_CM = 20.0; // Abre a 20 cm o menos
const float ALTURA_CESTO_VACIO_CM = 40.0; // Distancia al fondo = 0% lleno
const float ALTURA_CESTO_LLENO_CM = 5.0;  // Distancia al tope = 100% lleno
const int   UMBRAL_MEDIO_PCT      = 60;   // Amarillo desde 60%
const int   UMBRAL_LLENO_PCT      = 85;   // Rojo y alerta Telegram desde 85%
const int   UMBRAL_BLOQUEO_PCT    = 95;   // Bloqueo de apertura desde 95%
const int   ANGULO_CERRADO        = 0;
const int   ANGULO_ABIERTO        = 90;
const unsigned long TIEMPO_TAPA_ABIERTA_MS = 4000;

// --- Variables y objetos ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servoTapa;

bool tapaAbierta = false;
unsigned long momentoApertura = 0;

// Banderas para avisar por Telegram solo en el flanco (no spamear en cada loop)
bool alertaLlenoEnviada       = false;
bool alertaFallaSensorEnviada = false;
bool alertaFallaTapaEnviada   = false;
bool fallaTapaActiva          = false;

// Reemplaza espacios y saltos para armar la URL del bot
String codificarURL(const String& texto) {
  String out = "";
  for (size_t i = 0; i < texto.length(); i++) {
    char c = texto.charAt(i);
    if (c == ' ') out += "%20";
    else if (c == '\n') out += "%0A";
    else out += c;
  }
  return out;
}

// Envia un mensaje por HTTPS a la API de Telegram
void enviarTelegram(const String& mensaje) {
  Serial.println("[TELEGRAM] " + mensaje);

  if (!telegramHabilitado) {
    Serial.println("[TELEGRAM] Modo local (falta cargar TOKEN y CHAT_ID).");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEGRAM] Sin conexion WiFi, se omite el envio.");
    return;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // No usamos certificados raiz para no saturar memoria en el ESP32
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_TOKEN + "/sendMessage";

  http.begin(cliente, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String datos = String("chat_id=") + TELEGRAM_CHAT_ID + "&text=" + codificarURL(mensaje);

  int codigoHTTP = http.POST(datos);
  Serial.printf("[TELEGRAM] Respuesta HTTP: %d\n", codigoHTTP);
  http.end();
}

// Mide distancia en cm; si timeout (sensor desconectado) devuelve -1.0
float medirDistanciaCm(int pinTrig, int pinEcho) {
  digitalWrite(pinTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);

  // Timeout de 30 ms para no colgar el loop si el sensor no devuelve eco
  unsigned long duracion = pulseIn(pinEcho, HIGH, 30000);
  if (duracion == 0) {
    return -1.0;
  }
  return (duracion * 0.0343) / 2.0;
}

void emitirPitidos(int cantidad, int duracionMs) {
  for (int i = 0; i < cantidad; i++) {
    tone(PIN_BUZZER, 1200, duracionMs);
    delay(duracionMs + 80);
  }
}

void actualizarSemaforo(bool verde, bool amarillo, bool rojo) {
  digitalWrite(PIN_LED_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_LED_AMAR,  amarillo ? HIGH : LOW);
  digitalWrite(PIN_LED_ROJO,  rojo ? HIGH : LOW);
}

void mostrarLCD(const String& linea1, const String& linea2) {
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  for (int i = linea1.length(); i < 16; i++) lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print(linea2);
  for (int i = linea2.length(); i < 16; i++) lcd.print(' ');
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Iniciando Smart Bin IoT ---");

  // Pines de sensores
  pinMode(PIN_TRIG_MANO,  OUTPUT);
  pinMode(PIN_ECHO_MANO,  INPUT);
  pinMode(PIN_TRIG_NIVEL, OUTPUT);
  pinMode(PIN_ECHO_NIVEL, INPUT);

  // Pines de actuadores
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR,  OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);

  // Switches para inyectar fallas
  pinMode(PIN_SW_FALLA_SENSOR, INPUT_PULLUP);
  pinMode(PIN_SW_FALLA_TAPA,   INPUT_PULLUP);

  // LCD por bus I2C (SDA=21, SCL=22)
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  mostrarLCD("Smart Bin IoT", "Iniciando...");

  // Posicion inicial del servo (tapa cerrada)
  servoTapa.attach(PIN_SERVO, 500, 2400);
  servoTapa.write(ANGULO_CERRADO);

  // Si se pegaron credenciales reales habilitamos Telegram
  if (String(TELEGRAM_TOKEN) != "PEGAR_TOKEN_AQUI" && String(TELEGRAM_CHAT_ID) != "PEGAR_CHAT_ID_AQUI") {
    telegramHabilitado = true;
  }

  // Conexion WiFi a Wokwi-GUEST con timeout para que no quede clavado
  mostrarLCD("Conectando WiFi", "Wokwi-GUEST...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
  Serial.print("Buscando red WiFi");

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 60) {
    delay(250);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
    mostrarLCD("WiFi OK", WiFi.localIP().toString());
    enviarTelegram("Smart Bin IoT conectado y listo.");
  } else {
    Serial.println("\nSin WiFi. Funciona en modo autonomo local.");
    mostrarLCD("Modo Local", "Sin conexion");
  }
  delay(1200);
}

void loop() {
  // 1. Verificamos si estan activos los switches de falla (LOW por pull-up)
  bool simularFallaSensor = (digitalRead(PIN_SW_FALLA_SENSOR) == LOW);
  bool simularFallaTapa   = (digitalRead(PIN_SW_FALLA_TAPA)   == LOW);

  // 2. Leemos distancias de ambos sensores
  float distNivel = simularFallaSensor ? -1.0 : medirDistanciaCm(PIN_TRIG_NIVEL, PIN_ECHO_NIVEL);
  float distMano  = medirDistanciaCm(PIN_TRIG_MANO, PIN_ECHO_MANO);

  // --- Falla 1: Sensor de llenado desconectado o sin lectura ---
  if (distNivel < 0 || distNivel > 400.0) {
    servoTapa.write(ANGULO_CERRADO);
    tapaAbierta = false;
    actualizarSemaforo(false, false, true);
    mostrarLCD("ERR: SENSOR", "MODO SEGURO");
    Serial.println("[FALLA] Sensor de nivel sin lectura valida.");

    if (!alertaFallaSensorEnviada) {
      emitirPitidos(3, 120);
      enviarTelegram("ALERTA: Falla en sensor de nivel. Cesto en Modo Seguro.");
      alertaFallaSensorEnviada = true;
    }
    delay(500);
    return;
  } else if (alertaFallaSensorEnviada) {
    alertaFallaSensorEnviada = false;
    enviarTelegram("INFO: Sensor de nivel normalizado.");
  }

  // 3. Calculo de porcentaje de llenado (40 cm = 0%, 5 cm = 100%)
  float pctFloat = ((ALTURA_CESTO_VACIO_CM - distNivel) / (ALTURA_CESTO_VACIO_CM - ALTURA_CESTO_LLENO_CM)) * 100.0;
  int porcentaje = constrain((int)round(pctFloat), 0, 100);

  // --- Falla 2: Restablecimiento de servo si se destraba el switch ---
  if (!simularFallaTapa && fallaTapaActiva) {
    fallaTapaActiva = false;
    alertaFallaTapaEnviada = false;
    enviarTelegram("INFO: Servo de tapa restablecido.");
  }

  if (fallaTapaActiva) {
    actualizarSemaforo(false, false, true);
    mostrarLCD("ERR: TAPA", "REVISAR SERVO");
    delay(500);
    return;
  }

  // 4. Semáforo y notificacion de llenado por Telegram
  if (porcentaje >= UMBRAL_LLENO_PCT) {
    actualizarSemaforo(false, false, true);
    if (!alertaLlenoEnviada) {
      emitirPitidos(2, 150);
      enviarTelegram("ALERTA: Cesto al " + String(porcentaje) + "% de capacidad. Requiere vaciado.");
      alertaLlenoEnviada = true;
    }
  } else if (porcentaje >= UMBRAL_MEDIO_PCT) {
    actualizarSemaforo(false, true, false);
    if (alertaLlenoEnviada && porcentaje < 50) alertaLlenoEnviada = false;
  } else {
    actualizarSemaforo(true, false, false);
    if (alertaLlenoEnviada) {
      alertaLlenoEnviada = false;
      enviarTelegram("INFO: Cesto vaciado (" + String(porcentaje) + "%).");
    }
  }

  // 5. Apertura automatica sin contacto
  if (!tapaAbierta && distMano > 0 && distMano <= DISTANCIA_APERTURA_CM) {
    if (porcentaje >= UMBRAL_BLOQUEO_PCT) {
      // Si esta al 95% o mas, no abre para evitar desborde
      mostrarLCD("LLENO: " + String(porcentaje) + "%", "NO ABRE TAPA");
      emitirPitidos(2, 80);
      Serial.println("[AVISO] Apertura bloqueada por capacidad maxima.");
    } else if (simularFallaTapa) {
      // Simula traba del servo al intentar abrir
      fallaTapaActiva = true;
      actualizarSemaforo(false, false, true);
      mostrarLCD("ERR: TAPA", "REVISAR SERVO");
      Serial.println("[FALLA] El servo no respondio a la orden de apertura.");
      if (!alertaFallaTapaEnviada) {
        emitirPitidos(3, 180);
        enviarTelegram("ALERTA: Tapa trabada o falla de servo al intentar abrir.");
        alertaFallaTapaEnviada = true;
      }
      return;
    } else {
      // Apertura normal
      servoTapa.write(ANGULO_ABIERTO);
      tapaAbierta = true;
      momentoApertura = millis();
      tone(PIN_BUZZER, 1000, 100);
      Serial.println("[OK] Mano detectada -> Tapa abierta.");
    }
  }

  // 6. Cierre automatico a los 4 segundos si la mano ya no esta
  if (tapaAbierta && (millis() - momentoApertura >= TIEMPO_TAPA_ABIERTA_MS)) {
    if (distMano < 0 || distMano > DISTANCIA_APERTURA_CM) {
      servoTapa.write(ANGULO_CERRADO);
      tapaAbierta = false;
      Serial.println("[OK] Tiempo cumplido -> Tapa cerrada.");
    }
  }

  // 7. Visualizacion en LCD y Monitor Serie
  String estadoTapa = tapaAbierta ? "Tapa: ABIERTA" : (porcentaje >= UMBRAL_BLOQUEO_PCT ? "LLENO - BLOQUEO" : "Tapa: CERRADA");
  mostrarLCD("Llenado: " + String(porcentaje) + "%", estadoTapa);

  Serial.print("Llenado: ");
  Serial.print(porcentaje);
  Serial.print("% (");
  Serial.print(distNivel, 1);
  Serial.print(" cm) | Mano: ");
  Serial.print(distMano, 1);
  Serial.print(" cm | ");
  Serial.println(estadoTapa);

  delay(300);
}
