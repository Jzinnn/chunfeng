/*
 * Synchronous port of aboot-tiny aboot-download.c (no Contiki PROCESS).
 * Waits for OKAY/FAIL/DATA via aboot_transport_wait_response() + smux_poll.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "aboot_internal.h"
#include "aboot_download.h"

#ifdef CONFIG_RTTKERNEL
#include <rtthread.h>
#endif

#define MAX_DOWNLOAD_BUF_SIZE 1024

#define OOB_COMMAND_COMPLETE   0
#define OOB_COMMAND_REBOOT     1
#define OOB_COMMAND_DISCONNECT 2

static const char cmd_getvar[] = "getvar:";
static const char cmd_download[] = "download:";
static const char cmd_call[] = "call";
static const char cmd_nop[] = "nop";
static const char cmd_reboot[] = "reboot";
static const char cmd_complete[] = "complete";
static const char cmd_disconnect[] = "disconnect";
static const char crane_version_bootrom[] = "2019.01.15";

static FILE *s_file;
static size_t s_firmware_size;
static long s_firmware_end;
static bool s_reboot_after;
static int s_oob_command;
static bool s_is_crane_bootrom;

static void dl_delay_ms(int ms)
{
#ifdef CONFIG_RTTKERNEL
	rt_thread_mdelay(ms);
#else
	volatile int i;
	while (ms-- > 0) {
		for (i = 0; i < 10000; i++) {
		}
	}
#endif
}

/* simple strsep substitute */
static char *dl_strsep(char **stringp, const char *delim)
{
	char *s, *tok;
	const char *spanp;
	int c, sc;

	if (!(s = *stringp)) {
		return NULL;
	}
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0) {
					s = NULL;
				} else {
					s[-1] = 0;
				}
				*stringp = s;
				return tok;
			}
		} while (sc != 0);
	}
}

static int download_read_line(char *line)
{
	long current_pos;

	while (1) {
		current_pos = ftell(s_file);
		if (current_pos < 0 || current_pos >= s_firmware_end) {
			return 0;
		}
		if (!fgets(line, ABOOT_COMMAND_SZ, s_file)) {
			return -1;
		}
		if (line[0] == '\n') {
			continue;
		}
		{
			size_t n = strlen(line);
			if (n && line[n - 1] == '\n') {
				line[n - 1] = '\0';
			}
		}
		return 1;
	}
}

static int download_read_cmd_response(char *cmd_line, char *response)
{
	int rc = download_read_line(cmd_line);

	if (rc <= 0) {
		return rc;
	}
	rc = download_read_line(response);
	return rc;
}

static int download_handle_getvar(const char *variable, char *expect, const char *response)
{
	if (memcmp(expect, response, 4)) {
		return -1;
	}
	expect += 4;
	response += 4;

	if (!strcmp(variable, "max-download-size")) {
		uint32_t expect_size = (uint32_t)strtoul(expect, NULL, 16);
		uint32_t response_size = (uint32_t)strtoul(response, NULL, 16);

		if (expect_size > response_size) {
			return -1;
		}
	} else if (!strcmp(variable, "version-bootrom")) {
		if (strcmp(expect, response)) {
			return -1;
		}
		s_is_crane_bootrom = !strcmp(crane_version_bootrom, response);
	} else {
		char *token;
		char *exp = expect;

		while ((token = dl_strsep(&exp, "|"))) {
			if (!strcmp(token, response)) {
				break;
			}
		}
		if (!token) {
			return -1;
		}
	}
	return 0;
}

static int download_handle_download(uint32_t size, char *expect, const char *response)
{
	uint32_t expect_size, response_size;

	if (memcmp(expect, response, 4)) {
		return -1;
	}
	expect += 4;
	response += 4;
	expect_size = (uint32_t)strtoul(expect, NULL, 16);
	response_size = (uint32_t)strtoul(response, NULL, 16);
	if (size != response_size || size != expect_size) {
		return -1;
	}
	return 0;
}

static int download_do_download_data(uint32_t size)
{
	uint8_t buf[MAX_DOWNLOAD_BUF_SIZE];
	size_t len;
	uint32_t left = size;

	aboot_transport_set_data_size(size);

	while (left) {
		len = left > MAX_DOWNLOAD_BUF_SIZE ? MAX_DOWNLOAD_BUF_SIZE : left;
		if (fread(buf, 1, len, s_file) != len) {
			return -1;
		}
		aboot_transport_send_data(buf, len);
		left -= (uint32_t)len;
		/* keep RX path alive during long TX */
		aboot_smux_poll(0);
	}
	return 0;
}

