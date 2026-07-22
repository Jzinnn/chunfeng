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

typedef struct {
	aboot_event_t event;
	int error;
	union {
		const char *message;
		int progress; /* 0..100 */
		const char *status;
	} u;
} aboot_message_t;

typedef void (*aboot_callback_t)(const aboot_message_t *msg, void *ctx);

/* Platform provides this */
const aboot_io_t *aboot_port_usbh_serial(void);

/* Relative elapsed log: [aboot +1.234s] ... (from aboot_core_init) */
void aboot_log_printf(const char *fmt, ...);

int aboot_core_init(const aboot_io_t *io, aboot_callback_t cb, void *cb_ctx);
void aboot_core_deinit(void);

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
