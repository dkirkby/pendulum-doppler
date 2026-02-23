# Pendulum Doppler Experiment Firmware

Firmware for the [Infineon CY8CKIT-062S2-AI](https://www.infineon.com/CY8CKIT-062S2-AI) development kit
used in a physics experiment to study the Doppler effect.
A pendulum bob carries the kit as it swings past a stationary sound source.
The onboard microphone measures the instantaneous dominant frequency, and the onboard IMU
(coming soon) records the bob's acceleration and angular rate.
Together these datasets let students directly measure how observed frequency varies with velocity and
compare the result to the classical Doppler formula.

## Physics background

When a listener moves toward a stationary source at speed *v*, the observed frequency is

> f = f₀ · (c + v) / c

where *f₀* is the emitted frequency and *c* ≈ 343 m/s is the speed of sound.
For a pendulum of length *L* released from angle *θ*, the maximum bob speed is

> v_max = √(2gL(1 − cos θ))

At typical lab scales (L = 1 m, θ = 30°) this is about 1.6 m/s, producing a Doppler shift of
roughly 0.5 %, or ~5 Hz for a 1 kHz source — well within the resolution of this system.

## What the firmware does

| Feature | Detail |
|---------|--------|
| Audio capture | PDM microphone → 16 kHz, 16-bit PCM via DMA |
| Spectrum analysis | 4096-point Hann-windowed radix-2 FFT → one-sided PSD |
| Peak detection | Parabolic interpolation on dB-scale bins (sub-bin accuracy) |
| SNR | Peak power vs. mean noise power outside a ±10-bin exclusion window |
| Display | Real-time 40-band ASCII bar chart + peak freq/SNR/power over UART |
| Self-test | Synthetic sine waves at 220 / 440 / 880 / 1000 Hz verified on startup |
| IMU | Planned — accelerometer + gyroscope readings synchronized with audio frames |

## Hardware

**Required:** CY8CKIT-062S2-AI (PSoC 62S2 AI evaluation kit)

Connect the kit to your computer with the USB cable plugged into the **KitProg3** connector
(the micro-USB port closer to the corner of the board, labeled "KitProg3 USB").
This connector provides both the programming/debug interface and the UART bridge.

## Prerequisites

### 1. ModusToolbox 3.x

Download the installer for your OS from
[https://www.infineon.com/modustoolbox](https://www.infineon.com/modustoolbox)
and run it.

**macOS** — the default install location is `/Applications/ModusToolbox/`.
Accept the default; the `Makefile` auto-discovers tools there without any additional
environment setup.

**Linux** — the installer installs to `~/ModusToolbox/`.
The `Makefile` also auto-discovers tools there.

**Windows** — the installer installs to `%USERPROFILE%\ModusToolbox\`.
Use the **modus-shell** terminal (Start → "modus-shell") for all `make` commands;
it has all required tools on PATH.

> **Custom install path:** If you install to a non-default location, set the
> `CY_TOOLS_PATHS` environment variable to the `tools_X.Y` subdirectory:
> ```bash
> export CY_TOOLS_PATHS=/path/to/ModusToolbox/tools_3.6
> ```

#### Making the cross-compiler available on the command line

ModusToolbox bundles the GCC ARM toolchain.
Build commands invoked through `make` work without any PATH changes.
If you also want to call `arm-none-eabi-gcc` directly from a terminal
(e.g. for one-off compiles or scripting), add the bundled toolchain to your PATH:

**macOS / Linux**
```bash
# Add to ~/.zshrc or ~/.bashrc to make permanent
export PATH="/Applications/mtb-gcc-arm-eabi/14.2.1/gcc/bin:$PATH"
```

Verify with:
```bash
arm-none-eabi-gcc --version
# arm-none-eabi-gcc (GNU Arm Embedded Toolchain ...) 14.2.1
```

### 2. VS Code with the ModusToolbox Assistant extension

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Open VS Code, go to the Extensions panel, and install
   **ModusToolbox Assistant** (publisher: *Infineon Technologies*).
3. When prompted, point the extension to your ModusToolbox installation.
   If you installed to the default path it is detected automatically.

> The companion extensions **Cortex-Debug** and **C/C++** (listed in
> `.vscode/extensions.json`) will also be recommended automatically when
> you open the project.

## Getting started

### Clone and fetch libraries

```bash
git clone <repo-url> pendulum-doppler
cd pendulum-doppler
make getlibs
```

`make getlibs` downloads the firmware libraries listed in `deps/*.mtb` into the
`../mtb_shared/` directory one level above the project.
It only needs to be run once after cloning (and again if you update any `.mtb` file).

### Build and program with VS Code

1. Open the workspace: **File → Open Workspace from File** → select
   `mtb-example-psoc6-pdm-pcm.code-workspace`.
2. Connect the board via KitProg3 USB.
3. Press **Ctrl+Shift+B** (macOS: **⌘⇧B**) and choose **Build** — or press **F5**
   to build and start a debug session.
4. To just flash the board, run the **Build & Program** task
   (**Terminal → Run Task → Build & Program**).

### Build and program from the command line

```bash
make build                     # compile only (Debug config, GCC_ARM toolchain)
make program                   # compile and flash
make program CONFIG=Release    # optimised build
make clean                     # remove build artefacts
```

## Viewing UART output

Open a serial terminal at **115200 baud, 8N1** on the KitProg3 COM port.

On startup the firmware prints four self-test lines:
```
Self-test: generated 220.0 Hz, measured 220.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 440.0 Hz, measured 440.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 880.0 Hz, measured 880.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 1000.0 Hz, measured 1000.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
```

Then, approximately four times per second, the display updates with a live 40-band spectrum
and a peak-frequency line:
```
   0- 200 |##########
 200- 400 |####
...
Peak frequency: 1001.3 Hz (SNR: 24.6 dB, Power: 38.2 dB)
```

The bar chart uses an absolute 0–80 dB scale (2 dB per character).
Each frequency band spans 200 Hz across the 0–8 kHz audio range.

## Project structure

```
pendulum-doppler/
├── main.c                          # All application code
├── Makefile                        # ModusToolbox build entry point
├── bsps/TARGET_APP_CY8CKIT-062S2-AI/  # Board support package
│   ├── bsp.mk
│   ├── config/
│   │   └── GeneratedSource/        # Clock, pin, and peripheral init code
│   └── ...
├── deps/                           # Library dependency references (*.mtb)
├── openocd.tcl                     # OpenOCD config for KitProg3 + PSoC 6 CM4
└── .vscode/                        # VS Code tasks, launch, and settings
```

Shared firmware libraries (HAL, PDL, retarget-io, CMSIS) are fetched by
`make getlibs` into `../mtb_shared/` and are not stored in this repository.

## DSP pipeline

```
PDM mic → DMA (4096 samples @ 16 kHz) → DC removal → Hann window
       → radix-2 FFT → one-sided PSD (2049 bins, 3.9 Hz/bin)
       → parabolic interpolation → peak frequency + SNR
```

Key parameters (all `#define` in `main.c`):

| Constant | Value | Notes |
|----------|-------|-------|
| `FRAME_SIZE` | 4096 | Samples per FFT |
| `SAMPLE_RATE_HZ` | 16000 | Nominal sample rate |
| `SAMPLE_RATE_CORRECTION` | 1.00638 | Empirical clock calibration factor |
| `DECIMATION_RATE` | 64 | PDM decimation; requires 24.576 MHz audio clock |
| `PROCESS_EVERY_NTH` | 4 | Process one frame in four (~4 Hz update rate) |
| `NUM_DISPLAY_BANDS` | 40 | Frequency bands in ASCII chart |
