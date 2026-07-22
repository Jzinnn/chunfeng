#include <stdio.h>
#include <string.h>
#include <rtthread.h>

#include "aboot_internal.h"

const aboot_io_t *g_aboot_io;
aboot_callback_t g_aboot_cb;
void *g_aboot_cb_ctx;

/* Expand \\r \\n \\t \\\\ in AT string; return length written (no NUL required). */
static size_t aboot_unescape(const char *in, char *out, size_t out_sz)
{
	size_t n = 0;

	if (!in || !out || out_sz == 0) {
		return 0;
	}
	while (*in && n + 1 < out_sz) {
		if (*in == '\\' && in[1]) {
			in++;
			switch (*in) {
			case 'r':
				out[n++] = '\r';
				break;
			case 'n':
				out[n++] = '\n';
				break;
			case 't':
				out[n++] = '\t';
				break;
			case '\\':
				out[n++] = '\\';
				break;
			default:
				out[n++] = *in;
				break;
			}
			in++;
			continue;
		}
		out[n++] = *in++;
	}
	out[n] = '\0';
	return n;
}

static int aboot_at_done(const char *acc)
{
	return (strstr(acc, "OK") != NULL) ||
	       (strstr(acc, "ERROR") != NULL) ||
	       (strstr(acc, "FAIL") != NULL);
}

void aboot_notify_log(const char *msg)
{
	aboot_message_t m;

	if (!g_aboot_cb || !msg) {
		printf("[aboot] %s\n", msg ? msg : "");
		return;
	}
	memset(&m, 0, sizeof(m));
	m.event = ABOOT_EVT_LOG;
	m.u.message = msg;
	g_aboot_cb(&m, g_aboot_cb_ctx);
}

void aboot_notify_status(const char *status, int error)
{
	aboot_message_t m;

	if (!g_aboot_cb) {
		printf("[aboot] status=%s err=%d\n", status ? status : "", error);
		return;
	}
	memset(&m, 0, sizeof(m));
	m.event = ABOOT_EVT_STATUS;
	m.error = error;
	m.u.status = status;
	g_aboot_cb(&m, g_aboot_cb_ctx);
}

void aboot_notify_progress(int percent)
{
	aboot_message_t m;

	if (!g_aboot_cb) {
		printf("[aboot] progress=%d\n", percent);
		return;
	}
	memset(&m, 0, sizeof(m));
	m.event = ABOOT_EVT_PROGRESS;
	m.u.progress = percent;
	g_aboot_cb(&m, g_aboot_cb_ctx);
}

int aboot_core_init(const aboot_io_t *io, aboot_callback_t cb, void *cb_ctx)
{
	if (!io || !io->open || !io->close || !io->write || !io->read) {
		return -1;
	}
	g_aboot_io = io;
	g_aboot_cb = cb;
	g_aboot_cb_ctx = cb_ctx;
	aboot_notify_log("core init ok");
	return 0;
}

void aboot_core_deinit(void)
{
	aboot_core_disconnect();
	g_aboot_io = NULL;
	g_aboot_cb = NULL;
	g_aboot_cb_ctx = NULL;
}

int aboot_core_connect(const char *dev_path)
{
	int ret;

	if (!g_aboot_io || !dev_path) {
		return -1;
	}

	aboot_notify_status("CONNECTING", 0);
	ret = g_aboot_io->open(dev_path);
	if (ret < 0) {
		aboot_notify_status("FAILED", ret);
		return ret;
	}

	/* SMUX path: framed UABT + HELLO (aboot-tiny smux_connect_process) */
	ret = aboot_smux_init();
	if (ret < 0) {
		g_aboot_io->close();
		aboot_notify_status("FAILED", ret);
		return ret;
	}

	ret = aboot_smux_handshake(15000);
	if (ret < 0) {
		aboot_smux_exit();
		g_aboot_io->close();
		return ret;
	}

	ret = aboot_transport_init();
	if (ret < 0) {
		aboot_smux_exit();
		g_aboot_io->close();
		aboot_notify_status("FAILED", ret);
		return ret;
	}

	aboot_notify_log("connect ok (smux RUNNING)");
	return 0;
}

