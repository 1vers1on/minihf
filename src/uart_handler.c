#include "uart_handler.h"
#include "config.h"
#include "protocol/packet_parser.h"
#include "protocol/cobs.h"
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/drivers/uart.h>

#define RING_BUF_SIZE 512

RING_BUF_DECLARE(rx_ring_buf, RING_BUF_SIZE);
RING_BUF_DECLARE(tx_ring_buf, RING_BUF_SIZE);

static void uart_isr(const struct device *dev, void *user_data) {
    if (!uart_irq_update(dev)) {
        return;
    }

    uint8_t byte;

    while (uart_irq_rx_ready(dev)) {
        uart_fifo_read(dev, &byte, 1);

        if (byte == 0x00) {
            uint8_t cobs_data[256];
            uint32_t len_in_buf = ring_buf_get(&rx_ring_buf, cobs_data, sizeof(cobs_data));
            if (len_in_buf > 0) {
                uint8_t decoded_data[300];
                int decoded_len = cobs_decode(cobs_data, len_in_buf, decoded_data);
                if (decoded_len > 0) {
                    parse_dispatch_packet(decoded_data, decoded_len);
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
    uart_irq_callback_set(uart_dev, uart_isr);
    uart_irq_rx_enable(uart_dev);
}

int send_uart_data(const uint8_t *data, size_t length) {
    uint32_t written = ring_buf_put(&tx_ring_buf, data, length);
    uart_irq_tx_enable(uart_dev);

    return written;
}