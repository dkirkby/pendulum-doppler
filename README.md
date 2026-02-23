# Pendulum Doppler Experiment Firmware

Firmware for the [Infineon CY8CKIT-062S2-AI](https://www.infineon.com/CY8CKIT-062S2-AI) development kit
used in a physics experiment to study the Doppler effect.
A pendulum bob carries the kit as it swings past a stationary sound source.
The onboard microphone measures the instantaneous dominant frequency, and the onboard IMU
records the bob's 3-axis acceleration synchronized to each audio frame.
Together these datasets let students directly measure how observed frequency varies with velocity and
compare the result to the classical Doppler formula.

## Physics background

When a listener moves toward a stationary source at speed *v*, the observed frequency is

> f = f₀ · (c + v) / c

where *f₀* is the emitted frequency and *c* ≈ 343 m/s is the speed of sound.
For a pendulum of length *L* released from angle *θ*, the maximum bob speed is

> v_max = √(2gL(1 − cos θ))

At typical lab scales (L = 1.5 m, θ = 30°) this is about 2.0 m/s, producing a Doppler shift of
roughly 0.6 %, or ~18 Hz for a 3 kHz source — well within the resolution of this system.

## What the firmware does

| Feature | Detail |
|---------|--------|
| Audio capture | PDM microphone → 16 kHz, 16-bit PCM via DMA |
| Spectrum analysis | 2048-point Hann-windowed radix-2 FFT → one-sided PSD |
| Peak detection | Parabolic interpolation on dB-scale bins (sub-bin accuracy) |
| SNR | Peak power vs. mean noise power outside a ±10-bin exclusion window |
| IMU | BMI270 accelerometer read at the midpoint of each audio frame via I2C |
| Data output | CSV over UART: timestamp, peak frequency, SNR, power, ax, ay, az |
| Self-test | Synthetic sine waves at 220 / 440 / 880 / 1000 Hz + IMU level check on startup |

## Hardware

**Required:** CY8CKIT-062S2-AI (PSoC 62S2 AI evaluation kit)

Connect the kit to your computer with the USB cable plugged into the **KitProg3** connector
(the micro-USB port closer to the corner of the board, labeled "KitProg3 USB").
This connector provides both the programming/debug interface and the UART bridge.

**Recommended sound source:** A tone generator or speaker at **3–4 kHz** placed at the equilibrium
position of the pendulum. Higher frequencies give a larger Doppler shift in Hz and are easier to
distinguish from background noise.

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

## Viewing and recording data

Open a serial terminal at **115200 baud, 8N1** on the KitProg3 COM port.

On startup the firmware prints four audio self-test lines followed by an IMU level check:
```
Self-test: generated 220.0 Hz, measured 220.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 440.0 Hz, measured 440.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 880.0 Hz, measured 880.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
Self-test: generated 1000.0 Hz, measured 1000.0 Hz, error 0.00 Hz, SNR 57.4 dB, Power 44.0 dB
IMU self-test: ax=0.012 g, ay=-0.008 g, az=0.998 g  (expect |az|~1, |ax|,|ay|<0.2)
```

Then, approximately 8 times per second, one CSV line is printed per processed audio frame:
```
t_ms,freq_hz,snr_db,power_db,ax_g,ay_g,az_g
0,3001.2,28.4,42.1,0.0123,-0.0081,0.9984
128,3001.5,27.9,41.8,0.1042,-0.0076,0.9945
256,3004.1,26.3,41.2,0.3817,-0.0082,0.9247
...
```

To save a run to a file, redirect the serial port output:

**macOS / Linux**
```bash
# List available ports first
ls /dev/tty.*          # macOS
ls /dev/ttyACM*        # Linux

# Record to a file (Ctrl-C to stop)
cat /dev/tty.usbmodem1234 > run1.csv
```

**Windows (PowerShell)**
```powershell
# Find the COM port in Device Manager, then:
& "C:\Windows\System32\mode.com" COM3: BAUD=115200 PARITY=n DATA=8 STOP=1
Get-Content \\.\COM3 | Out-File run1.csv
```

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

Shared firmware libraries (HAL, PDL, retarget-io, CMSIS, BMI270 SensorAPI) are fetched by
`make getlibs` into `../mtb_shared/` and are not stored in this repository.

## DSP pipeline

```
PDM mic → DMA (2048 samples @ 16 kHz) → DC removal → Hann window
       → radix-2 FFT → one-sided PSD (1025 bins, 7.8 Hz/bin)
       → parabolic interpolation → peak frequency + SNR
```

Key parameters (all `#define` in `main.c`):

| Constant | Value | Notes |
|----------|-------|-------|
| `FRAME_SIZE` | 2048 | Samples per FFT; 128 ms per frame |
| `SAMPLE_RATE_HZ` | 16000 | Nominal sample rate |
| `SAMPLE_RATE_CORRECTION` | 1.00638 | Empirical clock calibration factor |
| `DECIMATION_RATE` | 64 | PDM decimation; requires 24.576 MHz audio clock |
| `PROCESS_EVERY_NTH` | 1 | Every frame is processed (~7.8 Hz update rate) |
| `NUM_DISPLAY_BANDS` | 40 | Frequency bands in ASCII chart (debug use) |

## IMU timing

The BMI270 accelerometer is read once per audio frame at the **frame midpoint** (64 ms after
DMA capture begins). A one-shot HAL timer fires at 64 ms and sets a flag; the main loop performs
the blocking I2C read before the frame ends, ensuring the acceleration sample is time-aligned
to the center of the audio window.

| Parameter | Value |
|-----------|-------|
| Sensor | Bosch BMI270, I2C address 0x68 |
| Output data rate | 200 Hz |
| Range | ±4 g |
| Filter | OSR4 performance mode (~1 mg noise floor) |
| I2C bus | 400 kHz (CYBSP_I2C_SCL / CYBSP_I2C_SDA) |
