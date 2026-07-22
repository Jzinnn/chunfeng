#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aboot_internal.h"
#include "aboot_download.h"

#ifdef CONFIG_RTTKERNEL
#include <rtthread.h>
#endif

static char s_resp_buf[ABOOT_RESPONSE_SZ];
static volatile int s_resp_ready;

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

void aboot_transport_clear_response(void)
{
	s_resp_ready = 0;
	s_resp_buf[0] = '\0';
}

int aboot_transport_wait_response(char *out, size_t out_sz, int timeout_ms)
{
	int elapsed = 0;
	int last_hb = -3000;
	const int slice = 20;
	const int hb_ms = 2000;

	if (!out || out_sz == 0) {
		return -1;
	}

	while (elapsed < timeout_ms) {
		if (s_resp_ready) {
			strncpy(out, s_resp_buf, out_sz - 1);
			out[out_sz - 1] = '\0';
			s_resp_ready = 0;
			return 0;
		}
		aboot_smux_poll(slice);
		dl_delay_ms(slice);
		elapsed += slice;

		/* long waits: alive tick (device INFO muted during download) */
		if (timeout_ms >= 15000 && elapsed - last_hb >= hb_ms) {
			char msg[120];
			unsigned dropped = aboot_device_log_take_dropped();

			if (dropped) {
				snprintf(msg, sizeof(msg),
					 "waiting reply... %ds / %ds (muted %u device logs)",
					 elapsed / 1000, timeout_ms / 1000, dropped);
			} else {
				snprintf(msg, sizeof(msg),
					 "waiting reply... %ds / %ds",
					 elapsed / 1000, timeout_ms / 1000);
			}
			aboot_notify_log(msg);
			last_hb = elapsed;
		}
	}
	return -1;
}

static void aboot_transport_rx_callback(const uint8_t *status, size_t size)
{
	char response[ABOOT_RESPONSE_SZ];
	char msg[160];

	memset(response, 0, sizeof(response));
	if (size < 4) {
		snprintf(msg, sizeof(msg), "transport: malformed %u bytes", (unsigned)size);
		aboot_notify_log(msg);
		return;
	}
	if (size >= ABOOT_RESPONSE_SZ) {
		size = ABOOT_RESPONSE_SZ - 1;
	}

	if (!memcmp(status, "INFO", 4)) {
		if (aboot_device_log_is_quiet()) {
			aboot_device_log_drop();
			return;
		}
		memcpy(response, status + 4, size - 4);
		response[size - 4] = '\0';
		aboot_notify_log(response);
	} else if (!memcmp(status, "PROG", 4)) {
		memcpy(response, status + 4, size - 4);
		response[size - 4] = '\0';
		aboot_notify_progress(atoi(response));
	} else if (!memcmp(status, "DATA", 4) || !memcmp(status, "OKAY", 4) ||
		   !memcmp(status, "FAIL", 4)) {
		memcpy(response, status, size);
		response[size] = '\0';
		strncpy(s_resp_buf, response, sizeof(s_resp_buf) - 1);
		s_resp_buf[sizeof(s_resp_buf) - 1] = '\0';
		s_resp_ready = 1;
		snprintf(msg, sizeof(msg), "transport: rx %s", response);
		aboot_notify_log(msg);
	} else {
		aboot_notify_log("transport: unknown response");
	}
}

int aboot_transport_init(void)
{
	aboot_transport_clear_response();
	aboot_smux_register_cmd_callback(aboot_transport_rx_callback);
	aboot_notify_log("transport: SMUX cmd callback registered");
	return 0;
}

void aboot_transport_exit(void)
{
	aboot_smux_register_cmd_callback(NULL);
	aboot_smux_register_data_callback(NULL);
	aboot_transport_clear_response();
	aboot_notify_log("transport: exit");
}

void aboot_transport_send_cmd(const uint8_t *cmd, size_t size)
{
	aboot_smux_write_aboot_cmd(cmd, size);
}

void aboot_transport_set_data_size(size_t size)
{
	aboot_smux_set_aboot_data_size(size);
}

void aboot_transport_send_data(const uint8_t *data, size_t size)
{
	aboot_smux_write_aboot_data(data, size);
}
