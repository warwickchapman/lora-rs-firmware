# Sensor Support

This document describes the sensor connections supported by LoRa Relay Switcher
hardware and firmware.

## LRS-v1.2 LoRa Relay Switcher

The LRS-v1.2 board provides a 6-way auxiliary sensor header, CN1. It exposes
power, ground, two ESP8266 GPIO lines, and one analog input.

```text
Pin 1  GND
Pin 2  3V3
Pin 3  GPIO0 / PROG
Pin 4  GPIO2 / LED
Pin 5  GND
Pin 6  AIN
```

### Pin Functions

`Pin 1 - GND`

Ground reference for sensors.

`Pin 2 - 3V3`

3.3 V sensor supply. Use only for low-power sensors suitable for the board's
3.3 V rail.

`Pin 3 - GPIO0 / PROG`

Digital GPIO exposed on the sensor header. Current firmware uses this as the
default DS18B20 1-wire data pin. GPIO0 is also an ESP8266 boot/programming
strap pin, so attached hardware must not pull it low during boot.

`Pin 4 - GPIO2 / LED`

Digital GPIO exposed on the sensor header. This line is shared with the board
status/identify LED in the standard firmware profile. GPIO2 is also an ESP8266
boot strap pin and must not be pulled low during boot.

`Pin 5 - GND`

Second ground terminal for convenient sensor wiring.

`Pin 6 - AIN`

Analog input path to the ESP8266 ADC. Current firmware uses this for the
4-20 mA tank level sensor through a current-to-voltage converter. This is not a
digital input, not 1-wire, and not I2C.

### Current Supported Connections

DS18B20 temperature sensor:

```text
GND   -> Pin 1 or Pin 5
VCC   -> Pin 2
DATA  -> Pin 3 GPIO0
```

4-20 mA tank level sensor:

```text
converter analog output           -> Pin 6 AIN
converter/sensor ground reference -> Pin 1 or Pin 5
converter VCC, if compatible      -> Pin 2 3V3
```

Dry-contact or float-switch inputs are supported on the product's normal input
terminals, not on AIN. Do not connect a dry contact directly to AIN unless a
defined analog interface circuit is being used.

### Simultaneous Use

The normal intended simultaneous configuration is:

```text
DS18B20 temperature sensor -> GPIO0 / 1-wire
4-20 mA tank sensor        -> AIN / analog
dry contact or float       -> normal input terminal
```

That combination is valid because each function uses a different hardware path.

### I2C Mode

GPIO0 and GPIO2 can also be used together as an I2C bus:

```text
GPIO0 -> SDA
GPIO2 -> SCL
```

This has been tested with an SSD1306 OLED/LCD display.

I2C mode should be treated as an alternate sensor/display profile, not something
that is freely additive with the standard GPIO0/GPIO2 uses. When GPIO0 and GPIO2
are assigned to I2C:

- GPIO0 is no longer available for the DS18B20 1-wire temperature sensor.
- GPIO2 is no longer available as a normal status/identify LED output.
- Any firmware LED behavior must be disabled, moved, or made I2C-mode aware.
- Attached I2C devices must not disturb the ESP8266 boot strap requirements.

Because both GPIO0 and GPIO2 are boot-sensitive ESP8266 pins, I2C devices and
pull-ups must be wired so that neither line is held low during boot.

### Possible But Not Standard

Multiple DS18B20 sensors may be possible on the same GPIO0 1-wire bus if wiring
and pull-up are correct. Current firmware should be treated as supporting the
single configured DS18B20 reading path unless multi-sensor enumeration is added
explicitly.

Additional I2C devices may share the GPIO0/GPIO2 I2C bus when I2C mode is
enabled, subject to address compatibility, power budget, cable length, and boot
strap constraints.

### Important Cautions

- GPIO0 and GPIO2 must not be pulled low during boot.
- AIN accepts an analog voltage only; it is not a digital bus.
- The AIN path is board-scaled and calibrated in firmware, currently using a
  3553 mV effective ADC reference for the 4-20 mA tank sensor calculation.
- GPIO2 is shared with the LED in the standard firmware profile, so using it for
  I2C or other sensors conflicts with status/identify indications.
- Sensors powered from 3V3 must be compatible with 3.3 V logic and the available
  current from the board.
