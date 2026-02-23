/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PDM PCM Example
*              for ModusToolbox.
*
* Related Document: See README.md 
*
*
*******************************************************************************
* Copyright 2021-2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

#include "stdlib.h"
#include "math.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* Define how many samples in a frame */
#define FRAME_SIZE                  (2048)
/* Process every Nth frame (1 = every frame; ~7.8 Hz update rate at 16kHz/2048) */
#define PROCESS_EVERY_NTH           1u
/* Desired sample rate. Typical values: 8/16/22.05/32/44.1/48kHz */
#define SAMPLE_RATE_HZ              16000u
/* Empirical correction for actual PDM sample rate (calibrate with known tone) */
#define SAMPLE_RATE_CORRECTION      1.00638f
/* Decimation Rate of the PDM/PCM block. Typical value is 64 */
#define DECIMATION_RATE             64u
/* Audio Subsystem Clock. Typical values depends on the desire sample rate:
- 8/16/48kHz    : 24.576 MHz
- 22.05/44.1kHz : 22.579 MHz */
#define AUDIO_SYS_CLOCK_HZ          24576000u
/* PDM/PCM Pins */
#define PDM_DATA                    P10_5
#define PDM_CLK                     P10_4
/* Number of PSD output bins (one-sided spectrum for real input) */
#define PSD_SIZE                    (FRAME_SIZE / 2 + 1)
/* Number of frequency bands for ASCII display */
#define NUM_DISPLAY_BANDS           40u
/* Absolute dB scale range for display */
#define DB_MIN                      0
#define DB_MAX                      80
/* Each character represents this many dB */
#define DB_PER_CHAR                 2
/* Bar width is derived from the dB range */
#define MAX_BAR_WIDTH               ((DB_MAX - DB_MIN) / DB_PER_CHAR)
/* Half-width of the exclusion window (in bins) around the peak for SNR */
#define SNR_EXCLUSION_HALFWIDTH     10u

/*******************************************************************************
* Function Prototypes
********************************************************************************/
void button_isr_handler(void *arg, cyhal_gpio_event_t event);
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event);
void clock_init(void);
void compute_psd(const int16_t *samples, float *psd_out);
void display_psd(const float *psd);
float find_peak_frequency(const float *psd, float *snr_out, float *peak_db_out);
void self_test(int16_t *buf, float *psd_buf, float test_freq);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Interrupt flags */
volatile bool button_flag = false;
volatile bool pdm_pcm_flag = true;

/* Frame counter for rate limiting */
uint32_t frame_count = 0;

/* Elapsed time in ms; incremented by one frame period each time a frame is processed */
uint32_t elapsed_ms = 0;

/* PSD output buffer */
float psd[PSD_SIZE];

/* Actual sample rate (corrected from hardware clock readback) */
float actual_sample_rate;

/* HAL Object */
cyhal_pdm_pcm_t pdm_pcm;
cyhal_clock_t   audio_clock;
cyhal_clock_t   pll_clock;

/* HAL Config */
const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg = 
{
    .sample_rate     = SAMPLE_RATE_HZ,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_LEFT,
    .word_length     = 16,  /* bits */
    .left_gain       = 0,   /* dB */
    .right_gain      = 0,   /* dB */
};

/*This structure is used to initialize callback*/
cyhal_gpio_callback_data_t cb_data =
    {
        .callback = button_isr_handler,
        .callback_arg = NULL
 };


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* The main function for Cortex-M4 CPU does the following:
*  Initialization:
*  - Initializes all the hardware blocks
*  Do forever loop:
*  - Check if PDM/PCM flag is set. If yes, report the current volume
*  - Update the LED status based on the volume and the noise threshold
*  - Check if the User Button was pressed. If yes, reset the noise threshold
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    int16_t  audio_frame[FRAME_SIZE] = {0};

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Init the clocks */
    clock_init();

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    /* Initialize the User LED */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    /* Initialize the User Button */
    cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULLUP, CYBSP_BTN_OFF);
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL, CYHAL_ISR_PRIORITY_DEFAULT, true);
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &cb_data);

    /* Initialize the PDM/PCM block */
    cyhal_pdm_pcm_init(&pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_pcm_isr_handler, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);
    cyhal_pdm_pcm_start(&pdm_pcm);
    
    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("Pendulum Doppler Analyzer\r\n\n");

    /* Run self-tests with synthetic tones */
    self_test(audio_frame, psd, 220.0f);
    self_test(audio_frame, psd, 440.0f);
    self_test(audio_frame, psd, 880.0f);
    self_test(audio_frame, psd, 1000.0f);
    printf("\r\n");

    /* CSV header for data logging (ax_g,ay_g,az_g columns will be added with IMU) */
    printf("t_ms,freq_hz,snr_db,power_db\r\n");

    for(;;)
    {
        /* Check if a microphone frame is ready */
        if (pdm_pcm_flag)
        {
            pdm_pcm_flag = false;
            frame_count++;

            /* Process every Nth frame, discard the rest */
            if (frame_count >= PROCESS_EVERY_NTH)
            {
                frame_count = 0;

                compute_psd(audio_frame, psd);
                float snr, peak_db;
                float peak_freq = find_peak_frequency(psd, &snr, &peak_db);
                printf("%lu,%.2f,%.1f,%.1f\r\n",
                    (unsigned long)elapsed_ms, peak_freq, snr, peak_db);
                elapsed_ms += FRAME_SIZE * 1000u / SAMPLE_RATE_HZ;
            }

            /* Setup to read the next frame */
            cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
        }

        /* Handle User Button press */
        if (button_flag)
        {
            button_flag = false;
        }

        cyhal_syspm_sleep();
    }
}

