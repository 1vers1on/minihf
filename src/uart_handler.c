#include "uart_handler.h"
#include "config.h"
#include "protocol/packet_parser.h"
#include "protocol/cobs.h"
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <string.h>

#define RING_BUF_SIZE    512
#define DECODED_PKT_MAX  300
#define COBS_BUF_MAX (DECODED_PKT_MAX + (DECODED_PKT_MAX / 254) + 1)

RING_BUF_DECLARE(rx_ring_buf, RING_BUF_SIZE);
RING_BUF_DECLARE(tx_ring_buf, RING_BUF_SIZE);

static uint8_t isr_cobs_buf[COBS_BUF_MAX];
static uint8_t isr_decoded_buf[DECODED_PKT_MAX];

#define RX_MSG_SIZE (sizeof(uint16_t) + DECODED_PKT_MAX)
#define RX_QUEUE_DEPTH 4
K_MSGQ_DEFINE(rx_pkt_msgq, RX_MSG_SIZE, RX_QUEUE_DEPTH, 4);

static struct k_work rx_dispatch_work;

static void rx_dispatch_handler(struct k_work *work) {
    uint8_t msg[RX_MSG_SIZE];
    while (k_msgq_get(&rx_pkt_msgq, msg, K_NO_WAIT) == 0) {
        uint16_t len = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8);
        parse_dispatch_packet(&msg[sizeof(uint16_t)], len);
    }
}

static void uart_isr(const struct device *dev, void *user_data) {
    if (!uart_irq_update(dev)) {
        return;
    }

    uint8_t byte;

    while (uart_irq_rx_ready(dev)) {
        uart_fifo_read(dev, &byte, 1);

        if (byte == 0x00) {
            uint32_t len_in_buf = ring_buf_get(&rx_ring_buf, isr_cobs_buf,
                                               sizeof(isr_cobs_buf));
            if (len_in_buf > 0) {
                int decoded_len = cobs_decode(isr_cobs_buf, len_in_buf,
                                              isr_decoded_buf);
                if (decoded_len > 0 &&
                    (size_t)decoded_len <= DECODED_PKT_MAX) {
                    uint8_t msg[RX_MSG_SIZE];
                    msg[0] = decoded_len & 0xFF;
                    msg[1] = (decoded_len >> 8) & 0xFF;
                    memcpy(&msg[sizeof(uint16_t)], isr_decoded_buf,
                           decoded_len);
                    if (k_msgq_put(&rx_pkt_msgq, msg, K_NO_WAIT) == 0) {
                        k_work_submit(&rx_dispatch_work);
                    }
                }
            }
        } else {
            ring_buf_put(&rx_ring_buf, &byte, 1);
        }
    }

    if (uart_irq_tx_ready(dev)) {
        uint8_t *data_ptr;
        uint32_t len_in_buf;

        len_in_buf = ring_buf_get_claim(&tx_ring_buf, &data_ptr, 64);

        if (len_in_buf > 0) {
            int written = uart_fifo_fill(dev, data_ptr, len_in_buf);
            
            ring_buf_get_finish(&tx_ring_buf, written);
        } else {
            uart_irq_tx_disable(dev);
        }
    }
}

void uart_handler_init() {
    k_work_init(&rx_dispatch_work, rx_dispatch_handler);
    uart_irq_callback_set(uart_dev, uart_isr);
    uart_irq_rx_enable(uart_dev);
}

int send_uart_data(const uint8_t *data, size_t length) {
    uint32_t written = ring_buf_put(&tx_ring_buf, data, length);
    uart_irq_tx_enable(uart_dev);

    return written;
}
