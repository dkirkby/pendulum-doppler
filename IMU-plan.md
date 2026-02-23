# IMU Integration Plan — BMI270 Accelerometer Readout

## Objective

Read the instantaneous 3-axis acceleration vector at the midpoint of each processed
audio frame (~64 ms after DMA capture begins) with as high accuracy as possible.

---

## 1. Hardware context (confirmed from BSP)

| Item | Detail |
|------|--------|
| Sensor | Bosch BMI270 (6-axis: accelerometer + gyroscope) |
| Bus | I2C — SCL: `CYBSP_I2C_SCL` (P0_2), SDA: `CYBSP_I2C_SDA` (P0_3) |
| I2C address | 0x68 (SDO = GND, default) — **verify against board schematic** |
| Interrupt pins | `CYBSP_IMU_INT1` (P1_5), `CYBSP_IMU_INT2` (P0_4) — available but not used in this plan |
| I2C SCB block | Not pre-configured in BSP `cycfg_peripherals.h`; must initialize via HAL |

---

## 2. Library dependency

The Bosch BMI270 SensorAPI is the official driver. It provides a platform-agnostic C
API that requires only two callback functions: `i2c_read` and `i2c_write`.

**Step:** Check the ModusToolbox Library Manager first (`make library-manager`) for an
Infineon-provided `sensor-motion-bmi270` or `mtb-bmi270` package — if one exists, prefer
it as it may provide pre-wired HAL callbacks. If not found, add the Bosch SensorAPI
directly.

To add the Bosch SensorAPI as an MTB dependency, create `deps/bmi270-sensor-api.mtb`:
```
https://github.com/boschsensortec/BMI270-Sensor-API#v2.86.1#$$ASSET_REPO$$/bmi270-sensor-api/v2.86.1
```
Then run `make getlibs` to fetch it into `../mtb_shared/`.

The API header to include is `bmi270.h`; the files to compile are `bmi2.c` and `bmi270.c`
(both are plain C, no special build requirements).

---

## 3. BMI270 configuration for maximum accuracy

For a pendulum experiment the signal bandwidth is well under 5 Hz, so we can trade
off high-frequency response for lower noise.

### Accelerometer settings

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `acc_odr` | `BMI2_ACC_ODR_200HZ` (200 Hz) | 5 ms between samples; a sample is always within ±2.5 ms of the target midpoint |
| `acc_range` | `BMI2_ACC_RANGE_4G` | ±4 g covers the full swing; better resolution than ±8 g or ±16 g |
| `acc_bwp` | `BMI2_ACC_OSR4_AVG1` | 4× over-sampling in performance mode; reduces noise by √4 = 2× at 80 Hz bandwidth |
| `acc_filter_perf` | `BMI2_PERF_OPT_MODE` | Performance (normal power) mode, not low-power |

The gyroscope is not needed for this experiment (velocity can be computed from the
accelerometer data and pendulum geometry) and should be kept disabled to reduce I²C
traffic and power.

### Effective noise floor

The BMI270 datasheet specifies accelerometer noise density of ~120 µg/√Hz in
performance mode. At 80 Hz effective bandwidth after OSR4:

> noise_rms ≈ 120 µg/√Hz × √80 Hz ≈ 1.07 mg (1σ) per axis

This is better than 1 mg per reading, which is more than sufficient for measuring
accelerations in a ~1 g pendulum experiment.

If still lower noise is needed after integration, apply a software average of
**N = 5 consecutive readings** in the timer ISR (see §4). At 200 Hz the 5 samples
span 25 ms, a window still much narrower than the pendulum period. Software averaging
adds another √5 ≈ 2.2× improvement, reducing the noise floor to ~0.5 mg per axis.

---

## 4. Timing strategy — HAL timer at the frame midpoint

### Why a dedicated timer

