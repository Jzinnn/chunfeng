#ifndef ABOOT_SMUX_H
#define ABOOT_SMUX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ABOOT_SMUX_TXBUF_SIZE
#define ABOOT_SMUX_TXBUF_SIZE (8 * 1024)
#endif

#ifndef ABOOT_SMUX_FRAME_MTU
#define ABOOT_SMUX_FRAME_MTU 1024
#endif

#define ABOOT_SMUX_PREAMBLE_SIZE 4
#define ABOOT_SMUX_PREAMBLE_UABT "UABT"

typedef void (*aboot_smux_rx_cb_t)(const uint8_t *data, size_t len);

int aboot_smux_init(void);
void aboot_smux_exit(void);

/* Poll IO, run INIT/HELLO until RUNNING or timeout_ms. */
int aboot_smux_handshake(int timeout_ms);

/* Feed RX from g_aboot_io (also used inside handshake / download loops). */
int aboot_smux_poll(int timeout_ms);

int aboot_smux_is_running(void);

void aboot_smux_register_cmd_callback(aboot_smux_rx_cb_t cb);
void aboot_smux_register_data_callback(aboot_smux_rx_cb_t cb);

void aboot_smux_set_aboot_data_size(size_t size);
void aboot_smux_write_aboot_cmd(const uint8_t *data, size_t len);
void aboot_smux_write_aboot_data(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ABOOT_SMUX_H */
