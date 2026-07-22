#ifndef ABOOT_INTERNAL_H
#define ABOOT_INTERNAL_H

#include "aboot_core.h"
#include "aboot_smux.h"
#include "aboot_download.h"

/* shared by preamble / smux / transport / download */
extern const aboot_io_t *g_aboot_io;
extern aboot_callback_t g_aboot_cb;
extern void *g_aboot_cb_ctx;

void aboot_notify_log(const char *msg);
void aboot_notify_status(const char *status, int error);
void aboot_notify_progress(int percent);

/* Mute modem INFO/STDIO printf during download (msh printf is too slow → USB RX drop). */
void aboot_device_log_drop(void); /* internal count bump */

void aboot_set_device_log_quiet(int quiet);
int aboot_device_log_is_quiet(void);
unsigned aboot_device_log_take_dropped(void);

/* layers */
int aboot_preamble_start(void);
int aboot_transport_init(void);
void aboot_transport_exit(void);
void aboot_transport_send_cmd(const uint8_t *cmd, size_t size);
void aboot_transport_set_data_size(size_t size);
/* return 0 ok, <0 on TX error */
int aboot_transport_send_data(const uint8_t *data, size_t size);
void aboot_transport_clear_response(void);
int aboot_transport_wait_response(char *out, size_t out_sz, int timeout_ms);

#endif /* ABOOT_INTERNAL_H */
