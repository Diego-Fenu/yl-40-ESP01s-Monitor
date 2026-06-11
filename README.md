# yl-40-ESP01s-Monitor
yl-40 PCF8591T ESP-01s 


Il codice legge quattro sensori analogici (fotoresistore, umidità, termistore, potenziometro) tramite il PCF8591T su ESP-01S via I2C. 
Per ogni canale, effettua una lettura a 2 byte: scarta il primo (dato vecchio/crosstalk), usa il secondo (dato corretto). 
Il termistore viene filtrato con una media mobile e convertito in °C tramite la formula di Steinhart-Hart (con Beta calibrato). 
I dati vengono stampati su seriale, inclusi valori intermedi per debug. Il ciclo principale legge e stampa tutti i sensori ogni 2 secondi.



🎯 Scopo del Progetto

Questo sketch implementa un sistema di acquisizione dati multi-sensore utilizzando:
- Chip PCF8591T convertitore analogico-digitale 8-bit via I2C
- Microcontrollore ESP-01S (ESP8266) come master I2C
- Quattro sensori analogici collegati al PCF8591:
  a. AIN0: Fotoresistore (misurazione luminosità)
  b. AIN1: Sensore di umidità del suolo
  c. AIN2: Termistore NTC (misurazione temperatura con conversione Steinhart-Hart)
  d. AIN3: Potenziometro (input di riferimento/manual)

🔧 Caratteristiche Tecniche Implementate

Architettura del Codice

- Programmazione orientata agli oggetti: Classe PCF8591SensorArray incapsulla tutta la logica I2C e il processing dei sensori
- Gestione corretta delle risorse: Implementazione completa del "Rule of 3/5" (costruttore di copia, operatore di assegnamento, distruttore) per evitare memory leak
- Design non bloccante: Macchina a stati basata su millis() che consente altre operazioni durante le attese I2C/conversione ADC
- Filtraggio del segnale: Media mobile circolare sui valori del termistore per ridurre il rumore ADC

Funzionalità Chiave

1. Inizializzazione I2C robusta:
  - Clock a 100kHz con clock stretch limit (200ms) per dispositivi lenti
  - Test di connessione al PCF8591  se non rilevato
2. Lettura sensori affidabile:
  - Selezione canale via I2C con co
  - Attesa non bloccante per conversione ADC (15ms)
  - Lettura di 2 byte e scarto del alk)
3. Elaborazione termistore avanzata:
  - Filtro media mobile configurabi
  - Conversione Steinhart-Hart (modello Beta) per temperatura in °C
  - Validazione a 4 livelli: range ensione limite, range temperatura(-40° to 85°C)
4. Output seriale formattato:
  - Visualizzazione chiara di tutti i sensori con unità
  - Indicazione errori per canali p
  - Dati aggiuntivi per termistore: Vout calcolato e R_NTC

Qualità del Codice

- Commenti dettagliati: Spiegazione di ogni sezione, formule matematiche e scelte di progettazione
- Gestione errori: Controlli approf e validazione dei dati
- Costanti configurabili: Tutti i parametri hardware (indirizzo I2C, pin, valori riferimento)
passabili al costruttore
- Memoria sicura: Allocazione dinamica del buffer filtro con corretta deallocazione nel distruttore
- Portabilità: Uso di tipi standardvitare dipendenze da librerieesterne oltre Wire.h

📊 Analisi Tecnica Approfondita

Scelte di Progettazione Notevoli

1. Stato macchina a stati asincrona (tick() method):
  - Stati: IDLE → SEND → WAIT → REC
  - Consente l'acquisizione non bloccante mantenendo il controllo preciso sui tempi
2. Filtro termistore sofisticato:
  - Inizializzazione intelligente: primo valore valido riempie tutto il buffer
  - Buffer circolare efficiente con
  - Riduzione rumore proporzionale alla radice quadrata della dimensione del filtro
3. Formula Steinhart-Hart ottimizza
  - Utilizza il modello Beta (semplificato) anziché l'equazione completa a 3 coefficienti
  - Include validazioni fisiche peri
  - Calcola anche Vout e R_NTC per debugging e verifica

Possibili Miglioramenti

1. Calibrazione in campo: Aggiungere funzione di taratura tramite comandi seriale
2. Sleep mode: Implementare deep slr risparmio energetico
3. Buffer statistico: Aggiungere min/max/media su finestra temporale più lunga
4. Interfaccia di configurazione: Piale senza ricompilare
5. Watchdog timer: Protezione contro blocchi permanenti dello stato macchina

🔌 Configurazione Hardware Implicita

- ESP-01S: SDA=GPIO0, SCL=GPIO2 (pins 0 e 2)
- PCF8591T: Indirizzo I2C default 0)
- Alimentazione: 3.3V (riferito nella variabile vref)
- Termistore: NTC 10kΩ β=3875K con
- Altri sensori: Intervallo 0-3.3V corrispondente a valori ADC 0-255
