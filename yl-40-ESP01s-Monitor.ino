// Drive per ADC PCF8591T su ESP-01S (ESP8266) via I2C.
// Legge 4 sensori analogici: fotoresistore, umidità, termistore NTC, potenziometro.
// Il termistore viene filtrato e convertito in °C con formula Steinhart-Hart.
// Il ciclo di lettura è non bloccante (basato su millis() e macchina a stati).
#include <Wire.h>

class PCF8591SensorArray {
public:
  // --- Parametri hardware e filtro ---
  uint8_t addr;                // Indirizzo I2C del PCF8591 (default 0x48)
  uint8_t sdaPin, sclPin;      // Pin I2C su ESP-01S: SDA=GPIO0, SCL=GPIO2
  float vref;                  // Tensione di riferimento ADC (3.3V su ESP-01S)
  float rFixed;                // Resistenza fissa del partitore del termistore (19.5kΩ)
  float beta;                  // Coefficiente Beta del termistore NTC (3875K)
  float t0Kelvin;              // Temperatura di riferimento in Kelvin (298.15K = 25°C)
  float r0;                    // Resistenza del termistore a t0Kelvin (10kΩ)
  unsigned long sensorDelay;   // Millisecondi di attesa tra selezione canale e lettura
  unsigned long cycleInterval; // Millisecondi di pausa tra un ciclo completo e il successivo
  int filterSize;              // Numero di campioni della media mobile sul termistore
  int* thermistorBuffer;       // Buffer circolare allocato dinamicamente per il filtro
  int thermistorIndex;         // Posizione corrente nel buffer circolare
  bool filterInitialized;      // Flag: true dopo il primo riempimento del buffer

  // --- Struct contenente i dati letti da tutti i 4 canali ---
  struct SensorData {
    int photoresistor;   // Canale AIN0: fotoresistore (0-255)
    int moistureSensor;  // Canale AIN1: sensore umidità suolo (0-255)
    int thermistor;      // Canale AIN2: termistore dopo filtro media mobile (0-255)
    float temperature;   // Temperatura calcolata in °C dal termistore
    int potentiometer;   // Canale AIN3: potenziometro (0-255)
    bool valid;          // true se tutti i canali sono stati letti senza errori
  };

  // --- Costruttore con parametri di default ---
  // I valori di default sono calibrati per: PCF8591 a 0x48, SDA=0, SCL=2,
  // Vref=3.3V, termistore NTC 10kΩ β=3875, R_fissa=19.5kΩ.
  // Il buffer del filtro è allocato sull'heap con new[].
  PCF8591SensorArray(uint8_t _addr = 0x48, uint8_t _sda = 0, uint8_t _scl = 2,
    float _vref = 3.3, float _rFixed = 19500.0, float _beta = 3875.0,
    float _t0Kelvin = 298.15, float _r0 = 10000.0, unsigned long _sensorDelay = 50,
    int _filterSize = 64, unsigned long _cycleInterval = 2000)
    : addr(_addr), sdaPin(_sda), sclPin(_scl), vref(_vref), rFixed(_rFixed), beta(_beta),
      t0Kelvin(_t0Kelvin), r0(_r0), sensorDelay(_sensorDelay), filterSize(_filterSize),
      thermistorIndex(0), filterInitialized(false), cycleInterval(_cycleInterval)
  {
    thermistorBuffer = new int[filterSize]();
  }

  // --- Copy constructor (Rule of 3/5) ---
  // Alloca un nuovo buffer e copia i dati se il filtro era già inizializzato.
  PCF8591SensorArray(const PCF8591SensorArray& other)
    : addr(other.addr), sdaPin(other.sdaPin), sclPin(other.sclPin),
      vref(other.vref), rFixed(other.rFixed), beta(other.beta),
      t0Kelvin(other.t0Kelvin), r0(other.r0), sensorDelay(other.sensorDelay),
      cycleInterval(other.cycleInterval), filterSize(other.filterSize),
      thermistorIndex(other.thermistorIndex), filterInitialized(other.filterInitialized)
  {
    thermistorBuffer = new int[filterSize]();
    if (filterInitialized) {
      for (int i = 0; i < filterSize; i++) thermistorBuffer[i] = other.thermistorBuffer[i];
    }
  }

