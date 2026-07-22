#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "aboot_core.h"

#if defined(CONFIG_CHERRYUSB_HOST_CDC_ACM) || defined(CONFIG_CHERRYUSB_HOST_GSM)
#include "usbh_serial.h"
#define ABOOT_HAVE_USBH_SERIAL 1
#endif

#ifdef ABOOT_HAVE_USBH_SERIAL

#ifndef ABOOT_SERIAL_PROBE_MAX
#define ABOOT_SERIAL_PROBE_MAX 4
#endif

static struct usbh_serial *s_serial;
static char s_opened_dev[64];

static int port_open_one(const char *dev_path)
{
	struct usbh_serial_termios termios;

	if (!dev_path || !dev_path[0]) {
		return -1;
	}

	s_serial = usbh_serial_open(dev_path, USBH_SERIAL_O_RDWR);
	if (!s_serial) {
		return -1;
	}

	memset(&termios, 0, sizeof(termios));
	termios.baudrate = 115200;
	termios.databits = USBH_SERIAL_DATABITS_8;
	termios.parity = USBH_SERIAL_PARITY_NONE;
	termios.stopbits = USBH_SERIAL_STOPBITS_1;
	termios.rtscts = false;
	termios.rx_timeout = 50;
	usbh_serial_control(s_serial, USBH_SERIAL_CMD_SET_ATTR, &termios);

	strncpy(s_opened_dev, dev_path, sizeof(s_opened_dev) - 1);
	s_opened_dev[sizeof(s_opened_dev) - 1] = '\0';
	printf("[aboot-port] open %s ok\n", s_opened_dev);
	return 0;
}

static int port_open_auto(void)
{
	char path[64];
	int idx;

	for (idx = 0; idx < ABOOT_SERIAL_PROBE_MAX; idx++) {
		snprintf(path, sizeof(path), "/dev/ttyACM%d", idx);
		printf("[aboot-port] try %s\n", path);
		if (port_open_one(path) == 0) {
			return 0;
		}
	}
	printf("[aboot-port] auto: no ttyACM* opened\n");
	return -1;
}

static int port_open(const char *dev_path)
{
	if (s_serial) {
		usbh_serial_close(s_serial);
		s_serial = NULL;
	}
	s_opened_dev[0] = '\0';

	if (!dev_path || !dev_path[0] ||
	    !strcmp(dev_path, "auto") || !strcmp(dev_path, "default")) {
		return port_open_auto();
	}

	/* only accept ttyACM* paths (or auto above) */
	if (!strstr(dev_path, "ttyACM")) {
		printf("[aboot-port] only ttyACM supported, got %s — try auto\n",
		       dev_path);
		return port_open_auto();
	}

	if (port_open_one(dev_path) == 0) {
		return 0;
	}
	printf("[aboot-port] open %s fail\n", dev_path);
	return port_open_auto();
}

static void port_close(void)
{
	if (s_serial) {
		usbh_serial_close(s_serial);
		s_serial = NULL;
	}
	s_opened_dev[0] = '\0';
}

static int port_write(const uint8_t *buf, size_t len)
{
	if (!s_serial || !buf || !len) {
		return -1;
	}
	return usbh_serial_write(s_serial, buf, (uint32_t)len);
}

static int port_read(uint8_t *buf, size_t len, int timeout_ms)
{
	int n;

	if (!s_serial || !buf || !len) {
		return -1;
	}

	/* timeout_ms==0: non-blocking drain (needed while TX) */
	if (timeout_ms == 0) {
		uint32_t flags = s_serial->open_flags;
		s_serial->open_flags |= USBH_SERIAL_O_NONBLOCK;
		n = usbh_serial_read(s_serial, buf, (uint32_t)len);
		s_serial->open_flags = flags;
		if (n < 0) {
			return 0;
		}
		return n;
	}

	(void)timeout_ms;
	return usbh_serial_read(s_serial, buf, (uint32_t)len);
}

#else /* no USB host serial */

static int port_open(const char *dev_path)
{
	printf("[aboot-port] USBH serial not enabled, cannot open %s\n",
	       dev_path ? dev_path : "");
	printf("[aboot-port] enable CONFIG_CHERRYUSB_HOST_CDC_ACM\n");
	return -1;
}

static void port_close(void)
{
}

static int port_write(const uint8_t *buf, size_t len)
{
	(void)buf;
	(void)len;
	return -1;
}

static int port_read(uint8_t *buf, size_t len, int timeout_ms)
{
	(void)buf;
	(void)len;
	(void)timeout_ms;
	return -1;
}

#endif /* ABOOT_HAVE_USBH_SERIAL */

static const aboot_io_t s_port = {
	.open = port_open,
	.close = port_close,
	.write = port_write,
	.read = port_read,
};

const aboot_io_t *aboot_port_usbh_serial(void)
{
	return &s_port;
}
