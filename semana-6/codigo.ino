#include "UbidotsEsp32Mqtt.h"
#include <string.h>

/****************************************
 * Define Constants
 ****************************************/

const char *WIFI_SSID = "Nicole"; // Put here your Wi-Fi SSID
const char *WIFI_PASS = "nicole12345678"; // Put here your Wi-Fi password

const char *UBIDOTS_TOKEN = "BBUS-H8vebIQp5OF6ycbeXKLEglUmI6gxOi"; // Put here your Ubidots TOKEN
const char *DEVICE_LABEL = "pronderada_t17_grupo3"; // Put here your Device Label
const char *VARIABLE_LABEL_PHASE = "phase"; // Variable to store the traffic light phase
const char *VARIABLE_LABEL_MULTA = "multa"; // Variable to store if a car is fined
const char *VARIABLE_LABEL_LDR = "ldr"; // Variable to store the LDR value
const char *VARIABLE_LABEL_EVENT = "event"; // Variable to store events

Ubidots ubidots(UBIDOTS_TOKEN);

// ---------- PINOS (ajuste conforme sua montagem) ----------
#define LED_VERDE_A     16
#define LED_AMARELO_A   15
#define LED_VERMELHO_A  4

#define LED_VERDE_B     27
#define LED_AMARELO_B   26
#define LED_VERMELHO_B  14

#define LDR_PIN         34

// Sensor de distância (apenas para B)
#define TRIG_B          5
#define ECHO_B          22

// ---------- PARÂMETROS ----------
const int LIMIAR_NOITE    = 100;   // ajuste após testar LDR
const int QUEDA_MINIMA    = 200;   // queda no LDR que indica passagem de carro (A)
const int DISTANCIA_MULTA = 10;    // cm para multa (ajuste conforme montagem)

// tempos em ms
const unsigned long A_VERDE_BASE   = 4000;
const unsigned long A_VERDE_EXTRA  = 2000; // quanto estende quando detecta carro
const unsigned long A_AMARELO_T    = 1000;

const unsigned long B_VERDE_BASE   = 4000;
const unsigned long B_AMARELO_T    = 1000;

enum Phase {
  PHASE_A_GREEN_B_RED,
  PHASE_A_YELLOW_B_RED,
  PHASE_A_RED_B_GREEN,
  PHASE_A_RED_B_YELLOW,
  PHASE_NIGHT_BLINK
};

Phase phase = PHASE_A_GREEN_B_RED;
unsigned long phaseStart = 0;
unsigned long phaseDuration = 0;

// LDR
int ldrPrev = 0;
int ldrNow = 0;
bool inNight = false;

// evita múltiplas extensões por ciclo
bool A_extended = false;

// evita múltiplas multas por evento
bool multa_active = false;

// Ubidots
unsigned long lastUbidotsUpdate = 0;
const int UBIDOTS_UPDATE_FREQUENCY = 5000; // 5 seconds

/****************************************
 * Auxiliar Functions
 ****************************************/

void callback(char *topic, byte *payload, unsigned int length)
{
  // In this example, we are not subscribing to any topic, so this function will not be called.
}

// leitura HC-SR04
long readDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long dur = pulseIn(echoPin, HIGH, 30000);
  if (dur == 0) return -1;
  return (long)(dur * 0.034 / 2.0);
}

void setA(bool g, bool y, bool r){
  digitalWrite(LED_VERDE_A, g?HIGH:LOW);
  digitalWrite(LED_AMARELO_A, y?HIGH:LOW);
  digitalWrite(LED_VERMELHO_A, r?HIGH:LOW);
}
void setB(bool g, bool y, bool r){
  digitalWrite(LED_VERDE_B, g?HIGH:LOW);
  digitalWrite(LED_AMARELO_B, y?HIGH:LOW);
  digitalWrite(LED_VERMELHO_B, r?HIGH:LOW);
}

void enterPhase(Phase p, unsigned long duration){
  phase = p;
  phaseStart = millis();
  phaseDuration = duration;
  // reset flag de extensão quando entrar em fase que não seja A_GREEN
  if (p != PHASE_A_GREEN_B_RED) A_extended = false;
  char event_context[50];
  switch(p){
    case PHASE_A_GREEN_B_RED:
      setA(true,false,false);
      setB(false,false,true);
      strcpy(event_context, "A VERDE - B VERMELHO");
      ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
      break;
    case PHASE_A_YELLOW_B_RED:
      setA(false,true,false);
      setB(false,false,true);
      strcpy(event_context, "A AMARELO - B VERMELHO");
      ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
      break;
    case PHASE_A_RED_B_GREEN:
      setA(false,false,true);
      setB(true,false,false);
      strcpy(event_context, "A VERMELHO - B VERDE");
      ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
      break;
    case PHASE_A_RED_B_YELLOW:
      setA(false,false,true);
      setB(false,true,false);
      strcpy(event_context, "A VERMELHO - B AMARELO");
      ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
      break;
    case PHASE_NIGHT_BLINK:
      strcpy(event_context, "MODO NOTURNO");
      ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
      break;
  }
}

// média de N leituras rápidas do LDR para reduzir ruído
int readLdrAverage(int samples=5, int delayUs=1000) {
  long sum = 0;
  for (int i=0;i<samples;i++){
    sum += analogRead(LDR_PIN);
    if (delayUs>0) delayMicroseconds(delayUs);
  }
  return (int)(sum / samples);
}

unsigned long lastNightToggle = 0;
bool nightOn = false;