static int download_wait_device(char *dev_resp, int timeout_ms)
{
	return aboot_transport_wait_response(dev_resp, ABOOT_RESPONSE_SZ, timeout_ms);
}

static void download_notify_progress(void)
{
	long pos;
	int pct;

	if (!s_file || s_firmware_size == 0) {
		return;
	}
	pos = ftell(s_file);
	if (pos < 0) {
		return;
	}
	pct = (int)((pos * 100) / (long)s_firmware_size);
	if (pct > 100) {
		pct = 100;
	}
	aboot_notify_progress(pct);
}

static int download_send_and_wait(char *cmd_line, char *expect_template,
				  char *dev_resp, int timeout_ms)
{
	char msg[160];

	(void)expect_template;
	aboot_transport_clear_response();
	snprintf(msg, sizeof(msg), "download: tx '%s'", cmd_line);
	aboot_notify_log(msg);
	if (!strcmp(cmd_line, "call")) {
		aboot_notify_status("PREBOOT", 0);
		aboot_notify_log("download: call/preboot (device INFO muted; watch PROG)");
	}
	aboot_transport_send_cmd((const uint8_t *)cmd_line, strlen(cmd_line));
	if (download_wait_device(dev_resp, timeout_ms) < 0) {
		aboot_notify_log("download: wait response timeout");
		return -1;
	}
	return 0;
}

