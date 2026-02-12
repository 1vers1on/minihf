#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include <stdint.h>
#include <stddef.h>

void uart_handler_init();
int send_uart_data(const uint8_t *data, size_t length);

#endif // UART_HANDLER_H