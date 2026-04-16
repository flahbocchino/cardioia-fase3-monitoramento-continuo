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
// Escolha de projeto: armazenamos até 50 leituras em RAM enquanto offline.
// Justificativa: em cenário de wearable cardíaco com leitura a cada 5s,
// isso cobre ~4 minutos de desconexão antes de descartar dados antigos.
// O Wokwi não suporta SPIFFS persistente, então a RAM é a alternativa
// funcional e pedagogicamente equivalente para fins de simulação.
const int FILA_MAX = 50;

// Estrutura de uma leitura de sensor
struct Leitura {
  float temperatura;
  float umidade;
  int bpm;
  unsigned long timestamp_ms;  // milissegundos desde o boot
};

Leitura fila[FILA_MAX];  // Fila circular em RAM
int fila_inicio = 0;     // Índice do registro mais antigo
int fila_fim    = 0;     // Índice onde a próxima leitura será inserida
int fila_tamanho = 0;    // Quantidade atual de registros na fila

// ----- Simulação de conectividade Wi-Fi -----
// Em produção real, isso seria substituído por WiFi.status() == WL_CONNECTED
bool wifi_conectado = false;

// ----- Contagem de batimentos (botão) -----
int contagem_btn = 0;             // Pressões no intervalo atual
unsigned long ultimo_calculo_bpm = 0;  // Último momento em que BPM foi calculado
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

// Adiciona uma leitura na fila. Se cheia, descarta a mais antiga (FIFO)
void fila_push(Leitura l) {
  fila[fila_fim] = l;
  fila_fim = (fila_fim + 1) % FILA_MAX;

  if (fila_tamanho < FILA_MAX) {
    fila_tamanho++;
  } else {
    // Fila cheia: avança o início, descartando o registro mais antigo
    fila_inicio = (fila_inicio + 1) % FILA_MAX;
    Serial.println("[AVISO] Fila cheia. Registro mais antigo descartado.");
  }
}

// Verifica se há dados na fila
bool fila_vazia() {
  return fila_tamanho == 0;
}

// Remove e retorna a leitura mais antiga da fila
Leitura fila_pop() {
  Leitura l = fila[fila_inicio];
  fila_inicio = (fila_inicio + 1) % FILA_MAX;
  fila_tamanho--;
  return l;
}

// =============================================================
// FUNÇÃO: Enviar uma leitura para a "nuvem" (Serial = simulação)
// Na Parte 2, esta função será substituída pelo envio MQTT real
// =============================================================
void enviar_para_nuvem(Leitura l) {
  Serial.print("[NUVEM] T=");
  Serial.print(l.temperatura, 1);
  Serial.print("°C | U=");
  Serial.print(l.umidade, 1);
  Serial.print("% | BPM=");
  Serial.print(l.bpm);
  Serial.print(" | ts=");
  Serial.print(l.timestamp_ms);
  Serial.println("ms");
}

// =============================================================
// FUNÇÃO: Sincronizar fila com a nuvem ao reconectar
// =============================================================
void sincronizar_fila() {
  if (fila_vazia()) return;

  Serial.println(">>> [SYNC] Iniciando sincronização de dados offline...");
  Serial.print(">>> [SYNC] Registros pendentes: ");
  Serial.println(fila_tamanho);

  while (!fila_vazia()) {
    Leitura l = fila_pop();
    enviar_para_nuvem(l);
    delay(50);  // Pequena pausa para não saturar a Serial
  }

  Serial.println(">>> [SYNC] Sincronização concluída.");
}

// =============================================================
// FUNÇÃO: Ler sensores e retornar uma struct Leitura
// =============================================================
Leitura ler_sensores(int bpm_atual) {
  Leitura l;
  l.temperatura    = dht.readTemperature();
  l.umidade        = dht.readHumidity();
  l.bpm            = bpm_atual;
  l.timestamp_ms   = millis();

  // Validação básica do DHT22 (pode retornar NaN em falha)
  if (isnan(l.temperatura)) l.temperatura = -1.0;
  if (isnan(l.umidade))     l.umidade     = -1.0;

  return l;
}

// =============================================================
// FUNÇÃO: Imprimir leitura no Monitor Serial (sempre, online ou offline)
// =============================================================
void imprimir_leitura_local(Leitura l, bool offline) {
  Serial.print(offline ? "[OFFLINE] " : "[ONLINE]  ");
  Serial.print("T=");
  Serial.print(l.temperatura, 1);
  Serial.print("°C | U=");
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

  pinMode(BTN_PIN, INPUT_PULLUP);  // Botão com resistor pull-up interno
  pinMode(LED_PIN, OUTPUT);

  Serial.println("==============================================");
  Serial.println("  CardioIA - Fase 3 | Edge Computing");
  Serial.println("  ESP32 + DHT22 + Botão (BPM)");
  Serial.println("==============================================");
  Serial.println("[INFO] Sistema iniciado. Wi-Fi: DESCONECTADO");
  Serial.println("[INFO] Pressione o botão para simular batimentos.");
  Serial.println("[INFO] O Wi-Fi alterna automaticamente a cada 30s.");

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
    // Borda de descida: botão pressionado
    if ((agora - ultimo_debounce) > DEBOUNCE_MS) {
      contagem_btn++;
      ultimo_debounce = agora;
      // Pulso no LED para feedback visual de cada batimento
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  btn_estado_anterior = btn_estado;

  // --- 2. Calcular BPM ao fim da janela de 10 segundos ---
  int bpm_calculado = 0;
  if ((agora - ultimo_calculo_bpm) >= JANELA_BPM_MS) {
    // BPM = pressões em 10s × 6 (extrapola para 1 minuto)
    bpm_calculado    = contagem_btn * 6;
    contagem_btn     = 0;
    ultimo_calculo_bpm = agora;
  }

  // --- 3. Leitura e processamento a cada INTERVALO_LEITURA_MS ---
  if ((agora - ultima_leitura) >= INTERVALO_LEITURA_MS) {
    ultima_leitura = agora;

    // Usa o BPM da última janela completa calculada
    // (valor 0 indica que a janela ainda não fechou neste ciclo)
    Leitura leitura_atual = ler_sensores(bpm_calculado);

    // Imprime sempre no Serial (substitui SPIFFS em simulador)
    imprimir_leitura_local(leitura_atual, !wifi_conectado);

    if (wifi_conectado) {
      // Conectado: envia direto para a nuvem
      enviar_para_nuvem(leitura_atual);
    } else {
      // Desconectado: armazena na fila circular (Edge Computing)
      fila_push(leitura_atual);
      Serial.print("[FILA] Registros em espera: ");
      Serial.println(fila_tamanho);
    }
  }

  // --- 4. Toggle simulado de conectividade Wi-Fi a cada 30 segundos ---
  if ((agora - ultimo_toggle_wifi) >= INTERVALO_TOGGLE_MS) {
    ultimo_toggle_wifi = agora;
    wifi_conectado = !wifi_conectado;

    if (wifi_conectado) {
      Serial.println("------ [WIFI] Conexão estabelecida ------");
      // Ao reconectar, sincroniza tudo que ficou na fila
      sincronizar_fila();
    } else {
      Serial.println("------ [WIFI] Conexão perdida. Modo offline ------");
    }
  }
}
