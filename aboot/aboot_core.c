#include <stdio.h>
#include <string.h>

#include "aboot_internal.h"

const aboot_io_t *g_aboot_io;
aboot_callback_t g_aboot_cb;
void *g_aboot_cb_ctx;

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

int aboot_core_disconnect(void)
{
	aboot_transport_exit();
	aboot_smux_exit();
	if (g_aboot_io) {
		g_aboot_io->close();
	}
	return 0;
}
