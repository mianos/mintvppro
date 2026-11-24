# mintvppro

A desk clock for the GeekMagic SmallTV Pro, the small ESP32 "crypto ticker" with a
1.54 inch screen. It replaces the stock firmware with a simple clock.

## What it shows

The time (12 hour), the grid power and outside temperature read from MQTT, and
the date. There are two clock faces, chosen on the settings page:

- **VFD** (the default): a glowing aqua display like an old VCR. Big segment digits
  with a half digit for the 1 of 10-12 o'clock, then GRID power in kW (positive while
  exporting, negative while importing) and the OUT temperature, each with a ▲ or ▼ if it went up or down in the
  last 10 minutes, and the date. Along the top:
  - **Wi-Fi symbol:** lit when connected, blinking while connecting.
  - **NET:** lit while the internet is reachable, dark while it is not. The symbol
    to its left says the link is up; this one says something answers at the far
    end of it. After a minute down the date is replaced by `NO NET`.
  - **SYNC:** the clock got the time from the internet in the last 3 hours.
  - **Sun or moon:** day or night, from sunrise and sunset at your location.
  - **Sunset time:** today's sunset, for example 7:07. Its colon blinks in the
    last hour before sunset.
  - The sun, moon and sunset time sit behind an amber filter, as do the
    power, temperature and date.
  - **AM / PM:** tiny, stacked at the right end; the one that applies is lit.
- **Nixie tubes:** the whole screen is one glass tube with four glowing digits,
  standing on a wooden base with the grid power, outside temperature and date on a
  brass plate. The
  first digit stays unlit before 10 o'clock, like a 3.5 digit clock.
  A small lamp in the top left corner is orange when Wi-Fi is connected, blinks
  while connecting, and is red when Wi-Fi is not set up yet. If the internet stays
  unreachable for a minute, the brass plate says `No internet` instead of the date.

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

Open `http://<clock name>/` in a browser (for example `http://nmtv-clock.mianos.com/`).
You can change:

- the clock's network name
- the time zone
- the clock face
- your location (used to work out sunset)
- the MQTT server, the grid power topic and the outside temperature topic
- the targets probed to decide whether the internet is reachable

Changes to time and location work straight away. Changes to the name or MQTT make the
clock restart. There is also a button to restart the clock.

The same settings as JSON:

```sh
curl http://nmtv-clock.mianos.com/config
curl -X POST -d '{"face":1}' http://nmtv-clock.mianos.com/config
```

## MQTT messages

The grid power topic (default `home/power/tele`) sends the power in watts, negative
while exporting (as the meter reports it). Other fields are ignored:

```json
{"power": -786}
```

The clock flips the sign, so exporting shows as positive and importing (paying) as
negative, and shows it in kW to one decimal place, from -9.9 to 99.9. If no message arrives
for 3 minutes, it shows `--` instead. The ▲ or ▼ needs a change of 0.3 kW or more.

The outside temperature topic should send a message like this:

```json
{"temperature": 22.6, "timestamp": "2026-10-13T03:31:30Z"}
```

If a reading is more than 30 minutes old, the clock shows `--` instead.

## Internet status

The NET symbol, and `NO NET` after a minute, come from a probe of the targets in
the `net_check` setting (default `1.1.1.1:53,8.8.8.8:53`, comma separated
host:port). Every 30 seconds the clock opens a TCP connection to the first target
that answers and closes it again. A completed connection, not a ping: ICMP is
often filtered and the point is whether anything is reachable, not whether it is
polite.

Two failed rounds in a row are needed before it counts as down, so a single lost
packet does not blink the lamp; one success brings it back. Wi-Fi dropping counts
at once. The state is also in `/healthz` as `internet`, `internet_rtt_ms` and
`internet_down_s` (counted from the first failed round, zero while up).

The check says nothing about DNS or throughput — it is the path out of the house,
which is what a clock with a NET lamp is for. Point `net_check` at something in
your own provider if 1.1.1.1 disagrees with the rest of the network.

## Where the project lives

On **esp-builder**, not the Mac: `~/walocal/nmtv-clock`, with its bare repo at
`~/walocal/nmtv-clock.git`. The Mac is only for the art tools below and for
reading the clock's pages. Build over ssh:

```sh
ssh esp-builder
cd ~/walocal/nmtv-clock && ./deploy.sh     # build, push, and report health
```

The builder has IDF v6.0.3; the Mac's v6.0.1 produces a different binary. Neither
`sdkconfig` nor `build/` is committed, so each host keeps its own.

## Updating the firmware

`./deploy.sh` does this in one step. By hand, build and then send the image over
Wi-Fi:

```sh
./build.sh
curl --data-binary @build/mintvpro.bin http://nmtv-clock.mianos.com/firmware
```

The clock restarts with the new firmware. If the new firmware can't get onto Wi-Fi
within 2 minutes, the clock goes back to the old firmware by itself.

The upload blocks the clock's web server until it finishes — it handles one request
at a time — so loading pages mid-deploy just waits. A sender that dies mid-body is
given up on after 90 s without progress, which frees the slot and the OTA partition.

Useful pages for checking on the clock:

```sh
curl http://nmtv-clock.mianos.com/healthz    # version, uptime, memory, reset reason, internet state
curl http://nmtv-clock.mianos.com/log        # recent log messages
curl http://nmtv-clock.mianos.com/log?prev   # the last log messages before the last restart
```

## Changing the nixie and VFD faces

Their glass, digits and text are pictures made in advance, not drawn by the clock.
To change how they look, edit `tools/nixie/nixie.html` or `tools/vfd/vfd.html` (open
it in a browser to see a preview), then run:

```sh
python3 tools/nixie/gen.py
python3 tools/vfd/gen.py
```

This needs Google Chrome on a Mac, so work in a clone there. It rewrites the `main/nixie_*` or `main/vfd_*`
files and `main/NixieArt.h` or `main/VfdArt.h`; build as usual after that.

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
