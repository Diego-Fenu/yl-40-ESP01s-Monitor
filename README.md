# yl-40-ESP01s-Monitor
yl-40 PCF8591T ESP-01s 


Il codice legge quattro sensori analogici (fotoresistore, umidità, termistore, potenziometro) tramite il PCF8591T su ESP-01S via I2C. 
Per ogni canale, effettua una lettura a 2 byte: scarta il primo (dato vecchio/crosstalk), usa il secondo (dato corretto). 
Il termistore viene filtrato con una media mobile e convertito in °C tramite la formula di Steinhart-Hart (con Beta calibrato). 
I dati vengono stampati su seriale, inclusi valori intermedi per debug. Il ciclo principale legge e stampa tutti i sensori ogni 2 secondi.
