![](Images/SensorOS4OS.gif)

# Documentation of the os4os ESP32 board

Overview
--------
This repository contains the firmware, payload decoder and minimal documentation for the ParKli ESP32 water-quality sensor platform. The device measures battery level, conductivity (TDS), pH, up to five DS18B20 temperatures, an optional analog sensor, and a BME280 (air temperature, pressure, humidity). Data is sent via LoRa (TTN/OTAA).

Repository layout
-----------------
- [src_code/main.ino](src_code/main.ino) — Main ESP32 Arduino sketch with LMIC LoRaWAN integration; key functions: [`do_send`](src_code/main.ino), [`refreshSensorData`](src_code/main.ino), [`setSleepTime`](src_code/main.ino), [`getCellLvlPercent`](src_code/main.ino), [`getBootCycle`](src_code/main.ino), [`onEvent`](src_code/main.ino), [`checkTxIntervalWatchdog`](src_code/main.ino), [`enforceBackoffLimit`](src_code/main.ino).
- [TTN_Decoder/decoder.txt](TTN_Decoder/decoder.txt) — TTN payload formatter / decoder used on The Things Network; exposes [`decodeUplink`](TTN_Decoder/decoder.txt), [`decodeSigned16Bit`](TTN_Decoder/decoder.txt), [`moveComma`](TTN_Decoder/decoder.txt).
- [Documentation/test.txt](Documentation/test.txt) — placeholder/notes.
- [Images/](Images/) — project images and GIFs used in this README.

Firmware summary
----------------
- Power management: measures battery via ADC, computes percent in [`getCellLvlPercent`](src_code/main.ino), and gates deep-sleep duration via [`setSleepTime`](src_code/main.ino).
- Sensors:
  - DS18B20 via OneWire / DallasTemperature (up to 5 sensors).
  - BME280 via I2C (address probing 0x76 / 0x77).
  - Analog sensors: TDS (conductivity), pH, and an optional analog input.
- Transmission:
  - OTAA join with LMIC; join and TX event handling in [`onEvent`](src_code/main.ino).
  - Data packed into a 30‑byte payload inside `buffer` and sent from [`do_send`](src_code/main.ino).
  - Watchdog/backoff safety: [`checkTxIntervalWatchdog`](src_code/main.ino) and [`enforceBackoffLimit`](src_code/main.ino).

TTN Payload decoder
-------------------
Use the TTN JavaScript decoder in [TTN_Decoder/decoder.txt](TTN_Decoder/decoder.txt). It maps raw bytes to:
- BatterieRAW, BatterieProzent
- Leitwert (TDS), PH, Optional_Sensor
- Temperatur_1..5, Air_Temperature (scaled by 1/100)
- Pressure, Humidity
- Aufruf (boot counter)

Build / flash
-------------
1. Open [src_code/main.ino](src_code/main.ino) in the Arduino IDE or PlatformIO (ESP32 board).
2. Install required libraries: LMIC, DallasTemperature, OneWire, Adafruit_BME280, Adafruit_Sensor (and ESP32 core).
3. Update OTAA credentials in the sketch (APPEUI / DEVEUI / APPKEY).
4. Compile and flash to the ESP32.

Notes & troubleshooting
-----------------------
- If battery percent is low, the device enforces long deep-sleep.
- Large LoRa backoff forces a longer sleep via [`enforceBackoffLimit`](src_code/main.ino).
- Serial prints are available at 115200 baud for debugging.

License and credits
-------------------
Add your license and credits here.

Contact
-------
Use repository issues for questions or debugging.