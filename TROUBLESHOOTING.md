# Troubleshooting

## "Wrong boot mode detected (0x13)" on Windows

### Symptom

`scripts/flash.ps1` fails intermittently with:

```
A fatal error occurred: Failed to connect to ESP32: Wrong boot mode detected (0x13)!
The chip needs to be in download mode.
```

Serial monitor (`scripts/monitor.ps1`) works fine at the same time. Flashing
sometimes succeeds, sometimes doesn't, with no code changes in between.

### What `0x13` actually means

`0x13` is `SPI_FAST_FLASH_BOOT` — the ESP32's strapping register says GPIO0
was **high** at EN release, so the chip booted normally instead of entering
UART download mode. In other words: esptool issued the auto-reset, but the
GPIO0 line wasn't pulled low during the critical window.

### Root causes (ranked by likelihood, from community reports)

1. **Marginal USB power delivery.** The DTR/RTS transistors on the board need
   a clean, stable 5V supply to actually toggle EN and GPIO0. Front-panel USB
   ports, cheap hubs, and worn cables routinely deliver enough power for UART
   traffic but not enough for the reset circuit. This was the culprit on at
   least one board used with this project.
2. **Bad/cheap USB data cable.** Power-only cables and worn micro-USB
   connectors cause exactly this pattern.
3. **Missing or wrong EN capacitor on clone boards.** Some ESP32 dev boards
   ship with an under-spec'd EN RC network. Fix is a 1 uF cap between EN and
   GND, or a 10k pull-down on GPIO0 (the latter ties up GPIO0 permanently).
4. **External circuitry back-driving GPIO0 or EN.** Sensors, buttons,
   breadboard leakage on those pins can hold them high during reset.
5. **Dead auto-reset transistor.** Rare.

### What `flash.ps1` does about it

`flash.ps1` loops up to `-MaxAttempts` (default 30) times, cycling through
three strategies:

1. **Slow reset sequence** — `D0|R1|W0.1|D1|R0|W0.5|D0|W0.5`, via the
   `ESPTOOL_CUSTOM_RESET_SEQUENCE` env var. Gives the EN capacitor and GPIO0
   line 500 ms to settle instead of the default ~50 ms. This is what
   eventually succeeded on a marginal-power setup during this project's
   debugging.
2. **Retry of the slow sequence** — covers transient wobbles.
3. **Swapped-polarity slow sequence** — `R0|D1|W0.1|R1|D0|W0.5|R0|W0.5`.
   Handles clone boards wired with DTR-to-EN and RTS-to-GPIO0 swapped from
   the Espressif reference design.

If all attempts fail, it's almost certainly physical: try a different USB
cable, a rear motherboard USB port, or a powered hub before replacing the
board.

### Hardware-free fallback

Hold `BOOT`, run flash, tap `EN` / `RST` once while "Connecting..." dots
appear, release `BOOT` after the connection is established. Works regardless
of USB power state because GPIO0 is manually held low.

### References

- [esptool issue #741 (ESPTOOL-455)](https://github.com/espressif/esptool/issues/741)
- [esptool issue #949 (ESPTOOL-801)](https://github.com/espressif/esptool/issues/949)
- [arduino-esp32 issue #11034](https://github.com/espressif/arduino-esp32/issues/11034)
- [PlatformIO thread — powered USB hub fix](https://community.platformio.org/t/esp-wroom32-upload-error-on-new-pc-boot-mode-problem-0x13-despite-working-serial-monitor/48376)
- [esptool troubleshooting docs](https://docs.espressif.com/projects/esptool/en/latest/esp32/troubleshooting.html)
