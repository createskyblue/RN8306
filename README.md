# RN8306 Driver (4V+3I / 8 kHz / HSDC Continuous Waveform Stream)

**English** | [简体中文](README_zh.md)

## What is the RN8306?

The RN8306 is a dedicated three-phase energy-metering IC from Shenzhen Renergy
Microelectronics (part of the Shanghai Belling / HDSC group), belonging to the
RN830x/RN730x V3 family. It integrates seven Sigma-Delta ADC channels — three
phase voltages, three phase currents, and one independent differential input IN
(usable as a 4th voltage channel or neutral current) — and performs
high-accuracy measurement of three-phase voltage / current / power / energy
along with harmonic analysis, complying with IEC and IR46 standards. The chip
exposes two serial links: an **SPI slave interface** for register access
(configuration, calibration, metering results) and an **HSDC high-speed
interface** on which the chip acts as SPI master, streaming raw ADC waveform
buffers to the MCU at up to 4.096 MHz. Typical applications: smart meters,
energy management and power monitoring.

> Note: **HSDC** (the chip's high-speed waveform interface implemented by this
> driver) and **HDSC** (HuaDa Semiconductor, the parent group behind Renergy:
> CEC → HDSC → Shanghai Belling → Renergy) are two unrelated things — a
> coincidence of acronyms.

## About this driver

A platform-independent C driver for the Renergy **RN8306** three-phase
metering IC (RN830x/RN730x V3 family). OOP-style objects with dependency
injection — you inject a single IO callback and switch-case your way through
SPI transfers / delays / reset. The driver obtains a **7-channel, 8 kHz,
24-bit continuous waveform stream** (UA/UB/UC + 4th voltage on IN + IA/IB/IC)
through the chip's HSDC interface — no internal buffering, data is handed to
your callback the moment it arrives.

## Repository layout

```
src/        Driver core: rn8306.h / rn8306.c (C standard library only)
examples/   Usage example: rn8306_example.c (injected callback, IRQ entry, data flow)
test/       Host self-test: rn8306_selftest.c (built-in "mock chip", 73 assertions)
doc/        Protocol notes: RN8306_通信协议_SPI_HSDC.md (SPI + HSDC protocol digest,
            4V+3I register sequence, pitfall checklist — in Chinese)
            rn8306.pdf (official RN830x_RN730x V3 datasheet Rev1.5)
ref/        Official reference material: user manual V1.2, datasheet V1.3,
            vendor HSDC.c sample code, RN8302B resource kit
```

## Quick start

```c
rn8306_if_t itf = { .io = my_io_cb, .on_wave = my_wave_cb, .on_evt = my_evt_cb };
rn8306_config_t cfg = RN8306_CONFIG_DEFAULT;   /* 8 kHz / SUM32 / byte-reversed */
cfg.wave_channels = 7;                          /* 4V+3I: UA,UB,UC,IA,IB,IC,IN */

rn8306_init(&rn, &itf, &cfg);
rn8306_check_device(&rn);      /* read DeviceID as a sanity check */
rn8306_hsdc_setup(&rn);        /* WMSW EMM switch, two-level write enable, BUFCFG=10010 */
/* Queue the MCU SPI-slave receive buffers before starting the stream. */
rn8306_hsdc_start(&rn, true);  /* completed SPI-slave RX -> rn8306_hsdc_feed() */
```

Full wiring example: [examples/rn8306_example.c](examples/rn8306_example.c).
Protocol and register references: [doc/RN8306_通信协议_SPI_HSDC.md](doc/RN8306_通信协议_SPI_HSDC.md).

Build the example / self-test (host gcc, no hardware needed):

```bash
cd examples && gcc -std=c99 -Wall -Wextra -I../src -o demo ../src/rn8306.c rn8306_example.c
cd test     && gcc -std=c99 -Wall -Wextra -I../src -o selftest ../src/rn8306.c rn8306_selftest.c && ./selftest
```

## Hardware verification

**Verified on an ESP32-S3 + RN8306 board on October 29, 2026. Register access,
continuous waveform acquisition and SD recording all operated correctly.**

| Check | Result |
|-------|--------|
| DeviceID | `0x830200`, SPI frame checksum passed |
| Waveform stream | 7 channels, 24-bit samples; product integration measured about 6.4 kHz, source-driver default is 8 kHz |
| Continuous acquisition | 130 seconds, zero frame checksum errors, length errors or pipeline drops |
| SD recording | 192,000 sample frames per complete 30-second segment |
| Recorded file integrity | Header, waveform data and footer CRCs all passed |
| Host driver tests | All 73 assertions passed |

The WSAVECON read-back check handles bits `[5:4]` as live cache status while
strictly checking the remaining configuration bits. This fixes the false
`ERR_VERIFY` returned when writing `0x82` and reading `0xB2`.

The hardware results cover the communication, acquisition and storage path.
Analog gain calibration, phase accuracy and physical input mapping require
signal-generator measurements. Register-specific notes are kept in
[doc/OPEN_QUESTIONS.md](doc/OPEN_QUESTIONS.md).

For synchronous sampling (DS_CFG=00 in `dma_wavecon`) set
`cfg.sample_rate_hz` to the expected waveform rate in Hz (valid range
2560-35840 Hz), and choose `cfg.sync_follow_frequency` when the chip should
track the input frequency. Fixed-rate DS_CFG=01/10/11 configurations require
`cfg.sample_rate_hz = RN8306_SAMPLE_RATE_8K_HZ`. Otherwise `hsdc_setup()`
returns `ERR_PARAM`; HSCLK selection follows manual §6.6: duty cycle <= 100%.