- One audio frame = 2048 samples ÷ 16 kHz = **128 ms**
- Midpoint = **64 ms** after `cyhal_pdm_pcm_read_async` is called
- The PDM/PCM ISR fires at the *end* of the frame, which is too late; we cannot go back

A one-shot `cyhal_timer_t` is armed each time `cyhal_pdm_pcm_read_async` is called
and fires at 64 ms. Its ISR reads the IMU immediately (the I2C transaction for 6 bytes
at 400 kHz takes ≈ 100 µs — negligible).

### Timer ISR data flow

```
cyhal_pdm_pcm_read_async()  ←── called at end of every PDM/PCM ISR
        │
        ├── arm 64 ms one-shot timer
        │
       64 ms later
        │
        └── timer_isr()
                ├── bmi2_get_sensor_data() → reads ACC x,y,z via I2C
                ├── (optional) average N=5 samples over ~25 ms window
                └── stores result in:  volatile imu_sample_t imu_midpoint;
                                       volatile bool        imu_ready = true;

       128 ms later
        │
        └── pdm_pcm_isr_handler()
                └── sets pdm_pcm_flag = true

       main loop (every frame)
                └── reads imu_midpoint + audio data together
```

### Data structure

```c
typedef struct {
    float ax_g;   /* acceleration x, in g */
    float ay_g;   /* acceleration y, in g */
    float az_g;   /* acceleration z, in g */
} imu_sample_t;

volatile imu_sample_t imu_midpoint;
volatile bool         imu_ready = false;
```

Raw BMI270 output is a signed 16-bit integer. Conversion:
```
value_g = raw_value * (range_g / 32768.0f)
```
For ±4 g range: `value_g = raw * (4.0f / 32768.0f)`.

---

## 5. I2C initialization

The HAL I2C master is initialized once in `main()` after `cybsp_init()`:

```c
cyhal_i2c_t i2c_obj;
cyhal_i2c_cfg_t i2c_cfg = {
    .is_slave        = false,
    .address         = 0,       /* ignored for master */
    .frequencyhal_hz = 400000u  /* 400 kHz fast mode */
};
cyhal_i2c_init(&i2c_obj, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL);
cyhal_i2c_configure(&i2c_obj, &i2c_cfg);
```

The two SensorAPI callbacks wrap the HAL calls:

```c
BMI2_INTF_RETURN_TYPE bmi2_i2c_read(uint8_t reg_addr, uint8_t *data,
                                    uint32_t len, void *intf_ptr)
{
    uint8_t dev_addr = *(uint8_t *)intf_ptr;
    cyhal_i2c_master_write(&i2c_obj, dev_addr, &reg_addr, 1, 10, true);
    cyhal_i2c_master_read(&i2c_obj,  dev_addr, data,      len, 10, true);
    return BMI2_OK;
}

BMI2_INTF_RETURN_TYPE bmi2_i2c_write(uint8_t reg_addr, const uint8_t *data,
                                     uint32_t len, void *intf_ptr)
{
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    uint8_t dev_addr = *(uint8_t *)intf_ptr;
    cyhal_i2c_master_write(&i2c_obj, dev_addr, buf, len + 1, 10, true);
    return BMI2_OK;
}

void bmi2_delay_us(uint32_t period, void *intf_ptr)
{
    cyhal_system_delay_us(period);
}
```

---

## 6. BMI270 initialization sequence

```c
struct bmi2_dev bmi2;
uint8_t bmi2_dev_addr = BMI2_I2C_PRIM_ADDR;   /* 0x68 */

bmi2.intf       = BMI2_I2C_INTF;
bmi2.intf_ptr   = &bmi2_dev_addr;
bmi2.read       = bmi2_i2c_read;
bmi2.write      = bmi2_i2c_write;
bmi2.delay_us   = bmi2_delay_us;
bmi2.read_write_len = 32;

bmi270_init(&bmi2);    /* loads config file, verifies chip ID */

/* Configure accelerometer */
struct bmi2_sens_config config;
config.type = BMI2_ACCEL;
config.cfg.acc.odr        = BMI2_ACC_ODR_200HZ;
config.cfg.acc.range      = BMI2_ACC_RANGE_4G;
config.cfg.acc.bwp        = BMI2_ACC_OSR4_AVG1;
config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
bmi2_set_sensor_config(&config, 1, &bmi2);

/* Enable only the accelerometer (gyro stays off) */
uint8_t sens_list = BMI2_ACCEL;
bmi2_sensor_enable(&sens_list, 1, &bmi2);
```

