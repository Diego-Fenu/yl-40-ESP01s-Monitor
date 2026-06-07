#include <Wire.h>

/*
Il codice legge quattro sensori analogici (fotoresistore, umidità, termistore, potenziometro) tramite il PCF8591T su ESP-01S via I2C. 
Per ogni canale, effettua una lettura a 2 byte: scarta il primo (dato vecchio/crosstalk), usa il secondo (dato corretto). 
Il termistore viene filtrato con una media mobile e convertito in °C tramite la formula di Steinhart-Hart (con Beta calibrato). 
I dati vengono stampati su seriale, inclusi valori intermedi per debug. Il ciclo principale legge e stampa tutti i sensori ogni 2 secondi.
*/

// --- Classe riutilizzabile per PCF8591 e sensori ---
class PCF8591SensorArray {
public:
  // Parametri configurabili
  uint8_t addr;
  uint8_t sdaPin, sclPin;
  float vref, rFixed, beta, t0Kelvin, r0;
  unsigned long sensorDelay;
  int filterSize;
  int* thermistorBuffer;
  int thermistorIndex;
  bool filterInitialized;

  struct SensorData {
    int photoresistor;
    int moistureSensor;
    int thermistor;
    float temperature;
    int potentiometer;
    bool valid;
  };

  SensorData readAll();

  PCF8591SensorArray(uint8_t _addr = 0x48, uint8_t _sda = 0, uint8_t _scl = 2,
    float _vref = 3.3, float _rFixed = 19500.0, float _beta = 3875.0,
    float _t0Kelvin = 298.15, float _r0 = 10000.0, unsigned long _sensorDelay = 50, int _filterSize = 8)
    : addr(_addr), sdaPin(_sda), sclPin(_scl), vref(_vref), rFixed(_rFixed), beta(_beta),
      t0Kelvin(_t0Kelvin), r0(_r0), sensorDelay(_sensorDelay), filterSize(_filterSize), thermistorIndex(0), filterInitialized(false)
  {
    thermistorBuffer = new int[filterSize]();
  }

  void begin() {
    Wire.begin(sdaPin, sclPin);
    Wire.setClock(100000);
  }

  bool testConnection() {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
  }

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

  int filterThermistor(int rawValue) {
    if (!filterInitialized && rawValue > 0) {
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

  ~PCF8591SensorArray() {
    delete[] thermistorBuffer;
  }
};

// --- Fine classe riutilizzabile ---

// Parametri principali
#define LOOP_DELAY 2000

PCF8591SensorArray sensors;

// (struct SensorData ora interna alla classe)

// Estensione della classe per includere la lettura di tutti i sensori
PCF8591SensorArray::SensorData PCF8591SensorArray::readAll() {
  SensorData data;
  bool allValid = true;
  data.photoresistor = readChannel(0);
  delay(sensorDelay);
  data.moistureSensor = readChannel(1);
  delay(sensorDelay);
  int rawThermistor = readChannel(2);
  data.thermistor = filterThermistor(rawThermistor);
  delay(sensorDelay);
  data.potentiometer = readChannel(3);
  if (data.photoresistor < 0) allValid = false;
  if (data.moistureSensor < 0) allValid = false;
  if (rawThermistor < 0) allValid = false;
  if (data.potentiometer < 0) allValid = false;
  if (data.thermistor >= 0) {
    data.temperature = toCelsius(data.thermistor);
    if (isnan(data.temperature)) allValid = false;
  } else {
    data.temperature = NAN;
  }
  data.valid = allValid;
  return data;
}


void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.println("\n=== Avvio Sistema Sensori PCF8591 (modulare) ===");
  sensors.begin();
  if (!sensors.testConnection()) {
    Serial.println("ERRORE: PCF8591 non rilevato! Controlli i collegamenti.");
    while (1) { delay(1000); }
  }
  Serial.println("PCF8591 inizializzato correttamente");
  Serial.println("Inizio acquisizione dati...\n");
}


// Funzione legacy, ora sostituita dal metodo sensors.readChannel()
int readPCF8591(uint8_t channel) {
  return sensors.readChannel(channel);
}

// Funzione legacy, ora sostituita dal metodo sensors.filterThermistor()
int applyThermistorFilter(int rawValue) {
  return sensors.filterThermistor(rawValue);
}

// Funzione legacy, ora sostituita dal metodo sensors.toCelsius()
float convertToCelsius(int adcValue) {
  return sensors.toCelsius(adcValue);
}


void printSensorData(const PCF8591SensorArray::SensorData& data, const PCF8591SensorArray& sensors) {
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
      float vOut = (data.thermistor * sensors.vref) / 255.0;
      float rNtc = sensors.rFixed * (vOut / (sensors.vref - vOut));
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

void loop() {
  auto sensorData = sensors.readAll();
  printSensorData(sensorData, sensors);
  delay(LOOP_DELAY);
}