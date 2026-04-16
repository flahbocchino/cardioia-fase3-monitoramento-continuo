// =============================================================
// CardioIA - Fase 3 | Parte 1: Edge Computing
// ESP32 + DHT22 (temperatura/umidade) + Botão (BPM simulado)
// Lógica de resiliência offline com fila circular em memória RAM
// Simulação de conectividade Wi-Fi via variável booleana
// =============================================================

#include <DHT.h>

// ----- Configuração dos pinos -----
#define DHTPIN 4          // Pino de dados do DHT22
#define DHTTYPE DHT22     // Tipo do sensor de temperatura/umidade
#define BTN_PIN 15        // Pino do botão de pressão (simula batimento cardíaco)
#define LED_PIN 2         // LED onboard do ESP32 (feedback visual)

DHT dht(DHTPIN, DHTTYPE);

// ----- Configuração da fila de resiliência offline -----
// Capacidade de 50 leituras (a cada 5s = ~4 minutos de buffer offline)
// Wokwi não suporta SPIFFS persistente; RAM é a alternativa funcional
const int FILA_MAX = 50;

struct Leitura {
  float temperatura;
  float umidade;
  int bpm;
  unsigned long timestamp_ms;
};

Leitura fila[FILA_MAX];
int fila_inicio  = 0;
int fila_fim     = 0;
int fila_tamanho = 0;

// ----- Simulação de conectividade Wi-Fi -----
bool wifi_conectado = false;

// ----- Contagem de batimentos (botão) -----
int contagem_btn = 0;                      // Pressões na janela atual
int ultimo_bpm_calculado = 0;              // CORRIGIDO: persiste o último BPM válido
                                           // entre leituras, evitando BPM=0 espúrio
unsigned long ultimo_calculo_bpm = 0;
const unsigned long JANELA_BPM_MS = 10000; // Janela de 10s para calcular BPM

// Debounce do botão
bool btn_estado_anterior = HIGH;
unsigned long ultimo_debounce = 0;
const unsigned long DEBOUNCE_MS = 50;

// ----- Temporização das leituras -----
unsigned long ultima_leitura = 0;
const unsigned long INTERVALO_LEITURA_MS = 5000;  // Leitura a cada 5 segundos

// ----- Temporização do toggle Wi-Fi (simulação) -----
unsigned long ultimo_toggle_wifi = 0;
const unsigned long INTERVALO_TOGGLE_MS = 30000;  // Alterna conexão a cada 30s

// =============================================================
// FUNÇÕES AUXILIARES — FILA CIRCULAR
// =============================================================

void fila_push(Leitura l) {
  fila[fila_fim] = l;
  fila_fim = (fila_fim + 1) % FILA_MAX;

  if (fila_tamanho < FILA_MAX) {
    fila_tamanho++;
  } else {
    // Fila cheia: descarta o registro mais antigo
    fila_inicio = (fila_inicio + 1) % FILA_MAX;
    Serial.println("[AVISO] Fila cheia. Registro mais antigo descartado.");
  }
}

bool fila_vazia() {
  return fila_tamanho == 0;
}

Leitura fila_pop() {
  Leitura l = fila[fila_inicio];
  fila_inicio = (fila_inicio + 1) % FILA_MAX;
  fila_tamanho--;
  return l;
}

// =============================================================
// FUNÇÃO: Enviar leitura para a "nuvem" via Serial (simulação)
// Na Parte 2 esta função será substituída pelo envio MQTT real
// =============================================================
void enviar_para_nuvem(Leitura l) {
  Serial.print("[NUVEM] T=");
  Serial.print(l.temperatura, 1);
  Serial.print("C | U=");
  Serial.print(l.umidade, 1);
  Serial.print("% | BPM=");
  Serial.print(l.bpm);
  Serial.print(" | ts=");
  Serial.print(l.timestamp_ms);
  Serial.println("ms");
}

// =============================================================
// FUNÇÃO: Sincronizar fila ao reconectar
// =============================================================
void sincronizar_fila() {
  if (fila_vazia()) return;

  Serial.println(">>> [SYNC] Iniciando sincronizacao de dados offline...");
  Serial.print(">>> [SYNC] Registros pendentes: ");
  Serial.println(fila_tamanho);

  while (!fila_vazia()) {
    Leitura l = fila_pop();
    enviar_para_nuvem(l);
    delay(50);
  }

  Serial.println(">>> [SYNC] Sincronizacao concluida.");
}

