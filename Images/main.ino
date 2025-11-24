#include <Arduino.h>
#include <EEPROM.h>  //https://github.com/espressif/arduino-esp32/tree/master/libraries/EEPROM
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <OneWire.h>
#include <Wire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <esp_task_wdt.h>
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

#define loraNSS 5
#define loraRST 26
#define loraDOI0 4
#define loraDOI1 17

#define SCL_PIN 22
#define SDA_PIN 21

#define ledPin 13
#define userButtonPin 35
#define vBatPin 34

#define humTempOptEnPin 32
#define tempPin 14
#define optSensorPin 33

#define phEnPin 25
#define phPin 36

#define loraTdsEnPin 27
#define tdsPin 39


OneWire oneWire(tempPin);
DallasTemperature sensors(&oneWire);

//Define interval for measurements, set by function setSleepTime() or absolut

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
uint64_t TIME_TO_SLEEP = 10ULL;   /* Time ESP32 will go to sleep (in seconds) Will be overwritten by setSleepTime() funktion*/
int16_t CellLvlPercent;
byte buffer[30];
#define EEPROM_SIZE 24
uint32_t bootCount;
int16_t batterylvl;
//if true sensor values are send every 6 sec
bool debug = false;
#define WDT_TIMEOUT 120 //Watchdog timeout in seconds
esp_err_t ESP32_ERROR;
// Soft watchdog for "TX cycle took too long"
static uint32_t cycleStartMs = 0;
//
// For normal use, we require that you edit the sketch to replace FILLMEIN
// with values assigned by the TTN console. However, for regression tests,
// we want to be able to compile these scripts. The regression tests define
// COMPILE_REGRESSION_TEST, and in that case we define FILLMEIN to a non-
// working but innocuous value.
//
#ifdef COMPILE_REGRESSION_TEST
#define FILLMEIN 0
#else
#warning "You must replace the values marked FILLMEIN with real values from the TTN control panel!"
#define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif

void do_send(osjob_t* j);

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8] = { FILLMEIN };