  // --- Assignment operator (Rule of 3/5) ---
  // Dealloca il buffer esistente, copia tutti i campi, alloca e copia il buffer.
  PCF8591SensorArray& operator=(const PCF8591SensorArray& other) {
    if (this != &other) {
      delete[] thermistorBuffer;
      addr = other.addr;
      sdaPin = other.sdaPin;
      sclPin = other.sclPin;
      vref = other.vref;
      rFixed = other.rFixed;
      beta = other.beta;
      t0Kelvin = other.t0Kelvin;
      r0 = other.r0;
      sensorDelay = other.sensorDelay;
      cycleInterval = other.cycleInterval;
      filterSize = other.filterSize;
      thermistorIndex = other.thermistorIndex;
      filterInitialized = other.filterInitialized;
      thermistorBuffer = new int[filterSize]();
      if (filterInitialized) {
        for (int i = 0; i < filterSize; i++) thermistorBuffer[i] = other.thermistorBuffer[i];
      }
    }
    return *this;
  }

  // --- Distruttore (Rule of 3/5) ---
  // Dealloca il buffer del filtro per evitare memory leak.
  ~PCF8591SensorArray() {
    delete[] thermistorBuffer;
  }

  // --- Inizializzazione I2C ---
  // Configura pin SDA/SCL, clock a 100kHz e clock stretch limit.
  // Clock stretch evita timeout su I2C con dispositivi lenti.
  void begin() {
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(100000);
    Wire.setClockStretchLimit(200000);
  }

  // --- Test connessione I2C col PCF8591 ---
  // Invia un indirizzo e verifica se il chip risponde (ACK).
  bool testConnection() {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
  }

  // --- Lettura sincrona di un canale (bloccante) ---
  // Seleziona il canale, attende 15ms per la conversione ADC,
  // richiede 2 byte, scarta il primo (dato vecchio/crosstalk),
  // restituisce il secondo (dato corrente).
  // Ritorna -1 in caso di errore.
  int readChannel(uint8_t channel) {
    if (channel >= 4) return -1;
    Wire.beginTransmission(addr);
    Wire.write(0x40 | channel);
    if (Wire.endTransmission() != 0) return -1;
    delay(15);
    uint8_t bytesReceived = Wire.requestFrom(addr, (uint8_t)2);
    if (bytesReceived != 2) return -1;
    Wire.read();
    return Wire.read();
  }

  // --- Filtro media mobile per il termistore ---
  // Buffer circolare: al primo valore valido riempie tutto il buffer,
  // poi sostituisce il campione più vecchio col nuovo e restituisce la media.
  // Attenua il rumore ADC riducendolo di sqrt(filterSize).
  int filterThermistor(int rawValue) {
    if (!filterInitialized) {
      for (int i = 0; i < filterSize; i++) thermistorBuffer[i] = rawValue;
      filterInitialized = true;
      thermistorIndex = 0;
      return rawValue;
    }
    thermistorBuffer[thermistorIndex] = rawValue;
    thermistorIndex = (thermistorIndex + 1) % filterSize;
    int sum = 0;
    for (int i = 0; i < filterSize; i++) sum += thermistorBuffer[i];
    return sum / filterSize;
  }

