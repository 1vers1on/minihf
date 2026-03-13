#include "radio/reciever.h"
#include "drivers/clock_control/clock_si5351a.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

/* CMSIS-DSP Includes */
#include <arm_math.h>
#include <arm_const_structs.h>

LOG_MODULE_REGISTER(receiver, LOG_LEVEL_INF);

#define ADC_NUM_CHANNELS 2
#define SAMPLES_PER_CHANNEL 128
#define BUFFER_SIZE (ADC_NUM_CHANNELS * SAMPLES_PER_CHANNEL)

static int16_t sample_buffer[BUFFER_SIZE];

struct k_poll_signal async_sig = K_POLL_SIGNAL_INITIALIZER(async_sig);
struct k_poll_event  async_evt = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, 
                                                         K_POLL_MODE_NOTIFY_ONLY,
                                                         &async_sig);

static const struct adc_dt_spec adc_i = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pa6);
static const struct adc_dt_spec adc_q = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pa7);

/* CMSIS-DSP Buffers */
q15_t fft_complex_buffer[SAMPLES_PER_CHANNEL * 2]; // Interleaved I & Q for FFT input
q15_t fft_magnitude[SAMPLES_PER_CHANNEL];          // Output magnitude buffer
uint8_t fft_buffer[128];

struct adc_sequence_options options = {
    .interval_us = 1000,      // Sample every 1ms (1kHz)
    .callback = NULL,         
    .user_data = NULL,
    .extra_samplings = SAMPLES_PER_CHANNEL - 1, 
};

struct adc_sequence sequence = {
    .options     = &options,
    .buffer      = sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution  = 12,
};

static atomic_t is_streaming = ATOMIC_INIT(0);

#define RECEIVER_STACK_SIZE 2048
#define RECEIVER_PRIORITY 5

K_THREAD_STACK_DEFINE(receiver_stack, RECEIVER_STACK_SIZE);
struct k_thread receiver_thread_data;

static void process_fft(void) {
    /* 1. Pack the interleaved ADC samples into the complex buffer. 
     * Assumes sample_buffer format: [I0, Q0, I1, Q1, ...] */
    for (int i = 0; i < SAMPLES_PER_CHANNEL; i++) {
        fft_complex_buffer[2 * i] = sample_buffer[2 * i];         // Real (I)
        fft_complex_buffer[2 * i + 1] = sample_buffer[2 * i + 1]; // Imag (Q)
    }

    /* 2. Compute Complex FFT (Forward FFT, bit reversal enabled) */
    arm_cfft_q15(&arm_cfft_sR_q15_len128, fft_complex_buffer, 0, 1);

    /* 3. Compute Magnitude */
    arm_cmplx_mag_q15(fft_complex_buffer, fft_magnitude, SAMPLES_PER_CHANNEL);
    
    /* 4. Scale 16-bit magnitude down to 8-bit for fft_buffer */
    for (int i = 0; i < SAMPLES_PER_CHANNEL; i++) {
        /* Shift right by 7 (divide by 128) to map 0-32767 down to 0-255.
         * You may need to tweak this shift value (e.g., >> 6 or >> 8) 
         * depending on your actual signal strength! */
        uint16_t scaled_val = fft_magnitude[i] >> 7; 
        
        /* Clamp the value to 255 to prevent overflow rollovers */
        if (scaled_val > 255) {
            scaled_val = 255;
        }
        
        fft_buffer[i] = (uint8_t)scaled_val;
    }
}

static void receiver_thread(void *arg1, void *arg2, void *arg3) {
    int err;
    unsigned int signaled;
    int result;

    while (1) {
        if (atomic_get(&is_streaming)) {
            k_poll_signal_reset(&async_sig);
            
            err = adc_read_async(adc_i.dev, &sequence, &async_sig);
            if (err < 0) {
                LOG_ERR("ADC read failed: %d", err);
                k_sleep(K_MSEC(100));
                continue;
            }

            err = k_poll(&async_evt, 1, K_FOREVER);
            if (err == 0) {
                k_poll_signal_check(&async_sig, &signaled, &result);
                if (signaled && result == 0) {
                    process_fft();
                }
            }
        } else {
            k_sleep(K_MSEC(50));
        }
    }
}

void receiver_init(void) {
    if (!device_is_ready(adc_i.dev) || !device_is_ready(adc_q.dev)) {
        LOG_ERR("ADC devices not ready!");
        return;
    }

    adc_channel_setup_dt(&adc_i);
    adc_channel_setup_dt(&adc_q);
    
    sequence.channels = BIT(adc_i.channel_id) | BIT(adc_q.channel_id);

    si5351a_set_ms_freq(si5351a, 0, 10000000, 0, 'A');
    si5351a_enable_output(si5351a, 0, true);

    k_thread_create(&receiver_thread_data, receiver_stack,
                    K_THREAD_STACK_SIZEOF(receiver_stack),
                    receiver_thread,
                    NULL, NULL, NULL,
                    RECEIVER_PRIORITY, 0, K_NO_WAIT);
}

void receiver_start(void) {
    atomic_set(&is_streaming, 1);
    LOG_INF("Receiver streaming started.");
}

void receiver_stop(void) {
    atomic_set(&is_streaming, 0);
    LOG_INF("Receiver streaming stopped.");
}