/*******************************************************************************
* Function Name: button_isr_handler
********************************************************************************
* Summary:
*  Button ISR handler. Set a flag to be processed in the main loop.
*
* Parameters:
*  arg: not used
*  event: event that occurred
*
*******************************************************************************/
void button_isr_handler(void *arg, cyhal_gpio_event_t event)
{
    (void) arg;
    (void) event;

    button_flag = true;
}

/*******************************************************************************
* Function Name: pdm_pcm_isr_handler
********************************************************************************
* Summary:
*  PDM/PCM ISR handler. Set a flag to be processed in the main loop.
*
* Parameters:
*  arg: not used
*  event: event that occurred
*
*******************************************************************************/
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event)
{
    (void) arg;
    (void) event;

    pdm_pcm_flag = true;
}

/*******************************************************************************
* Function Name: find_peak_frequency
********************************************************************************
* Summary:
*  Find the frequency of the PSD peak using parabolic interpolation around
*  the bin with the highest power. Skips bin 0 (DC).
*
* Parameters:
*  psd:         input PSD array (PSD_SIZE floats)
*  snr_out:     if non-NULL, receives the estimated SNR in dB
*               (peak power vs mean noise power, excluding bins near the peak)
*  peak_db_out: if non-NULL, receives the absolute peak power in dB
*
* Return:
*  Estimated peak frequency in Hz.
*
*******************************************************************************/
float find_peak_frequency(const float *psd, float *snr_out, float *peak_db_out)
{
    /* Find the bin with maximum power, skipping DC */
    uint32_t peak_bin = 1;
    float peak_val = psd[1];
    for (uint32_t k = 2; k < PSD_SIZE; k++)
    {
        if (psd[k] > peak_val)
        {
            peak_val = psd[k];
            peak_bin = k;
        }
    }

    /* Report absolute peak power in dB */
    if (peak_db_out != NULL)
    {
        *peak_db_out = (peak_val > 0.0f) ? 10.0f * log10f(peak_val) : 0.0f;
    }

    /* Compute SNR: peak power / mean power of bins outside exclusion window */
    if (snr_out != NULL)
    {
        float noise_sum = 0.0f;
        uint32_t noise_count = 0;
        uint32_t excl_lo = (peak_bin > SNR_EXCLUSION_HALFWIDTH) ?
                            peak_bin - SNR_EXCLUSION_HALFWIDTH : 1;
        uint32_t excl_hi = (peak_bin + SNR_EXCLUSION_HALFWIDTH < PSD_SIZE) ?
                            peak_bin + SNR_EXCLUSION_HALFWIDTH : PSD_SIZE - 1;
        for (uint32_t k = 1; k < PSD_SIZE; k++)
        {
            if (k < excl_lo || k > excl_hi)
            {
                noise_sum += psd[k];
                noise_count++;
            }
        }
        if (noise_count > 0 && noise_sum > 0.0f)
        {
            float noise_mean = noise_sum / (float)noise_count;
            *snr_out = 10.0f * log10f(peak_val / noise_mean);
        }
        else
        {
            *snr_out = 0.0f;
        }
    }

    /* Parabolic interpolation for sub-bin accuracy (dB-scale for Hann window) */
    float bin_freq = actual_sample_rate / (float)FRAME_SIZE;
    float interp_bin = (float)peak_bin;

    if (peak_bin > 1 && peak_bin < PSD_SIZE - 1)
    {
        float alpha = 10.0f * log10f(psd[peak_bin - 1]);
        float beta  = 10.0f * log10f(psd[peak_bin]);
        float gamma = 10.0f * log10f(psd[peak_bin + 1]);
        float denom = alpha - 2.0f * beta + gamma;
        if (denom != 0.0f)
        {
            float delta = 0.5f * (alpha - gamma) / denom;
            interp_bin = (float)peak_bin + delta;
        }
    }

    return interp_bin * bin_freq;
}