  // --- Conversione ADC → °C con formula Steinhart-Hart (modello Beta) ---
  // Formula: 1/T = 1/T0 + (1/β) * ln(R_NTC / R0)
  // Dove R_NTC si ricava dal partitore: R_NTC = R_fixed * (Vout / (Vref - Vout))
  // Vout = (ADC * Vref) / 255.0
  // Validazione a 4 livelli: range ADC, saturazione partitore, tensione limite, range temperatura.
  float toCelsius(int adcValue) {
    if (adcValue < 0 || adcValue > 255) return NAN;
    if (adcValue == 0 || adcValue == 255) return NAN;
    float vOut = (adcValue * vref) / 255.0;
    if (vOut <= 0.01 || vOut >= (vref - 0.01)) return NAN;
    float rNtc = rFixed * (vOut / (vref - vOut));
    if (rNtc <= 0 || isnan(rNtc)) return NAN;
    float tempKelvin = 1.0 / (1.0 / t0Kelvin + (1.0 / beta) * log(rNtc / r0));
    float tempCelsius = tempKelvin - 273.15;
    if (tempCelsius < -40.0 || tempCelsius > 85.0) return NAN;
    return tempCelsius;
  }

  // --- Stampa dati su seriale ---
  // Mostra tutti e 4 i canali con valori RAW, temperatura, Vout e R_NTC calcolati.
  // In caso di errore su un canale stampa "ERRORE".
  void printSensorData(const SensorData& data) const {
    Serial.print("[Fotoresistore AIN0] ");
    if (data.photoresistor >= 0) Serial.print(data.photoresistor); else Serial.print("ERRORE");
    Serial.print("   ");
    Serial.print("[Umidità AIN1] ");
    if (data.moistureSensor >= 0) Serial.print(data.moistureSensor); else Serial.print("ERRORE");
    Serial.print("   ");
    Serial.print("[Termistore AIN2] ");
    if (data.thermistor >= 0) {
      Serial.print(data.thermistor);
      if (!isnan(data.temperature)) {
        Serial.print(" (");
        Serial.print(data.temperature, 2);
        Serial.print(" °C)");
        float vOut = (data.thermistor * vref) / 255.0;
        float rNtc = rFixed * (vOut / (vref - vOut));
        Serial.print(" [Vout=");
        Serial.print(vOut, 3);
        Serial.print("V R_NTC=");
        Serial.print(rNtc, 0);
        Serial.print("Ω]");
      } else {
        Serial.print(" (ERRORE conversione)");
      }
    } else {
      Serial.print("ERRORE");
    }
    Serial.print("   ");
    Serial.print("[Potenziometro AIN3] ");
    if (data.potentiometer >= 0) Serial.print(data.potentiometer); else Serial.print("ERRORE");
    Serial.println();
  }

  // --- API non bloccante ---
  // Chiamare tick() da loop(). La macchina a stati interni (IDLE → SEND → WAIT →
  // RECV → COMPLETE → POST_CYCLE) gestisce l'intero ciclo di lettura senza delay().
  // Restituisce true solo quando un ciclo completo di 4 canali è stato acquisito.
  bool tick() {
    unsigned long now = millis();

    switch (asyncStep) {
      // IDLE: prepara una nuova tornata di letture, resetta i flag
      case IDLE:
        asyncData = SensorData();
        asyncValid = true;
        asyncChannel = 0;
        asyncStep = SEND;
        return false;

      // SEND: invia il comando di selezione canale al PCF8591
      case SEND: {
        bool ok = _selectChannel(asyncChannel);
        if (!ok) asyncValid = false;
        asyncStep = WAIT;
        asyncTimer = now;
        return false;
      }

      // WAIT: attesa non bloccante per la conversione ADC
      case WAIT:
        if (now - asyncTimer < sensorDelay) return false;
        asyncStep = RECV;
        return false;

      // RECV: legge il risultato della conversione via I2C
      case RECV: {
        int val = _readResult();
        if (val < 0) asyncValid = false;

        switch (asyncChannel) {
          case 0: asyncData.photoresistor = val; break;
          case 1: asyncData.moistureSensor = val; break;
          case 2: {
            asyncData.thermistor = filterThermistor(val);
            if (asyncData.thermistor >= 0) {
              asyncData.temperature = toCelsius(asyncData.thermistor);
              if (isnan(asyncData.temperature)) asyncValid = false;
            } else {
              asyncData.temperature = NAN;
              asyncValid = false;
            }
            break;
          }
          case 3: asyncData.potentiometer = val; break;
        }

        asyncChannel++;
        // Se tutti e 4 i canali sono stati letti, ciclo completo
        if (asyncChannel >= 4) {
          asyncData.valid = asyncValid;
          asyncStep = COMPLETE;
          asyncTimer = now;
          return true;
        }
        asyncStep = SEND;
        return false;
      }

      // COMPLETE: transizione immediata allo stato di pausa tra cicli
      case COMPLETE:
        asyncStep = POST_CYCLE;
        return false;

      // POST_CYCLE: pausa non bloccante prima del prossimo ciclo
      case POST_CYCLE:
        if (now - asyncTimer < cycleInterval) return false;
        asyncStep = IDLE;
        return false;
    }
    return false;
  }

