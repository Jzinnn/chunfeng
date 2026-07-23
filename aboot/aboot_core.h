#ifndef ABOOT_CORE_H
#define ABOOT_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transport IO injected by platform (Melis: usbh_serial / ttyUSB).
 * Implementations live in port_usbh_serial.c
 */
typedef struct aboot_io {
	int (*open)(const char *dev_path);
	void (*close)(void);
	int (*write)(const uint8_t *buf, size_t len);
	/* return bytes read, 0 on timeout, <0 on error */
	int (*read)(uint8_t *buf, size_t len, int timeout_ms);
} aboot_io_t;

typedef enum {
	ABOOT_EVT_LOG = 0,
	ABOOT_EVT_PROGRESS,
	ABOOT_EVT_STATUS,
} aboot_event_t;

/* ABOOT_EVT_PROGRESS source */
typedef enum {
	ABOOT_PROG_SCRIPT = 0, /* host: firmware.bin file offset */
	ABOOT_PROG_DEVICE,     /* modem PROG frame while flashing */
} aboot_progress_src_t;

typedef struct {
	aboot_event_t event;
	int error;
	union {
		const char *message;
		struct {
			int percent; /* 0..100 */
			aboot_progress_src_t src;
		} progress;
		const char *status;
	} u;
} aboot_message_t;

/* Unified event callback (LOG / PROGRESS / STATUS) */
typedef void (*aboot_callback_t)(const aboot_message_t *msg, void *ctx);

/* Dedicated callbacks — prefer these when UI only needs status/progress */
typedef void (*aboot_status_cb_t)(const char *status, int error, void *ctx);
typedef void (*aboot_progress_cb_t)(aboot_progress_src_t src, int percent,
				    void *ctx);
typedef void (*aboot_log_cb_t)(const char *message, void *ctx);

typedef struct aboot_cbs {
	aboot_status_cb_t on_status;
	aboot_progress_cb_t on_progress;
	aboot_log_cb_t on_log;
	void *ctx;
} aboot_cbs_t;

/* Platform provides this */
const aboot_io_t *aboot_port_usbh_serial(void);

/* Relative elapsed log: [aboot +1.234s] ... (from aboot_core_init) */
void aboot_log_printf(const char *fmt, ...);

/* 1 = print modem INFO/STDIO during download (may slow / risk USB RX drop) */
void aboot_set_verbose(int on);
int aboot_get_verbose(void);

int aboot_core_init(const aboot_io_t *io, aboot_callback_t cb, void *cb_ctx);
/* Same as init, plus dedicated status/progress/log callbacks (any may be NULL) */
int aboot_core_init_ex(const aboot_io_t *io, aboot_callback_t cb, void *cb_ctx,
		       const aboot_cbs_t *cbs);
void aboot_core_deinit(void);

/* Register / replace dedicated callbacks after init (NULL clears) */
void aboot_set_status_callback(aboot_status_cb_t cb, void *ctx);
void aboot_set_progress_callback(aboot_progress_cb_t cb, void *ctx);
void aboot_set_log_callback(aboot_log_cb_t cb, void *ctx);
void aboot_set_callbacks(const aboot_cbs_t *cbs);

/* open path, send UABT preamble, enter SMUX (stub until ported) */
int aboot_core_connect(const char *dev_path);

/* download ASR-style script image; reboot!=0 => reboot after done */
int aboot_core_download_file(const char *img_path, int reboot);

/* raw AT over serial (no SMUX): open, send cmd, print reply until OK/ERROR/timeout */
int aboot_core_at(const char *dev_path, const char *cmd, int timeout_ms);

int aboot_core_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* ABOOT_CORE_H */