int aboot_download_file(const char *img_path, int reboot)
{
	char cmd_line[ABOOT_COMMAND_SZ];
	char expect[ABOOT_RESPONSE_SZ];
	char dev_resp[ABOOT_RESPONSE_SZ];
	char magic[16];
	uint32_t hdr_size;
	int num, rc;
	bool oob = false;
	char msg[160];

	if (!img_path) {
		return -1;
	}

	s_file = fopen(img_path, "rb");
	if (!s_file) {
		snprintf(msg, sizeof(msg), "download: open %s fail", img_path);
		aboot_notify_log(msg);
		aboot_notify_status("FAILED", -1);
		return -1;
	}

	if (fseek(s_file, 0, SEEK_END) != 0) {
		fclose(s_file);
		s_file = NULL;
		return -1;
	}
	s_firmware_size = (size_t)ftell(s_file);
	if (fseek(s_file, 0, SEEK_SET) != 0) {
		fclose(s_file);
		s_file = NULL;
		return -1;
	}
	s_firmware_end = (long)s_firmware_size;
	s_reboot_after = reboot != 0;
	s_oob_command = s_reboot_after ? OOB_COMMAND_REBOOT : OOB_COMMAND_COMPLETE;
	s_is_crane_bootrom = false;

	snprintf(msg, sizeof(msg), "download: file=%s size=%u reboot=%d",
		 img_path, (unsigned)s_firmware_size, reboot);
	aboot_notify_log(msg);
	aboot_notify_progress(0);
	/* msh printf of modem INFO is far slower than Linux PC → mute during download */
	aboot_set_device_log_quiet(1);
	aboot_notify_log("download: device INFO muted (PROG/OKAY still shown)");

	if (download_read_line(cmd_line) <= 0) {
		goto fail;
	}
	num = sscanf(cmd_line, "%8s%8x", magic, &hdr_size);
	if (num != 2 || (size_t)hdr_size != s_firmware_size) {
		snprintf(msg, sizeof(msg), "download: bad header magic=%s size=%u (file %u)",
			 magic, (unsigned)hdr_size, (unsigned)s_firmware_size);
		aboot_notify_log(msg);
		goto fail;
	}
	snprintf(msg, sizeof(msg), "download: magic=%s", magic);
	aboot_notify_log(msg);

	rc = download_read_cmd_response(cmd_line, expect);
	if (rc <= 0) {
		goto fail;
	}
	if (download_send_and_wait(cmd_line, expect, dev_resp, 10000) < 0) {
		goto fail;
	}

	for (;;) {
		download_notify_progress();

		if (strlen(cmd_line) > strlen(cmd_getvar) &&
		    !memcmp(cmd_line, cmd_getvar, strlen(cmd_getvar))) {
			char variable[ABOOT_COMMAND_SZ];
			char expect_copy[ABOOT_RESPONSE_SZ];

			strcpy(variable, cmd_line + strlen(cmd_getvar));
			strncpy(expect_copy, expect, sizeof(expect_copy) - 1);
			expect_copy[sizeof(expect_copy) - 1] = '\0';
			if (download_handle_getvar(variable, expect_copy, dev_resp) < 0) {
				aboot_notify_log("download: getvar mismatch");
				goto fail;
			}
		} else if (strlen(cmd_line) > strlen(cmd_download) &&
			   !memcmp(cmd_line, cmd_download, strlen(cmd_download))) {
			uint32_t size = (uint32_t)strtoul(cmd_line + strlen(cmd_download), NULL, 16);
			char expect_copy[ABOOT_RESPONSE_SZ];

			strncpy(expect_copy, expect, sizeof(expect_copy) - 1);
			expect_copy[sizeof(expect_copy) - 1] = '\0';
			if (download_handle_download(size, expect_copy, dev_resp) < 0) {
				aboot_notify_log("download: DATA size mismatch");
				goto fail;
			}
			if (download_do_download_data(size) < 0) {
				aboot_notify_log("download: send payload fail");
				goto fail;
			}
			/* forgery: next expected response line from image */
			strcpy(cmd_line, expect);
			if (download_read_line(expect) <= 0) {
				goto fail;
			}
			if (download_wait_device(dev_resp, 60000) < 0) {
				aboot_notify_log("download: wait after payload timeout");
				goto fail;
			}
			continue;
		} else {
			if (strcmp(expect, dev_resp)) {
				snprintf(msg, sizeof(msg), "download: expect '%s' got '%s'",
					 expect, dev_resp);
				aboot_notify_log(msg);
				goto fail;
			}
		}

		if (!oob) {
			rc = download_read_cmd_response(cmd_line, expect);
		} else {
			if (!s_reboot_after) {
				aboot_set_device_log_quiet(0);
				aboot_notify_progress(100);
				aboot_notify_status("SUCCEEDED", 0);
				fclose(s_file);
				s_file = NULL;
				return 0;
			}
			rc = 0;
		}

		if (rc > 0) {
			int wait_ms = 30000;

			if (!strcmp(cmd_line, cmd_call) && s_is_crane_bootrom) {
				strcpy(cmd_line, cmd_nop);
			}
			/* call runs device preboot (PSRAM/PMIC/...); often >30s of INFO flood */
			if (!strcmp(cmd_line, cmd_call)) {
				wait_ms = 300000;
			}
			if (download_send_and_wait(cmd_line, expect, dev_resp, wait_ms) < 0) {
				goto fail;
			}
		} else if (rc < 0) {
			goto fail;
		} else {
			oob = true;
			if (s_reboot_after) {
				if (s_oob_command == OOB_COMMAND_REBOOT) {
					strcpy(cmd_line, cmd_reboot);
					s_oob_command = OOB_COMMAND_COMPLETE;
				} else if (s_oob_command == OOB_COMMAND_COMPLETE) {
					strcpy(cmd_line, cmd_complete);
					s_oob_command = OOB_COMMAND_DISCONNECT;
				} else if (s_oob_command == OOB_COMMAND_DISCONNECT) {
					strcpy(cmd_line, cmd_disconnect);
					dl_delay_ms(30);
					aboot_transport_clear_response();
					aboot_transport_send_cmd((const uint8_t *)cmd_line,
								 strlen(cmd_line));
					dl_delay_ms(100);
					aboot_notify_progress(100);
					aboot_notify_status("SUCCEEDED", 0);
					aboot_set_device_log_quiet(0);
					fclose(s_file);
					s_file = NULL;
					return 0;
				}
			} else {
				if (s_oob_command == OOB_COMMAND_COMPLETE) {
					strcpy(cmd_line, cmd_complete);
				}
			}
			strcpy(expect, "OKAY");
			if (download_send_and_wait(cmd_line, expect, dev_resp, 10000) < 0) {
				goto fail;
			}
		}
	}

fail:
	aboot_set_device_log_quiet(0);
	aboot_notify_status("FAILED", -1);
	if (s_file) {
		fclose(s_file);
		s_file = NULL;
	}
	return -1;
}
