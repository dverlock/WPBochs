#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wpb_net_data_cb)(int handle, const uint8_t *data, int length, void *user);
typedef void (*wpb_net_connect_cb)(int handle, int success, void *user);
typedef void (*wpb_net_closed_cb)(int handle, void *user);

int  wpb_net_udp_open(const char *host, int port, wpb_net_data_cb on_data, void *user);
void wpb_net_udp_send(int handle, const uint8_t *data, int length);
void wpb_net_udp_close(int handle);

int  wpb_net_tcp_open(const char *host, int port, wpb_net_connect_cb on_connect,
                       wpb_net_data_cb on_data, wpb_net_closed_cb on_closed, void *user);
void wpb_net_tcp_send(int handle, const uint8_t *data, int length);
void wpb_net_tcp_close(int handle);

#ifdef __cplusplus
}
#endif