void os_getArtEui(u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

// This should also be in little endian format lsb, see above.
static const u1_t PROGMEM DEVEUI[8] = { FILLMEIN }; //(lsb)

void os_getDevEui(u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is. 
static const u1_t PROGMEM APPKEY[16] = { FILLMEIN }; //(msb)

void os_getDevKey(u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

static uint8_t mydata[] = "Hello, world!";
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
unsigned TX_INTERVAL = 1;

const lmic_pinmap lmic_pins = {
  .nss = 5,  // chip select on (rf95module) CS
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 26,                          // reset pin
  .dio = { 4, 17, LMIC_UNUSED_PIN },  //G0, G1
};

void printHex2(unsigned v) {
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      {
        u4_t netid = 0;
        devaddr_t devaddr = 0;
        u1_t nwkKey[16];
        u1_t artKey[16];
        LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
        Serial.print("netid: ");
        Serial.println(netid, DEC);
        Serial.print("devaddr: ");
        Serial.println(devaddr, HEX);
        Serial.print("AppSKey: ");
        for (size_t i = 0; i < sizeof(artKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(artKey[i]);
        }
        Serial.println("");
        Serial.print("NwkSKey: ");
        for (size_t i = 0; i < sizeof(nwkKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(nwkKey[i]);
        }
        Serial.println();
      }
      // Disable link check validation (automatically enabled
      // during join, but because slow data rates change max TX
      // size, we don't use it in this example.
      LMIC_setLinkCheckMode(0);
      break;
    /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_RFU1:
        ||     Serial.println(F("EV_RFU1"));
        ||     break;
        */
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("Received ack"));
      if (LMIC.dataLen) {
        Serial.print(F("Received "));
        Serial.print(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
      }
      // Schedule next transmission
      setSleepTime();
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      Serial.println("ESP32 wake-up in " + String(TIME_TO_SLEEP) + " seconds");
      
      //Only enter Deep Sleep mode if esp goes to sleep more than 60sec 
      if(TX_INTERVAL == 1){
      // Go in Deep Sleep mode
      Serial.println("Goes into Deep Sleep mode");
      Serial.println("----------------------");
      delay(100);
      
      esp_deep_sleep_start();
      Serial.println("This will never be displayed");
      }
      //if sleep time is below 60 sec schedule TX_Intervall without resetting LORA session to minimize data usage.
      //esp_task_wdt_reset();  // Reset the watchdog timer to prevent it from triggering (wdt must be > TX_INTERVAL) 
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_SCAN_FOUND:
        ||    Serial.println(F("EV_SCAN_FOUND"));
        ||    break;
        */
    case EV_TXSTART:
      Serial.println(F("EV_TXSTART"));
      break;
    case EV_TXCANCELED:
      Serial.println(F("EV_TXCANCELED"));
      break;
    case EV_RXSTART:
      /* do not print anything -- it wrecks timing */
      break;
    case EV_JOIN_TXCOMPLETE:
      Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
      //if data can not be send the esp32 goes back to sleep
      setSleepTime();
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      Serial.println("ESP32 wake-up in " + String(TIME_TO_SLEEP) + " seconds");

      // Go in Deep Sleep mode
      Serial.println("Goes into Deep Sleep mode");
      Serial.println("----------------------");
      delay(100);
      esp_deep_sleep_start();
      Serial.println("This will never be displayed");

      break;

    default:
      Serial.print(F("Unknown event: "));
      Serial.println((unsigned)ev);
      break;
  }
}

void do_send(osjob_t* j) {
  // Payload to send (uplink)
   // Start/renew the TX cycle timer
  cycleStartMs = millis();
  refreshSensorData();
  Serial.println(F("refresh sensor data completed..."));
  // LMIC_setTxData2(1, buffer, sizeof(buffer), 0);
  digitalWrite(loraTdsEnPin, HIGH);
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);

    // Show when we are actually allowed to transmit again
    ostime_t now  = os_getTime();
    ostime_t next = LMIC.txend;
    
    // compute signed delta in ticks (wrap-safe with signed math)
    ostime_t dt_ticks = next - now;
    if (dt_ticks < 0) dt_ticks = 0;
    
    uint32_t msUntil = osticks2ms(dt_ticks);
    Serial.print("Next possible TX in [ms]: ");
    Serial.println(msUntil);
    // NEW: reboot right away if the backoff exceeds your limit
    enforceBackoffLimit(msUntil, "do_send()");

     // --------- WDT TX_INTERAVL Check start ----------
    checkTxIntervalWatchdog("after LMIC_setTxData2");

    Serial.println(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

void refreshSensorData() {

  getBootCycle();
  getCellLvlPercent();

  digitalWrite(loraTdsEnPin, HIGH);
  delay(1000);
  int16_t tds = (int16_t)analogRead(tdsPin);
  digitalWrite(loraTdsEnPin, LOW);

  digitalWrite(phEnPin, HIGH);
  delay(1000);
  int16_t ph = (int16_t)analogRead(phPin);
  digitalWrite(phEnPin, LOW);

  digitalWrite(humTempOptEnPin, HIGH);
  delay(1000);
 
  sensors.requestTemperatures();

  int16_t temp1 = int16_t(sensors.getTempCByIndex(0) * 100);
  int16_t temp2 = int16_t(sensors.getTempCByIndex(1) * 100);
  int16_t temp3 = int16_t(sensors.getTempCByIndex(2) * 100);
  int16_t temp4 = int16_t(sensors.getTempCByIndex(3) * 100);
  int16_t temp5 = int16_t(sensors.getTempCByIndex(4) * 100);

  //digitalWrite(humTempOptEnPin, LOW);
  digitalWrite(humTempOptEnPin, HIGH);
  delay(3000);

  if (!bme.begin(0x76)) {
    if (!bme.begin(0x77)) {
      Serial.println("BME280 nicht gefunden!");
    }
  }
  Serial.print("AirTemperature = ");
  int16_t airTemperature = int16_t(bme.readTemperature() * 100);
  Serial.print(airTemperature / 100.0F);
  Serial.println("*C");

  Serial.print("Pressure = ");
  int16_t pressure = int16_t(bme.readPressure());
  Serial.print(pressure / 100.0F);
  Serial.println("hPa");

  Serial.print("Humidity = ");
  int16_t humidity = int16_t(bme.readHumidity() * 100);
  Serial.print(humidity / 100.0F);
  Serial.println("%");

  Serial.println();
  //digitalWrite(humTempOptEnPin, LOW);

  digitalWrite(humTempOptEnPin, HIGH);
  delay(100);
  int16_t optionalSensor = analogRead(optSensorPin);
  digitalWrite(humTempOptEnPin, LOW);


  Serial.print(F("|| Batterie: "));
  Serial.print(batterylvl);
  Serial.print(F("|| TDS: "));
  Serial.print(tds);
  Serial.print(F("|| PH: "));
  Serial.print(ph);
  Serial.print(F("|| optionalSensor: "));
  Serial.print(optionalSensor);
  Serial.print(F("|| Temperatur1: "));
  Serial.print(temp1);
  Serial.print(F("|| Temperatur2: "));
  Serial.print(temp2);
  Serial.print(F("|| Temperatur3: "));
  Serial.print(temp3);
  Serial.print(F("|| Temperatur4: "));
  Serial.print(temp4);
  Serial.print(F("|| Temperatur5: "));
  Serial.print(temp5);
  Serial.print(F("|| Air Temperature: "));
  Serial.print(airTemperature);
  Serial.print(F("|| Pressure: "));
  Serial.print(pressure);
  Serial.print(F("|| Humidity: "));
  Serial.print(humidity);
  Serial.print(F("|| bootCount: "));
  Serial.println(bootCount);
  Serial.print(F("|| CellLvlPercent: "));
  Serial.println(CellLvlPercent);

  buffer[0] = batterylvl >> 8;
  buffer[1] = batterylvl;
  buffer[2] = tds >> 8;
  buffer[3] = tds;
  buffer[4] = ph >> 8;
  buffer[5] = ph;
  buffer[6] = optionalSensor >> 8;
  buffer[7] = optionalSensor;
  buffer[8] = temp1 >> 8;
  buffer[9] = temp1;
  buffer[10] = temp2 >> 8;
  buffer[11] = temp2;
  buffer[12] = temp3 >> 8;
  buffer[13] = temp3;
  buffer[14] = temp4 >> 8;
  buffer[15] = temp4;
  buffer[16] = temp5 >> 8;
  buffer[17] = temp5;
  buffer[18] = airTemperature >> 8;
  buffer[19] = airTemperature;
  buffer[20] = pressure >> 8;
  buffer[21] = pressure;
  buffer[22] = humidity >> 8;
  buffer[23] = humidity;
  buffer[24] = bootCount >> 24;
  buffer[25] = bootCount >> 16;
  buffer[26] = bootCount >> 8;
  buffer[27] = bootCount;
  buffer[28] = CellLvlPercent >> 8;
  buffer[29] = CellLvlPercent;
}

void setup() {
  // Serielle Verbindung initialisieren
  Serial.begin(115200);
  Serial.println(F("Starting"));
/*
  Serial.println("Configuring WDT...");
  Serial.print("Watchdog Timeout (in seconds) set to : ");
  Serial.println(WDT_TIMEOUT);
  esp_task_wdt_deinit();
   Task Watchdog configuration
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,                 // Convertin ms
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // Bitmask of all cores, https://github.com/espressif/esp-idf/blob/v5.2.2/examples/system/task_watchdog/main/task_watchdog_example_main.c
    .trigger_panic = true                             // Enable panic to restart ESP32
  };
  // WDT Init
  ESP32_ERROR = esp_task_wdt_init(&wdt_config);
  Serial.println("Last Reset : " + String(esp_err_to_name(ESP32_ERROR)));
  esp_task_wdt_add(NULL);  //add current thread to WDT watch
*/
  // Pin-Modi festlegen und initiale Zustände setzen
  pinMode(loraTdsEnPin, OUTPUT);
  digitalWrite(loraTdsEnPin, LOW);

  pinMode(phEnPin, OUTPUT);
  digitalWrite(phEnPin, LOW);

  pinMode(phPin, INPUT);
  pinMode(tdsPin, INPUT);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(userButtonPin, INPUT);
  pinMode(vBatPin, INPUT);

  pinMode(humTempOptEnPin, OUTPUT);
  digitalWrite(humTempOptEnPin, LOW);

  pinMode(tempPin, INPUT);
  pinMode(optSensorPin, INPUT);

  delay(100);
  batterylvl = analogRead(vBatPin);
  delay(100);
  Serial.println("batterylvl: ");
  Serial.println(batterylvl);


  if (!debug) {
    //Verhindere einen deadlock des ESP32 durch zu geringen Akkustand
    // Batterielevel auslesen
   
    getCellLvlPercent();
    if (CellLvlPercent < 15) {
      TIME_TO_SLEEP = 24 * 3600ULL;  //(24H)
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      Serial.println("ESP32 wake-up in " + String(TIME_TO_SLEEP) + " seconds");

      // Go in Deep Sleep mode
      Serial.println("Goes into Deep Sleep mode");
      Serial.println("----------------------");
      delay(100);
      esp_deep_sleep_start();
      Serial.println("This will never be displayed");
    }
  }

  digitalWrite(humTempOptEnPin, HIGH);
  delay(1000);
  sensors.begin();
  digitalWrite(humTempOptEnPin, LOW);



  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop() {
  os_runloop_once();
}

void setSleepTime() {
  getCellLvlPercent();
  // Standard Sleep Time (24 Stunden)

  TIME_TO_SLEEP = 24 * 3600ULL;

  if (CellLvlPercent > 90) {
    TIME_TO_SLEEP = 1ULL;                   // Disabled ESP32 deep sleep 
    TX_INTERVAL = 60ULL;                    // Use os_setTimedCallback
  } else if (CellLvlPercent > 85) {         
    TIME_TO_SLEEP = 1200ULL;  // 20 Minute   // Use ESP32 deep sleep 
    TX_INTERVAL = 1ULL;                     // Disabled os_setTimedCallback
  } else if (CellLvlPercent > 40) {
    TIME_TO_SLEEP = 3600ULL;  // 1 Stunde
    TX_INTERVAL = 1ULL;
  } else if (CellLvlPercent > 20) {
    TIME_TO_SLEEP = 24 * 3600ULL;  // 2 Stunden
    TX_INTERVAL = 1ULL;
  }
  // Ausgabe der Sleep-Time
  Serial.println("ESP32 wake-up in " + String(TIME_TO_SLEEP) + " seconds");
  if (debug) {
    TIME_TO_SLEEP = 6ULL;
    Serial.print("Debug=true; overwrite TIME_TO_SLEEP: ");
  }  
}

void getCellLvlPercent() {
  const int maxOperatingVoltage = 2400;
  const int minOperatingVoltage = 2000;  //Vergleichsweise hoch da DeepSleep Current bei werten unter 2000 deutlich über normal 200uA
  const float operationalRange = maxOperatingVoltage - minOperatingVoltage;

  if (batterylvl <= minOperatingVoltage) {
    CellLvlPercent = 0;
  } else if (batterylvl > maxOperatingVoltage) {
    CellLvlPercent = 100;
  } else {
    CellLvlPercent = ((batterylvl - minOperatingVoltage) / operationalRange) * 100;
  }

  Serial.print("CellLvlPercent: ");
  Serial.println(CellLvlPercent);
}

void getBootCycle() {
  // Initialisierung des EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Adressen und Code-Initialisierung
  const int addressBootCycle = 0;
  const int addressInitCode = 10;
  const uint32_t expectedCode = 123456789;

  // Initialisierungscode lesen
  uint32_t storedInitCode;
  EEPROM.get(addressInitCode, storedInitCode);

  Serial.print("Gelesener Init-Code: ");
  Serial.println(storedInitCode);

  // Überprüfen, ob der gespeicherte Init-Code übereinstimmt
  if (storedInitCode != expectedCode) {
    Serial.println("Init-Code nicht gefunden, BootCycle wird auf 1 gesetzt.");

    // Init-Code und Boot-Zähler zurücksetzen
    EEPROM.writeUInt(addressInitCode, expectedCode);
    EEPROM.writeUInt(addressBootCycle, 1);
    EEPROM.commit();  // Commit, da Änderungen vorgenommen wurden

    bootCount = 1;
  } else {
    // Boot-Zyklus erhöhen
    Serial.println("BootCycle wird erhöht.");

    uint32_t currentBootCycle;
    EEPROM.get(addressBootCycle, currentBootCycle);

    Serial.print("Aktueller Boot-Zyklus: ");
    Serial.println(currentBootCycle);

    uint32_t newBootCycle = currentBootCycle + 1;
    EEPROM.writeUInt(addressBootCycle, newBootCycle);
    EEPROM.commit();  // Commit, da Änderungen vorgenommen wurden

    bootCount = newBootCycle;
  }

  // EEPROM-Prozess beenden
  EEPROM.end();
}

static inline void checkTxIntervalWatchdog(const char* where) {
  const uint32_t now = millis();
  // Compare with subtraction to survive millis() wrap
  if (cycleStartMs && (now - cycleStartMs) > ((TIME_TO_SLEEP * TX_INTERVAL) * uS_TO_S_FACTOR)) {
    Serial.print("SW-WDT "); Serial.print(where);
    Serial.print(" elapsed(ms)="); Serial.print(now - cycleStartMs);
    Serial.print(" > TX_INTERVAL(ms)="); Serial.println((TIME_TO_SLEEP * TX_INTERVAL) * uS_TO_S_FACTOR);    
    Serial.println("Rebooting: TX cycle exceeded TIME_TO_SLEEP * TX_INTERVAL");
    delay(100);
    esp_restart();
  }
}

static inline void enforceBackoffLimit(uint32_t msUntil, const char* where) {
  const uint32_t limitMs = 30 * uS_TO_S_FACTOR;
  if (msUntil > limitMs) {
    Serial.print("Backoff too large at "); Serial.print(where);
    Serial.print(": msUntil="); Serial.print(msUntil);
    Serial.print(" > limit="); Serial.println(limitMs);
    Serial.print("start deep sleep and try again afterwards"); Serial.println(limitMs);
    delay(100);
    TIME_TO_SLEEP = 2 * 3600ULL;  // 2 Stunden
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    Serial.println("ESP32 wake-up in " + String(TIME_TO_SLEEP) + " seconds");
    // Go in Deep Sleep mode
    Serial.println("Goes into Deep Sleep mode");
    Serial.println("----------------------");
    delay(100);
    esp_deep_sleep_start();
    Serial.println("This will never be displayed");
  }
}







/*######################################
//TTN Payload formatter
//######################################

function decodeUplink(input) {
  
  var bytes = input.bytes;
  var decoded = {
    BatterieRAW: (bytes[0] << 8) + bytes[1],
    BatterieProzent: ((bytes[28] << 8) + bytes[29]),
    Leitwert: decodeSigned16Bit(bytes[2], bytes[3]),
    PH: decodeSigned16Bit(bytes[4], bytes[5]),
    Optional_Sensor: decodeSigned16Bit(bytes[6], bytes[7]),
    Temperatur_1: moveComma(decodeSigned16Bit(bytes[8], bytes[9])),  // 16-bit signed
    Temperatur_2: moveComma(decodeSigned16Bit(bytes[10], bytes[11])), // 16-bit signed
    Temperatur_3: moveComma(decodeSigned16Bit(bytes[12], bytes[13])), // 16-bit signed
    Temperatur_4: moveComma(decodeSigned16Bit(bytes[14], bytes[15])), // 16-bit signed
    Temperatur_5: moveComma(decodeSigned16Bit(bytes[16], bytes[17])), // 16-bit signed
    Air_Temperature: moveComma(decodeSigned16Bit(bytes[18], bytes[19])), // 16-bit signed
    Pressure: moveComma(decodeSigned16Bit(bytes[20], bytes[21])), // 16-bit signed
    Humidity: moveComma(decodeSigned16Bit(bytes[22], bytes[23])), // 16-bit signed
    Aufruf: (bytes[27] | (bytes[26] << 8) | (bytes[25] << 16) | (bytes[24] << 24)) >>> 0 // 32-bit unsigned
  };
  
  return {
    data: decoded,
    warnings: [],
    errors: []
  };
}

// Funktion zum Decodieren von 16-Bit vorzeichenbehafteten Werten (z.B. für Temperaturen)
function decodeSigned16Bit(byte1, byte2) {
  var value = (byte1 << 8) | byte2;
  if (value >= 0x8000) {
    value -= 0x10000;
  }
  return value;
}

function moveComma(input) {
  return input / 100; // Angenommen, Temperatur wird in Hundertstel-Graden gesendet
}

function ph(input) {
  return input;
}



//############################
//If the temperature is -127, no temperature sensor is connected to the corresponding port




###########################################################################


*/