/*******************************************************************************
* Function Name: print_db_scale
********************************************************************************
* Summary:
*  Print a horizontal dB scale line with tick marks every 10 dB.
*
*******************************************************************************/
void print_db_scale(void)
{
    /* Print label padding to match frequency label width */
    printf("          ");
    for (int db = DB_MIN; db <= DB_MAX; db += DB_PER_CHAR)
    {
        if (db % 10 == 0)
        {
            printf("|");
        }
        else
        {
            printf(" ");
        }
    }
    printf("\r\n          ");
    for (int db = DB_MIN; db <= DB_MAX; db += DB_PER_CHAR)
    {
        if (db % 10 == 0)
        {
            /* Print the tens digit of the dB value */
            printf("%d", (db / 10) % 10);
        }
        else
        {
            printf(" ");
        }
    }
    printf("0 dB\r\n");
}

/*******************************************************************************
* Function Name: display_psd
********************************************************************************
* Summary:
*  Display the PSD as an ASCII bar chart over UART. The PSD bins are averaged
*  into NUM_DISPLAY_BANDS linearly spaced frequency bands. Bars use an
*  absolute dB scale (10*log10 of band power) with a scale printed above
*  and below.
*
* Parameters:
*  psd: input PSD array (PSD_SIZE floats)
*
*******************************************************************************/
void display_psd(const float *psd)
{
    float band_power[NUM_DISPLAY_BANDS];
    uint32_t bins_per_band = PSD_SIZE / NUM_DISPLAY_BANDS;
    float freq_per_band = actual_sample_rate / 2.0f / (float)NUM_DISPLAY_BANDS;

    /* Average PSD bins into display bands */
    for (uint32_t b = 0; b < NUM_DISPLAY_BANDS; b++)
    {
        float sum = 0.0f;
        uint32_t start = b * bins_per_band;
        for (uint32_t k = start; k < start + bins_per_band; k++)
        {
            sum += psd[k];
        }
        band_power[b] = sum / (float)bins_per_band;
    }

    /* Clear screen and move cursor home */
    printf("\x1b[2J\x1b[;H");

    /* Display each band as a labeled bar */
    for (uint32_t b = 0; b < NUM_DISPLAY_BANDS; b++)
    {
        uint32_t freq_lo = (uint32_t)(b * freq_per_band);
        uint32_t freq_hi = (uint32_t)((b + 1) * freq_per_band);

        /* Convert to absolute dB */
        uint32_t bar_len = 0;
        if (band_power[b] > 0.0f)
        {
            float db = 10.0f * log10f(band_power[b]);
            if (db > (float)DB_MIN)
            {
                float chars = (db - (float)DB_MIN) / (float)DB_PER_CHAR;
                bar_len = (uint32_t)chars;
                if (bar_len > MAX_BAR_WIDTH) bar_len = MAX_BAR_WIDTH;
            }
        }

        printf("%4lu-%4lu |", (unsigned long)freq_lo, (unsigned long)freq_hi);
        for (uint32_t i = 0; i < bar_len; i++)
        {
            printf("#");
        }
        printf("\r\n");
    }

    /* Print bottom scale */
    print_db_scale();
}

