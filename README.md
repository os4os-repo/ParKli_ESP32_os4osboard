![](Images/SensorOS4OS.gif)

# Documentation of the os4os ESP32 board

Overview
--------
This repository contains the firmware, payload decoder and minimal documentation for the ParKli os4os ESP32 board, the sensor is used for water-quality water quality measurements. The device measures battery level, conductivity (TDS), pH, up to five DS18B20 temperatures, an optional analog sensor, and a BME280 (air temperature, pressure, humidity). Data is sent via LoRa (TTN/OTAA).

Further information
-----------------
https://datahub.openscience.eu/dataset/parkli-boje <br>
https://parkli.de/

Repository layout
-----------------
- [src_code/main.ino](src_code/main.ino) — Main ESP32 Arduino sketch with LMIC LoRaWAN integration; key functions: [`do_send`](src_code/main.ino), [`refreshSensorData`](src_code/main.ino), [`setSleepTime`](src_code/main.ino), [`getCellLvlPercent`](src_code/main.ino), [`getBootCycle`](src_code/main.ino), [`onEvent`](src_code/main.ino), [`checkTxIntervalWatchdog`](src_code/main.ino), [`enforceBackoffLimit`](src_code/main.ino).
- [src_code/archive/](src_code/archive/) — Superseded versions of the sketch, kept for reference (see "Archived versions" below).
- [TTN_Decoder/decoder.txt](TTN_Decoder/decoder.txt) — TTN payload formatter / decoder used on The Things Network; exposes [`decodeUplink`](TTN_Decoder/decoder.txt), [`decodeSigned16Bit`](TTN_Decoder/decoder.txt), [`moveComma`](TTN_Decoder/decoder.txt).
- [Documentation/test.txt](Documentation/test.txt) — placeholder/notes.
- [Images/](Images/) — Images of the PCB board and settings in Arduino IDE for code upload.

Archived versions
-----------------
`src_code/` only ever contains one buildable sketch: `main.ino`, always the current version. Superseded versions are not left in `src_code/` under ambiguous names (e.g. `main_old.ino`) because that makes it unclear which file is meant to be compiled/flashed, and the "old" one silently goes stale as `main.ino` moves on. Instead, retired versions are moved into [src_code/archive/](src_code/archive/) and renamed to `main_<YYYY-MM-DD>_<short-description>.ino`, where the date and description identify what the sketch looked like and why it was superseded. Archived files are kept for reference only and are not meant to be compiled as-is.

- [src_code/archive/main_2026-01-15_otaa-join.ino](src_code/archive/main_2026-01-15_otaa-join.ino) — version prior to LMIC session persistence: performs a fresh OTAA join on every wake from deep sleep and still uses `FILLMEIN` placeholders for OTAA credentials. See "Changes vs. archived versions" below.

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
2. Install required libraries: 
   - MCCI LoRaWAN LMIC library https://github.com/mcci-catena/arduino-lmic, 
   - DallasTemperature https://github.com/milesburton/Arduino-Temperature-Control-Library, 
   - OneWire https://www.pjrc.com/teensy/td_libs_OneWire.html, 
   - Adafruit BME280 https://github.com/adafruit/Adafruit_BME280_Library,  
The board is testet with the following versions MCCI LoRaWAN LMIC library(4.1.1), DallasTemperature(3.9.0), (2.3.8), Adafruit BME280(2.2.4), Adafruit_Sensor()
3. Install board in board managers, and choose ESP32-WROOM-DA Module https://github.com/espressif/arduino-esp32 
4. Update OTAA credentials in the sketch (APPEUI / DEVEUI / APPKEY). The credentials can be obtained from your LORA network server, e.g., the TTN console  
5. Compile and flash to the ESP32.

Notes & troubleshooting
-----------------------
- If battery percent is low, the device enforces long deep-sleep.
- Large LoRa backoff forces a longer sleep via [`enforceBackoffLimit`](src_code/main.ino).
- Serial prints are available at 115200 baud for debugging.

Changes vs. archived versions
-----------------------------
[src_code/archive/main_2026-01-15_otaa-join.ino](src_code/archive/main_2026-01-15_otaa-join.ino) is the previous version of the sketch (archived, see "Archived versions" above). It performs a fresh OTAA join on every wake from deep sleep. `main.ino` adds LMIC session persistence across deep sleep so the device does not need to rejoin (and burn airtime) on every wake cycle:

- **Persisted session in RTC memory**: a new `RTC_DATA_ATTR` struct `lmicSession` (magic marker, `netid`, `devaddr`, `nwkKey`, `artKey`, `seqnoUp`, `seqnoDn`, `uplinkCount`) survives deep sleep (but not power loss/reset) and stores the OTAA session state.
- **`saveLmicSession()`** (new function): snapshots the current LMIC session keys and frame counters into `lmicSession` and prints them to serial. Called after a successful `EV_JOINED` and after every `EV_TXCOMPLETE`.
- **`restoreLmicSession()`** (new function): called once from `setup()` after `LMIC_reset()`. If a valid saved session exists, it restores it via `LMIC_setSession()`, re-applies the EU868 channel plan (channels 0–7, since `LMIC_setSession()` clears the join state) and the saved frame counters, and returns `true` so `setup()` skips the OTAA join. Returns `false` (forcing a fresh join) when there is no valid saved session yet.
- **Periodic forced rejoin**: `REJOIN_AFTER_UPLINKS` (150 uplinks) caps how long a session is reused before `restoreLmicSession()` forces a fresh OTAA join, to heal frame-counter drift, refresh session keys, and let ADR re-tune from scratch. The counter (`lmicSession.uplinkCount`) is reset to 0 on `EV_JOINED` and incremented on every `EV_TXCOMPLETE`.
- **Hardcoded OTAA keys**: `main.ino` ships with example `APPEUI`/`DEVEUI`/`APPKEY` values already filled in, whereas the archived version still uses the `FILLMEIN` placeholders described in "Build / flash" step 4.

All sensor reading, payload packing, sleep-time, and watchdog/backoff logic is unchanged between the current and archived version.

License and credits
-------------------
Copyright 2025 os4os
This project is licensed under the MIT License — see the included LICENSE file for details.

Contact
-------
Use repository issues for questions or debugging.