int aboot_core_download_file(const char *img_path, int reboot)
{
	if (!g_aboot_io || !img_path) {
		return -1;
	}
	if (!aboot_smux_is_running()) {
		aboot_notify_log("download: smux not RUNNING, call connect first");
		return -1;
	}
	return aboot_download_file(img_path, reboot);
}

int aboot_core_at(const char *dev_path, const char *cmd, int timeout_ms)
{
	char tx[256];
	char rx[128];
	char acc[512];
	size_t tx_len;
	int ret;
	int elapsed = 0;
	size_t acc_len = 0;
	int got_reply = 0;

	if (!g_aboot_io || !dev_path || !cmd || !cmd[0]) {
		return -1;
	}
	if (timeout_ms <= 0) {
		timeout_ms = 3000;
	}

	/* AT path: raw serial only, no SMUX */
	aboot_notify_status("AT", 0);
	ret = g_aboot_io->open(dev_path);
	if (ret < 0) {
		aboot_notify_status("FAILED", ret);
		return ret;
	}

	tx_len = aboot_unescape(cmd, tx, sizeof(tx));
	if (tx_len == 0) {
		g_aboot_io->close();
		aboot_notify_log("at: empty command");
		return -1;
	}
	/* auto append CR if user omitted line ending */
	if (tx[tx_len - 1] != '\r' && tx[tx_len - 1] != '\n') {
		if (tx_len + 2 < sizeof(tx)) {
			tx[tx_len++] = '\r';
			tx[tx_len++] = '\n';
			tx[tx_len] = '\0';
		} else if (tx_len + 1 < sizeof(tx)) {
			tx[tx_len++] = '\r';
			tx[tx_len] = '\0';
		}
	}

	printf("[aboot] AT TX (%u): ", (unsigned)tx_len);
	{
		size_t i;
		for (i = 0; i < tx_len; i++) {
			unsigned char c = (unsigned char)tx[i];
			if (c == '\r') {
				printf("\\r");
			} else if (c == '\n') {
				printf("\\n");
			} else if (c >= 0x20 && c < 0x7f) {
				putchar(c);
			} else {
				printf("\\x%02x", c);
			}
		}
		printf("\n");
	}

	ret = g_aboot_io->write((const uint8_t *)tx, tx_len);
	if (ret < 0) {
		g_aboot_io->close();
		aboot_notify_status("FAILED", ret);
		return ret;
	}

	acc[0] = '\0';
	while (elapsed < timeout_ms) {
		ret = g_aboot_io->read((uint8_t *)rx, sizeof(rx) - 1, 100);
		if (ret > 0) {
			size_t copy;

			rx[ret] = '\0';
			printf("%.*s", ret, rx);
			got_reply = 1;
			copy = (size_t)ret;
			if (acc_len + copy >= sizeof(acc)) {
				copy = sizeof(acc) - 1 - acc_len;
			}
			if (copy > 0) {
				memcpy(acc + acc_len, rx, copy);
				acc_len += copy;
				acc[acc_len] = '\0';
			}
			if (aboot_at_done(acc)) {
				break;
			}
		}
		rt_thread_mdelay(20);
		elapsed += 20;
	}
	if (got_reply) {
		printf("\n");
	} else {
		printf("[aboot] AT: no reply (timeout %d ms)\n", timeout_ms);
	}

	g_aboot_io->close();
	aboot_notify_status(got_reply ? "AT_OK" : "AT_TIMEOUT", got_reply ? 0 : -1);
	return got_reply ? 0 : -1;
}

int aboot_core_disconnect(void)
{
	aboot_transport_exit();
	aboot_smux_exit();
	if (g_aboot_io) {
		g_aboot_io->close();
	}
	return 0;
}
