#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include "time.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ---------- Config WiFi & Firebase ----------
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define FIREBASE_HOST "https://pet-feeder-automatico-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "7z8Mfw1bmBkBzqbgu9xgA3N1DQ1e58MCSRQnohjD"

// ---------- Pinos ----------
#define SERVO_PIN 17
#define GREEN_LED_PIN 42
#define YELLOW_LED_PIN 40
#define RED_LED_PIN 41
#define TRIG_PIN 7
#define ECHO_PIN 6

// ---------- Objetos ----------
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- Variáveis ----------
String nivelRacaoAtual = "";
float distanciaAnterior = -1;

unsigned long lastSensorRead = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastScheduleCheck = 0;

// ---------- Funções ----------
float medirDistanciaCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms
  float distancia = duracao * 0.034 / 2; // velocidade som ~0.034 cm/us
  return distancia;
}

void updateLedStatus(String nivel) {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);

  if (nivel == "cheio") digitalWrite(GREEN_LED_PIN, HIGH);
  else if (nivel == "medio") digitalWrite(YELLOW_LED_PIN, HIGH);
  else if (nivel == "vazio") digitalWrite(RED_LED_PIN, HIGH);
}

void updateLcdDisplay(String nivel, float distancia) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Nivel: " + nivel);
  lcd.setCursor(0, 1);
  lcd.print("Dist: " + String(distancia, 1) + " cm");
}

// ---------- Firebase ----------
void updateFirebaseStatus(String nivel, float distancia) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(FIREBASE_HOST) + "/pet_feeder.json?auth=" + FIREBASE_AUTH;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Cria JSON com nivel_racao e peso_gramas
    DynamicJsonDocument doc(256);
    doc["nivel_racao"] = nivel;
    doc["peso_gramas"] = (int)distancia;

    String json;
    serializeJson(doc, json);

    int httpCode = http.PATCH(json);
    if (httpCode > 0) {
      Serial.println("Firebase atualizado: " + json);
    } else {
      Serial.println("Erro ao atualizar Firebase!");
    }
    http.end();
  }
}

void resetFirebaseComando() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(FIREBASE_HOST) + "/pet_feeder/comando_liberar.json?auth=" + FIREBASE_AUTH;
    http.begin(url);
    http.PUT("false");
    http.end();
  }
}

void logAlimentacaoFirebase() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(FIREBASE_HOST) + "/pet_feeder/historico.json?auth=" + FIREBASE_AUTH;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Captura data/hora atual
    time_t agora;
    struct tm timeinfo;
    time(&agora);
    localtime_r(&agora, &timeinfo);

    char buffer[30];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);

    // Cria JSON com evento + timestamp
    DynamicJsonDocument doc(128);
    doc["evento"] = "Alimentar pet";
    doc["timestamp"] = buffer;

    String json;
    serializeJson(doc, json);

    // POST adiciona um novo nó no histórico
    int httpCode = http.POST(json);
    if (httpCode > 0) {
      Serial.println("Histórico registrado: " + json);
    } else {
      Serial.println("Erro ao registrar histórico!");
    }

    http.end();
  }
}

void liberarRacao() {
  Serial.println("Liberando ração...");
  lcd.clear();
  lcd.print("Liberando Racao");
  lcd.setCursor(0, 1);
  lcd.print("Aguarde...");
  
  myServo.write(90); 
  delay(1500); 
  myServo.write(0);
  
  logAlimentacaoFirebase();
  
  delay(500);
  updateLcdDisplay(nivelRacaoAtual, distanciaAnterior);
}

void checarAgendamentos() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(FIREBASE_HOST) + "/pet_feeder/agendamentos.json?auth=" + FIREBASE_AUTH;
    http.begin(url);
    
    if (http.GET() > 0) {
      String payload = http.getString();
      
      if (payload != "null" && payload.length() > 2) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        
        // Pega hora atual
        time_t agora;
        struct tm timeinfo;
        time(&agora);
        localtime_r(&agora, &timeinfo);
        
        char horaAtual[6];
        strftime(horaAtual, sizeof(horaAtual), "%H:%M", &timeinfo);
        
        // Percorre agendamentos
        JsonObject obj = doc.as<JsonObject>();
        for (JsonPair kv : obj) {
          String horarioAgendado = kv.value().as<String>();
          
          if (horarioAgendado == horaAtual) {
            Serial.println("Horário agendado detectado: " + horarioAgendado);
            liberarRacao();
            
            // Remove agendamento após executar
            String deleteUrl = String(FIREBASE_HOST) + "/pet_feeder/agendamentos/" + kv.key().c_str() + ".json?auth=" + FIREBASE_AUTH;
            HTTPClient httpDel;
            httpDel.begin(deleteUrl);
            httpDel.sendRequest("DELETE");
            httpDel.end();
            
            break;
          }
        }
      }
    }
    http.end();
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(38, 39);  // I2C pins

  lcd.init();
  lcd.backlight();
  lcd.print("Pet Feeder");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  myServo.attach(SERVO_PIN);
  myServo.write(0);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\nConectado ao WiFi!");

  // Configura timezone para Brasil (UTC-3)
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  delay(2000);
  
  Serial.println("Sistema pronto!");
}

// ---------- Loop ----------
void loop() {
  // Leitura do sensor a cada 2s
  if (millis() - lastSensorRead > 2000) {
    lastSensorRead = millis();

    float distancia = medirDistanciaCM();
    Serial.printf("Distancia medida: %.2f cm\n", distancia);

 
    String novoNivel;
    if (distancia <= 100) {
      novoNivel = "cheio";      // Muito próximo = recipiente cheio
    } else if (distancia <= 250) {
      novoNivel = "medio";      // Distância média = meio cheio
    } else {
      novoNivel = "vazio";      // Longe = recipiente vazio
    }

    if (novoNivel != nivelRacaoAtual || abs(distancia - distanciaAnterior) > 1) {
      Serial.printf("--- Status mudou! Dist: %.2f cm, Nivel: %s ---\n", distancia, novoNivel.c_str());

      nivelRacaoAtual = novoNivel;
      distanciaAnterior = distancia;

      updateLedStatus(novoNivel);
      updateLcdDisplay(novoNivel, distancia);
      updateFirebaseStatus(novoNivel, distancia);
    }
  }

  // Checar comando no Firebase a cada 2s
  if (millis() - lastCommandCheck > 2000) {
    lastCommandCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = String(FIREBASE_HOST) + "/pet_feeder/comando_liberar.json?auth=" + String(FIREBASE_AUTH);
      http.begin(url);
      
      if (http.GET() > 0) {
        String response = http.getString();
        response.trim();
        
        if (response == "true") {
          Serial.println("Comando para liberar ração recebido!");
          liberarRacao();
          resetFirebaseComando();
        }
      }
      http.end();
    }
  }

  // Checar agendamentos a cada 30 segundos
  if (millis() - lastScheduleCheck > 30000) {
    lastScheduleCheck = millis();
    checarAgendamentos();
  }
}