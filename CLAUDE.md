# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PSoC 6 MCU firmware for a PDM-to-PCM audio analyzer targeting the CY8CKIT-062S2-AI board. Built with Infineon's ModusToolbox framework. The application captures audio from an onboard digital microphone via the PDM/PCM hardware block, computes a power spectral density (PSD) via an in-place radix-2 FFT, and displays a real-time ASCII bar chart over UART at 115200 baud.

## Build Commands

```bash
make build          # Build the project (Debug config, GCC_ARM toolchain)
make program        # Build and flash to connected board via KitProg3
make clean          # Clean build artifacts
make getlibs        # Fetch/update library dependencies from .mtb files
make library-manager  # Open the Library Manager to change BSP/libraries
```

Override defaults: `make program TARGET=<BSP> TOOLCHAIN=<toolchain> CONFIG=Release`

## Architecture

**Single-file firmware** — all application logic is in `main.c`:

- **PDM/PCM capture**: Async DMA reads of 4096-sample frames at 16 kHz from the PDM mic (pins P10_4/P10_5). ISR sets a flag; main loop polls it.
- **DSP pipeline**: `compute_psd()` applies a Hann window then runs an in-place radix-2 Cooley-Tukey FFT to produce a one-sided PSD (2049 bins). `find_peak_frequency()` uses parabolic interpolation on dB-scale bins for sub-bin peak detection. Processing runs every 4th frame (~1 Hz update).
- **Display**: `display_psd()` averages PSD bins into 40 frequency bands and renders an ASCII bar chart (0–80 dB absolute scale) via UART with ANSI escape codes for screen clear.
- **Self-test**: On startup, generates synthetic sine waves (220/440/880/1000 Hz) and verifies the DSP pipeline recovers the correct frequency.
- **Clock tree**: PLL → CLK_HF[1] at 24.576 MHz → PDM/PCM block. An empirical `SAMPLE_RATE_CORRECTION` factor (1.00638) compensates for actual vs nominal sample rate.

**Key constants** (all `#define` in `main.c`): `FRAME_SIZE=4096`, `SAMPLE_RATE_HZ=16000`, `DECIMATION_RATE=64`, `NUM_DISPLAY_BANDS=40`, `DB_MIN/DB_MAX=0/80`.

## Project Structure

- `main.c` — all application code
- `Makefile` — ModusToolbox build system entry point (includes `$(CY_TOOLS_DIR)/make/start.mk`)
- `bsps/TARGET_APP_CY8CKIT-062S2-AI/` — board support package (startup, linker scripts, pin configs)
- `libs/*.mtb`, `deps/*.mtb` — library dependency references (HAL, PDL, retarget-io, CMSIS, core-make, recipe)
- `openocd.tcl` — OpenOCD debug config for KitProg3 + PSoC 6 CM4

## Hardware Notes

- Target board: CY8CKIT-062S2-AI (PSoC 62S2 with AI kit)
- Dual-core: CM0+ boots CM4; application runs on CM4
- PDM microphone on P10_4 (CLK) / P10_5 (DATA), left channel mode
- UART debug output via KitProg3 USB-UART bridge at 115200 baud
- User LED (CYBSP_USER_LED) and User Button (CYBSP_USER_BTN) available but button handler is currently a no-op
