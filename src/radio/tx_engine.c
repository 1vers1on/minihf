#include "radio/tx_engine.h"
#include "config.h"
#include "radio/radio.h"
#include "drivers/clock_control/clock_si5351a.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/printk.h>
#include <math.h>

static tx_sequence_t *active_seq;
static volatile bool  engine_active;
static struct k_timer tx_timer;
static struct k_work  tx_work;

#define TX_CLK_OUTPUT  0

static void tx_timer_expiry(struct k_timer *timer);
static void tx_work_handler(struct k_work *work);
static void apply_symbol(const tx_symbol_t *sym);
static void tx_off();

void tx_engine_init() {
    k_timer_init(&tx_timer, tx_timer_expiry, NULL);
    k_work_init(&tx_work, tx_work_handler);
    active_seq = NULL;
    engine_active = false;
    printk("tx_engine: initialized\n");
}

void tx_engine_start(tx_sequence_t *seq) {
    if (!seq || seq->total_symbols == 0) {
        printk("tx_engine: start failed, seq is NULL or empty\n");
        return;
    }

    tx_engine_stop();

    active_seq = seq;
    active_seq->current_index = 0;
    engine_active = true;

    printk("tx_engine: started, base_freq=%u Hz, %u symbols, repeat=%d\n",
           seq->base_freq_hz, seq->total_symbols, seq->repeat);

    const tx_symbol_t *sym = &active_seq->symbols[0];
    apply_symbol(sym);
    k_timer_start(&tx_timer, K_USEC(sym->duration_us), K_NO_WAIT);
}

void tx_engine_stop() {
    printk("tx_engine: stopping\n");
    k_timer_stop(&tx_timer);
    k_work_cancel(&tx_work);
    tx_off();
    engine_active = false;
    active_seq = NULL;
}

bool tx_engine_is_active() {
    return engine_active;
}

static void tx_timer_expiry(struct k_timer *timer) {
    k_work_submit(&tx_work);
}

static void tx_work_handler(struct k_work *work) {
    tx_sequence_t *seq = active_seq;

    if (!seq) {
        engine_active = false;
        return;
    }

    seq->current_index++;

    if (seq->current_index >= seq->total_symbols) {
        if (seq->repeat) {
            printk("tx_engine: sequence repeating\n");
            seq->current_index = 0;
        } else {
            printk("tx_engine: sequence complete\n");
            tx_off();
            engine_active = false;
            active_seq = NULL;
            return;
        }
    }

    const tx_symbol_t *sym = &seq->symbols[seq->current_index];
    printk("tx_engine: symbol %u/%u, tx_on=%d, offset=%.2f Hz, dur=%u us\n",
           seq->current_index, seq->total_symbols, sym->tx_on,
           (double)sym->freq_offset_hz, sym->duration_us);
    apply_symbol(sym);

    k_timer_start(&tx_timer, K_USEC(sym->duration_us), K_NO_WAIT);
}

static void apply_symbol(const tx_symbol_t *sym) {
    if (sym->tx_on) {
        // int64_t offset_fp = (int64_t)(sym->freq_offset_hz * SI5351A_FREQ_MULT);
        // uint64_t freq = (uint64_t)((int64_t)(active_seq->base_freq_hz *
        //                  (uint64_t)SI5351A_FREQ_MULT) + offset_fp);

        // printk("tx_engine: TX on, freq_fp=%llu\n", freq);
        // // si5351a_set_freq(si5351a, TX_CLK_OUTPUT, freq);
        // // si5351a_output_enable(si5351a, TX_CLK_OUTPUT, true);
        regulator_enable(regulator);
    } else {
        tx_off();
    }
}

static void tx_off() {
    printk("tx_engine: TX off\n");
    si5351a_enable_output(si5351a, TX_CLK_OUTPUT, false);
    regulator_disable(regulator);
}