---

## 7. HAL timer setup

```c
cyhal_timer_t midpoint_timer;
cyhal_timer_cfg_t timer_cfg = {
    .compare_value = 0,
    .period        = 63999,    /* 64 ms at 1 MHz tick → 64000 counts - 1 */
    .direction     = CYHAL_TIMER_DIR_UP,
    .is_compare    = false,
    .is_continuous = false,    /* one-shot */
    .value         = 0
};
cyhal_timer_init(&midpoint_timer, NC, NULL);
cyhal_timer_configure(&midpoint_timer, &timer_cfg);
cyhal_timer_set_frequency(&midpoint_timer, 1000000u);   /* 1 MHz = 1 µs resolution */
cyhal_timer_register_callback(&midpoint_timer, midpoint_timer_isr, NULL);
cyhal_timer_enable_event(&midpoint_timer, CYHAL_TIMER_IRQ_TERMINAL_COUNT,
                         CYHAL_ISR_PRIORITY_DEFAULT, true);
```

Each call to `cyhal_pdm_pcm_read_async` is followed by:
```c
cyhal_timer_reset(&midpoint_timer);
cyhal_timer_start(&midpoint_timer);
```

---

## 8. Self-test / validation

Add a startup check similar to the existing audio self-test:

1. After `bmi270_init`, read the chip ID register and confirm it equals `0x24`.
2. Read a single accelerometer sample while the board is stationary and flat.
   Verify that |az| ≈ 1 g and |ax|, |ay| < 0.1 g (board level check).
3. Print both checks over UART alongside the existing audio self-test lines.

---

## 9. Changes to `main.c` — summary

| Location | Change |
|----------|--------|
| Includes | Add `bmi270.h` |
| Macros | Add `BMI2_I2C_ADDR`, `IMU_ODR_HZ 200`, `IMU_RANGE_G 4` |
| Globals | Add `cyhal_i2c_t`, `struct bmi2_dev`, `cyhal_timer_t`, `volatile imu_sample_t`, `volatile bool imu_ready` |
| `main()` | Call `imu_init()` after `clock_init()`; call `imu_self_test()` alongside audio self-tests |
| `pdm_pcm_isr_handler()` | After setting `pdm_pcm_flag`, arm the midpoint timer |
| New: `imu_init()` | I2C init + BMI270 init + timer init |
| New: `midpoint_timer_isr()` | Read accelerometer, convert to g, store in `imu_midpoint`, set `imu_ready` |
| New: `imu_self_test()` | Chip ID check + static level check |
| Main loop (process block) | Snapshot `imu_midpoint` immediately after entering the process block; include ax/ay/az in UART output |

---

## 10. Open questions

1. **I2C address**: Confirm 0x68 vs. 0x69 from the board schematic (the SDO net on the BMI270 footprint).
2. **Library Manager**: Check whether Infineon publishes a ready-made `sensor-motion-bmi270` MTB library before pulling the raw Bosch SensorAPI — it may already wrap the HAL callbacks.
3. **Gyroscope**: Not needed for initial implementation, but angular rate (ω) from the gyroscope multiplied by pendulum arm length L directly gives the bob velocity and could improve Doppler predictions. Worth enabling later.
4. **UART output format**: Decide whether to log ax/ay/az on every processed frame line (easy to parse) or as a separate tagged line for post-processing.
