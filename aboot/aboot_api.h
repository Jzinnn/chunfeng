#ifndef ABOOT_API_H
#define ABOOT_API_H

/**
 * High-level aboot API (CLI equivalents).
 *
 * Prefer this over argc/argv for app / UI code. Status and progress are
 * reported via aboot_cbs_t (see aboot_core.h).
 *
 * CLI mapping:
 *   aboot connect [dev]           -> aboot_api_connect()
 *   aboot <image> [dev] [-v]      -> aboot_api_download()
 *   aboot at <dev> <cmd> [ms]     -> aboot_api_at()
 */

#include "aboot_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Init transport + callbacks. io may be NULL => aboot_port_usbh_serial().
 * cbs may be NULL (falls back to printf in core notify).
 */
int aboot_api_init(const aboot_io_t *io, const aboot_cbs_t *cbs);
void aboot_api_deinit(void);

/** Replace status/progress/log callbacks after init. */
void aboot_api_set_callbacks(const aboot_cbs_t *cbs);

void aboot_api_set_verbose(int on);

/**
 * Open + SMUX handshake, then disconnect (probe / connect test).
 * @param dev  device path or "auto"/NULL (default auto).
 * @return 0 on success.
 */
int aboot_api_connect(const char *dev);

/**
 * Connect + download ASR aboot image + disconnect.
 * @param img_path  firmware package path (required).
 * @param dev       device path or "auto"/NULL.
 * @param verbose   non-zero => print modem INFO during download.
 * @return 0 on success.
 */
int aboot_api_download(const char *img_path, const char *dev, int verbose);

/**
 * Raw AT over serial (no SMUX).
 * @param timeout_ms  <=0 => 3000.
 */
int aboot_api_at(const char *dev, const char *cmd, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ABOOT_API_H */