/*******************************************************************************
* Function Name: compute_psd
********************************************************************************
* Summary:
*  Compute the one-sided power spectral density of a real-valued audio frame
*  using an in-place radix-2 Cooley-Tukey FFT. FRAME_SIZE must be a power of 2.
*
*  The output array psd_out must have space for PSD_SIZE floats.
*  Each bin k corresponds to frequency k * SAMPLE_RATE_HZ / FRAME_SIZE.
*  Units are proportional to V^2/Hz (normalized by FRAME_SIZE).
*
* Parameters:
*  samples:  input audio frame (FRAME_SIZE int16_t values)
*  psd_out:  output PSD array (PSD_SIZE floats)
*
*******************************************************************************/
void compute_psd(const int16_t *samples, float *psd_out)
{
    /* Working arrays for real and imaginary parts */
    static float re[FRAME_SIZE];
    static float im[FRAME_SIZE];

    /* Precomputed periodic Hann window coefficients (computed once on first call) */
    static float hann[FRAME_SIZE];
    static bool hann_initialized = false;
    if (!hann_initialized)
    {
        for (uint32_t i = 0; i < FRAME_SIZE; i++)
        {
            hann[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)FRAME_SIZE));
        }
        hann_initialized = true;
    }

    /* Remove DC offset */
    float mean = 0.0f;
    for (uint32_t i = 0; i < FRAME_SIZE; i++)
    {
        mean += (float)samples[i];
    }
    mean /= (float)FRAME_SIZE;

    /* Apply Hann window, copy to float, and zero imaginary part */
    for (uint32_t i = 0; i < FRAME_SIZE; i++)
    {
        re[i] = ((float)samples[i] - mean) * hann[i];
        im[i] = 0.0f;
    }

    /* Bit-reversal permutation */
    uint32_t j = 0;
    for (uint32_t i = 1; i < FRAME_SIZE; i++)
    {
        uint32_t bit = FRAME_SIZE >> 1;
        while (j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if (i < j)
        {
            float tmp;
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }

    /* Cooley-Tukey FFT butterfly stages */
    for (uint32_t len = 2; len <= FRAME_SIZE; len <<= 1)
    {
        float angle = -2.0f * (float)M_PI / (float)len;
        float w_re = cosf(angle);
        float w_im = sinf(angle);

        for (uint32_t i = 0; i < FRAME_SIZE; i += len)
        {
            float tw_re = 1.0f;
            float tw_im = 0.0f;

            for (uint32_t k = 0; k < len / 2; k++)
            {
                uint32_t u = i + k;
                uint32_t v = u + len / 2;

                float t_re = tw_re * re[v] - tw_im * im[v];
                float t_im = tw_re * im[v] + tw_im * re[v];

                re[v] = re[u] - t_re;
                im[v] = im[u] - t_im;
                re[u] += t_re;
                im[u] += t_im;

                float new_tw_re = tw_re * w_re - tw_im * w_im;
                tw_im = tw_re * w_im + tw_im * w_re;
                tw_re = new_tw_re;
            }
        }
    }

    /* Compute one-sided PSD: |X[k]|^2 / N */
    float inv_n = 1.0f / (float)FRAME_SIZE;
    psd_out[0] = (re[0] * re[0] + im[0] * im[0]) * inv_n;
    for (uint32_t k = 1; k < FRAME_SIZE / 2; k++)
    {
        /* Factor of 2 for one-sided spectrum (folding negative frequencies) */
        psd_out[k] = 2.0f * (re[k] * re[k] + im[k] * im[k]) * inv_n;
    }
    psd_out[FRAME_SIZE / 2] = (re[FRAME_SIZE / 2] * re[FRAME_SIZE / 2] +
                                im[FRAME_SIZE / 2] * im[FRAME_SIZE / 2]) * inv_n;
}

/*******************************************************************************
* Function Name: self_test
********************************************************************************
* Summary:
*  Generate a synthetic sine wave at a known frequency, compute its PSD,
*  and verify that find_peak_frequency recovers the expected value. Uses
*  the nominal SAMPLE_RATE_HZ for generation and analysis to isolate the
*  DSP pipeline from hardware clock uncertainties.
*
* Parameters:
*  buf:       working buffer (FRAME_SIZE int16_t samples)
*  psd_buf:   working buffer (PSD_SIZE floats)
*  test_freq: frequency of the test tone in Hz
*
*******************************************************************************/
void self_test(int16_t *buf, float *psd_buf, float test_freq)
{
    /* Generate sine wave at test_freq assuming nominal sample rate */
    float phase_inc = 2.0f * (float)M_PI * test_freq / (float)SAMPLE_RATE_HZ;
    for (uint32_t i = 0; i < FRAME_SIZE; i++)
    {
        buf[i] = (int16_t)(16000.0f * sinf(phase_inc * (float)i));
    }

    /* Use nominal sample rate for analysis */
    float saved_rate = actual_sample_rate;
    actual_sample_rate = (float)SAMPLE_RATE_HZ;

    compute_psd(buf, psd_buf);
    float snr, peak_db;
    float measured = find_peak_frequency(psd_buf, &snr, &peak_db);

    printf("Self-test: generated %.1f Hz, measured %.1f Hz, error %.2f Hz, SNR %.1f dB, Power %.1f dB\r\n",
        test_freq, measured, measured - test_freq, snr, peak_db);

    /* Restore actual sample rate */
    actual_sample_rate = saved_rate;
}

/*******************************************************************************
* Function Name: clock_init
********************************************************************************
* Summary:
*  Initialize the clocks in the system.
*
*******************************************************************************/
void clock_init(void)
{
    /* Initialize the PLL */
    cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[0]);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    cyhal_clock_set_enabled(&pll_clock, true, true);

    /* Initialize the audio subsystem clock (CLK_HF[1]) 
     * The CLK_HF[1] is the root clock for the I2S and PDM/PCM blocks */
    cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);

    /* Source the audio subsystem clock from PLL */
    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);

    /* Apply empirical correction to nominal sample rate */
    actual_sample_rate = (float)SAMPLE_RATE_HZ * SAMPLE_RATE_CORRECTION;
}

/* [] END OF FILE */