// =============================================================
// FUNÇÃO: Ler sensores
// Usa ultimo_bpm_calculado — valor persistido da última janela fechada
// =============================================================
Leitura ler_sensores() {
  Leitura l;
  l.temperatura  = dht.readTemperature();
  l.umidade      = dht.readHumidity();
  l.bpm          = ultimo_bpm_calculado;  // CORRIGIDO: sempre usa o último BPM válido
  l.timestamp_ms = millis();

  if (isnan(l.temperatura)) l.temperatura = -1.0;
  if (isnan(l.umidade))     l.umidade     = -1.0;

  return l;
}

// =============================================================
// FUNÇÃO: Imprimir leitura local no Monitor Serial
// =============================================================
void imprimir_leitura_local(Leitura l, bool offline) {
  Serial.print(offline ? "[OFFLINE] " : "[ONLINE]  ");
  Serial.print("T=");
  Serial.print(l.temperatura, 1);
  Serial.print("C | U=");
  Serial.print(l.umidade, 1);
  Serial.print("% | BPM=");
  Serial.print(l.bpm);
  Serial.print(" | ts=");
  Serial.print(l.timestamp_ms);
  Serial.println("ms");
}

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("==============================================");
  Serial.println("  CardioIA - Fase 3 | Edge Computing");
  Serial.println("  ESP32 + DHT22 + Botao (BPM)");
  Serial.println("==============================================");
  Serial.println("[INFO] Sistema iniciado. Wi-Fi: DESCONECTADO");
  Serial.println("[INFO] Pressione o botao para simular batimentos.");
  Serial.println("[INFO] Wi-Fi alterna automaticamente a cada 30s.");
  Serial.println("[INFO] BPM calculado a cada 10s. Leitura a cada 5s.");

  ultimo_calculo_bpm = millis();
  ultima_leitura     = millis();
  ultimo_toggle_wifi = millis();
}

// =============================================================
// LOOP PRINCIPAL
// =============================================================
void loop() {
  unsigned long agora = millis();

  // --- 1. Leitura do botão com debounce ---
  bool btn_estado = digitalRead(BTN_PIN);

  if (btn_estado == LOW && btn_estado_anterior == HIGH) {
    if ((agora - ultimo_debounce) > DEBOUNCE_MS) {
      contagem_btn++;
      ultimo_debounce = agora;
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  btn_estado_anterior = btn_estado;

  // --- 2. Fechar janela de BPM a cada 10 segundos ---
  // Quando a janela fecha, atualiza ultimo_bpm_calculado (persistente).
  // As leituras de sensor seguintes usarão este valor até a próxima janela.
  if ((agora - ultimo_calculo_bpm) >= JANELA_BPM_MS) {
    ultimo_bpm_calculado = contagem_btn * 6;  // extrapola 10s → 1 minuto
    contagem_btn         = 0;
    ultimo_calculo_bpm   = agora;

    Serial.print("[BPM]  Nova janela fechada. BPM calculado: ");
    Serial.println(ultimo_bpm_calculado);
  }

  // --- 3. Leitura e processamento a cada 5 segundos ---
  // Usa sempre ultimo_bpm_calculado, que persiste entre janelas
  if ((agora - ultima_leitura) >= INTERVALO_LEITURA_MS) {
    ultima_leitura = agora;

    Leitura leitura_atual = ler_sensores();
    imprimir_leitura_local(leitura_atual, !wifi_conectado);

    if (wifi_conectado) {
      enviar_para_nuvem(leitura_atual);
    } else {
      fila_push(leitura_atual);
      Serial.print("[FILA] Registros em espera: ");
      Serial.println(fila_tamanho);
    }
  }

  // --- 4. Toggle simulado de Wi-Fi a cada 30 segundos ---
  if ((agora - ultimo_toggle_wifi) >= INTERVALO_TOGGLE_MS) {
    ultimo_toggle_wifi = agora;
    wifi_conectado = !wifi_conectado;

    if (wifi_conectado) {
      Serial.println("------ [WIFI] Conexao estabelecida ------");
      sincronizar_fila();
    } else {
      Serial.println("------ [WIFI] Conexao perdida. Modo offline ------");
    }
  }
}