void setup() {
  Serial.begin(115200);
  delay(50);

  ubidots.setDebug(false); // Disable debug messages
  ubidots.connectToWifi(WIFI_SSID, WIFI_PASS);
  ubidots.setCallback(callback);
  ubidots.setup();
  ubidots.reconnect();

  pinMode(LED_VERDE_A, OUTPUT);
  pinMode(LED_AMARELO_A, OUTPUT);
  pinMode(LED_VERMELHO_A, OUTPUT);

  pinMode(LED_VERDE_B, OUTPUT);
  pinMode(LED_AMARELO_B, OUTPUT);
  pinMode(LED_VERMELHO_B, OUTPUT);

  pinMode(LDR_PIN, INPUT);

  pinMode(TRIG_B, OUTPUT);
  pinMode(ECHO_B, INPUT);

  // leitura inicial
  ldrPrev = readLdrAverage(8,800);
  ubidots.add(VARIABLE_LABEL_LDR, ldrPrev);
  char event_context[50];
  strcpy(event_context, "Setup pronto");
  ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
  ubidots.publish(DEVICE_LABEL);


  enterPhase(PHASE_A_GREEN_B_RED, A_VERDE_BASE);
}

void loop() {
  unsigned long now = millis();

  if (!ubidots.connected()) {
    ubidots.reconnect();
  }

  // leitura média do LDR
  ldrNow = readLdrAverage(5,800);
  bool shouldBeNight = (ldrNow < LIMIAR_NOITE);

  // --- modo noturno ---
  if (shouldBeNight && phase != PHASE_NIGHT_BLINK) {
    inNight = true;
    enterPhase(PHASE_NIGHT_BLINK, 0);
  }

  if (phase == PHASE_NIGHT_BLINK) {
    if (now - lastNightToggle >= 500) {
      lastNightToggle = now;
      nightOn = !nightOn;
      digitalWrite(LED_VERDE_A, LOW);
      digitalWrite(LED_VERMELHO_A, LOW);
      digitalWrite(LED_VERDE_B, LOW);
      digitalWrite(LED_VERMELHO_B, LOW);
      digitalWrite(LED_AMARELO_A, nightOn?HIGH:LOW);
      digitalWrite(LED_AMARELO_B, nightOn?HIGH:LOW);
    }
    // sair do noturno
    if (!shouldBeNight) {
      inNight = false;
      // retomar ciclo de forma previsível: A verde
      enterPhase(PHASE_A_GREEN_B_RED, A_VERDE_BASE);
      ldrPrev = ldrNow;
    }
    // No modo noturno, não faz as outras verificações
  } else { // --- caso normal: verificar transições ---
    if (now - phaseStart >= phaseDuration) {
      switch(phase) {
        case PHASE_A_GREEN_B_RED:
          enterPhase(PHASE_A_YELLOW_B_RED, A_AMARELO_T);
          break;
        case PHASE_A_YELLOW_B_RED:
          enterPhase(PHASE_A_RED_B_GREEN, B_VERDE_BASE);
          break;
        case PHASE_A_RED_B_GREEN:
          enterPhase(PHASE_A_RED_B_YELLOW, B_AMARELO_T);
          break;
        case PHASE_A_RED_B_YELLOW:
          enterPhase(PHASE_A_GREEN_B_RED, A_VERDE_BASE);
          break;
        default:
          enterPhase(PHASE_A_GREEN_B_RED, A_VERDE_BASE);
          break;
      }
      // após mudar de fase, atualiza ldrPrev para evitar detecção imediata
      ldrPrev = ldrNow;
    }
  }

  // --- dentro da fase: comportamentos específicos ---
  // 1) Se estamos na fase em que A está VERDE (PHASE_A_GREEN_B_RED), detectar queda brusca para estender
  if (phase == PHASE_A_GREEN_B_RED) {
    // só permite uma extensão por ciclo
    if (!A_extended) {
      if (ldrPrev - ldrNow > QUEDA_MINIMA) {
        // aplica extensão: aumenta phaseDuration em A_VERDE_EXTRA
        phaseDuration += A_VERDE_EXTRA;
        A_extended = true;
        char event_context[60];
        strcpy(event_context, "Queda detectada: estendendo VERDE (A)");
        ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
        // pequeno debounce para evitar múltiplas detecções seguidas
        delay(120);
      }
    }
  }

  // 2) Se B está vermelho (PHASE_A_GREEN_B_RED ou PHASE_A_YELLOW_B_RED), checar multa
  if (phase == PHASE_A_GREEN_B_RED || phase == PHASE_A_YELLOW_B_RED) {
    static unsigned long lastCheck = 0;
    if (now - lastCheck >= 200) {
      lastCheck = now;
      long d = readDistanceCM(TRIG_B, ECHO_B);
      bool fine_detected_now = (d > 0 && d < DISTANCIA_MULTA);

      if (fine_detected_now != multa_active) {
        multa_active = fine_detected_now;
        if (multa_active) {
          char event_context[60];
          strcpy(event_context, "MULTA no semáforo B! Carro passou no vermelho.");
          ubidots.add(VARIABLE_LABEL_EVENT, 1, event_context);
          ubidots.add(VARIABLE_LABEL_MULTA, 1);
        } else {
          ubidots.add(VARIABLE_LABEL_MULTA, 0);
        }
      }
    }
  }

  // Envia dados para o Ubidots a cada 5 segundos
  if (now - lastUbidotsUpdate > UBIDOTS_UPDATE_FREQUENCY) {
    lastUbidotsUpdate = now;
    ubidots.add(VARIABLE_LABEL_PHASE, phase);
    ubidots.add(VARIABLE_LABEL_LDR, ldrNow);
    ubidots.publish(DEVICE_LABEL);
  }

  // atualiza ldrPrev para próxima iteração
  ldrPrev = ldrNow;

  ubidots.loop();
  delay(8);
}