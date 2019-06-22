#ifndef _UART_CMD_H
#define _UART_CMD_H

int uart_open(int fd, const char *pathname);
int uart_set(int fd, int nSpeed, int nBits, char nEvent,int nStop);
int uart_close(int fd);

int uart_write(int fd, const unsigned char *w_buf, size_t len);
int uart_read(int fd,  unsigned char *r_buf, size_t len, int timeout);

#endif