  // --- Forza l'avvio di un nuovo ciclo di lettura ---
  void beginRead() {
    if (asyncStep == POST_CYCLE) return;
    asyncStep = IDLE;
  }

  // --- Verifica se ci sono dati pronti ---
  bool hasData() const {
    return asyncStep == COMPLETE || asyncStep == POST_CYCLE;
  }

  // --- Recupera l'ultima lettura completa ---
  SensorData getData() const {
    return asyncData;
  }

private:
  // Stati della macchina a stati per la lettura non bloccante
  enum AsyncStep : uint8_t { IDLE, SEND, WAIT, RECV, COMPLETE, POST_CYCLE };

  AsyncStep asyncStep = IDLE;  // Stato corrente
  unsigned long asyncTimer = 0; // Timestamp per le attese non bloccanti
  uint8_t asyncChannel = 0;    // Canale corrente (0-3)
  SensorData asyncData;         // Dati in fase di acquisizione
  bool asyncValid = true;       // Flag di validità parziale

  // --- Seleziona un canale analogico sul PCF8591 ---
  // Invia il control byte via I2C: bit6=1 (DAC enable), bit1-0=canale.
  bool _selectChannel(uint8_t channel) {
    if (channel >= 4) return false;
    Wire.beginTransmission(addr);
    Wire.write(0x40 | channel);
    return Wire.endTransmission() == 0;
  }

  // --- Legge il risultato della conversione ADC via I2C ---
  // Richiede 2 byte: scarta il primo (dato precedente/crosstalk),
  // restituisce il secondo (dato corrente del canale selezionato).
  int _readResult() {
    uint8_t bytesReceived = Wire.requestFrom(addr, (uint8_t)2);
    if (bytesReceived != 2) return -1;
    Wire.read();
    return Wire.read();
  }
};

// Istanza globale del driver con parametri di default
PCF8591SensorArray sensors;

// --- Setup: inizializza seriale, I2C, test connessione PCF8591 ---
void setup() {
  Serial.begin(115200);
  // Attesa seriale con timeout 3s (evita blocco infinito su ESP-01S)
  unsigned long serialTimeout = millis() + 3000;
  while (!Serial && millis() < serialTimeout) {
    delay(10);
  }
  Serial.println("\n=== Avvio Sistema Sensori PCF8591 ===");
  sensors.begin();
  if (!sensors.testConnection()) {
    Serial.println("ERRORE: PCF8591 non rilevato! Controlli i collegamenti.");
    while (1) { delay(1000); }
  }
  Serial.println("PCF8591 inizializzato correttamente");
  Serial.println("Inizio acquisizione dati...\n");
  sensors.beginRead();
}

// --- Loop: chiama tick() a ogni iterazione ---
// Quando tick() restituisce true, un ciclo completo di 4 canali è pronto
// e viene stampato su seriale. La macchina a stati gestisce internamente
// i tempi di attesa senza bloccare la CPU.
void loop() {
  if (sensors.tick()) {
    sensors.printSensorData(sensors.getData());
  }
}
