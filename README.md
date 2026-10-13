# mintvppro

A desk clock for the GeekMagic SmallTV Pro, the small ESP32 "crypto ticker" with a
1.54 inch screen. It replaces the stock firmware with a simple clock.

## What it shows

- The time, in big digits (12 or 24 hour).
- Inside and outside temperature, read from MQTT. A small arrow shows if each one
  went up or down in the last 10 minutes.
- The date and today's sunset time.
- A dot in the top left corner for Wi-Fi: green is connected, orange is connecting,
  red means Wi-Fi is not set up yet.

## The button

The touch button is on the front.

- **Tap:** makes the screen one step dimmer. After the dimmest step it goes back to
  the brightest. The clock remembers this after a restart.
- **Hold for 10 seconds, within one minute of turning it on:** forgets the Wi-Fi
  details and restarts, so you can set up Wi-Fi again. To do this, unplug the clock,
  plug it back in, then hold the button. Holding it at other times does nothing.

## First start

1. Power the clock from its USB-C port.
2. The screen says `USE ESPTOUCH V2`.
3. Use the ESP-Touch app on your phone (version 2) to send your Wi-Fi name and
   password to the clock.
4. The clock joins Wi-Fi, gets the time from the internet, and starts working.

## Settings page

Open `http://<clock name>/` in a browser (for example `http://mintvpro.mianos.com/`).
You can change:

- the clock's network name
- the time zone and 12 or 24 hour time
- your location (used to work out sunset)
- the MQTT server and the two temperature topics

Changes to time and location work straight away. Changes to the name or MQTT make the
clock restart. There is also a button to restart the clock.

The same settings as JSON:

```sh
curl http://mintvpro.mianos.com/config
curl -X POST -d '{"use_24h":1}' http://mintvpro.mianos.com/config
```

## Temperature messages

Each temperature topic should send a message like this:

```json
{"temperature": 22.6, "timestamp": "2026-10-13T03:31:30Z"}
```

If a reading is more than 30 minutes old, the clock shows `--` instead.

## Updating the firmware

You need ESP-IDF 6.0.1. Build, then send the new firmware over Wi-Fi:

```sh
./build.sh
curl --data-binary @build/mintvpro.bin http://mintvpro.mianos.com/firmware
```

The clock restarts with the new firmware. If the new firmware can't get onto Wi-Fi
within 2 minutes, the clock goes back to the old firmware by itself.

Useful pages for checking on the clock:

```sh
curl http://mintvpro.mianos.com/healthz    # version, uptime, memory, why it last restarted
curl http://mintvpro.mianos.com/log        # recent log messages
curl http://mintvpro.mianos.com/log?prev   # the last log messages before the last restart
```

## Flashing with a cable

You only need this the first time, or if updating over Wi-Fi stops working.

The USB-C port only gives power. To flash, open the case and connect a 3.3 V USB to
serial adapter to the pins inside (TX, RX, GND). Then:

1. Connect GPIO0 to GND.
2. Turn the power off and on.
3. Remove the GPIO0 wire.
4. Run `./build.sh flash`.

Power the board from its own USB-C port while flashing. A weak power supply makes it
keep restarting.

## Hardware

Pins for the SmallTV Pro (ESP32 version). These come from
[giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod).

| Part | GPIO |
| --- | --- |
| Screen (ST7789, SPI): data / clock / DC / reset | 23 / 18 / 2 / 4 |
| Screen chip select | tied to GND on the board |
| Backlight (low = on) | 25 |
| Touch button | 32 (touch channel T9) |
