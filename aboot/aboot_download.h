#ifndef ABOOT_DOWNLOAD_H
#define ABOOT_DOWNLOAD_H

#include <stddef.h>
#include <stdbool.h>

#define ABOOT_COMMAND_SZ  128
#define ABOOT_RESPONSE_SZ 128

#ifdef __cplusplus
extern "C" {
#endif

int aboot_download_file(const char *img_path, int reboot);

#ifdef __cplusplus
}
#endif

#endif /* ABOOT_DOWNLOAD_H */
