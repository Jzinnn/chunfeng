#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "aboot_core.h"

#if defined(CONFIG_CHERRYUSB_HOST_GSM) || defined(CONFIG_CHERRYUSB_HOST_CDC_ACM)
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
	termios.rx_timeout = 500;
	usbh_serial_control(s_serial, USBH_SERIAL_CMD_SET_ATTR, &termios);

	strncpy(s_opened_dev, dev_path, sizeof(s_opened_dev) - 1);
	s_opened_dev[sizeof(s_opened_dev) - 1] = '\0';
	printf("[aboot-port] open %s ok\n", s_opened_dev);
	return 0;
}

/* Parse ".../ttyUSB3" or ".../ttyACM1" → kind + index; return 0 if matched */
static int parse_tty_name(const char *path, int *is_usb, int *idx)
{
	const char *p;

	if (!path) {
		return -1;
	}
	p = strstr(path, "ttyUSB");
	if (p) {
		*is_usb = 1;
		*idx = atoi(p + 6);
		return 0;
	}
	p = strstr(path, "ttyACM");
	if (p) {
		*is_usb = 0;
		*idx = atoi(p + 6);
		return 0;
	}
	return -1;
}

static int port_try_pair(int prefer_usb, int idx)
{
	char path[64];
	int order_usb[2];
	int i;

	/* prefer requested family first, then the other */
	order_usb[0] = prefer_usb ? 1 : 0;
	order_usb[1] = prefer_usb ? 0 : 1;

	for (i = 0; i < 2; i++) {
		if (order_usb[i]) {
			snprintf(path, sizeof(path), "/dev/ttyUSB%d", idx);
		} else {
			snprintf(path, sizeof(path), "/dev/ttyACM%d", idx);
		}
		printf("[aboot-port] try %s\n", path);
		if (port_open_one(path) == 0) {
			return 0;
		}
	}
	return -1;
}

static int port_open_auto(void)
{
	int idx;

	/* each index: prefer ttyUSB, then ttyACM */
	for (idx = 0; idx < ABOOT_SERIAL_PROBE_MAX; idx++) {
		if (port_try_pair(1, idx) == 0) {
			return 0;
		}
	}
	printf("[aboot-port] auto: no ttyUSB*/ttyACM* opened\n");
	return -1;
}

static int port_open(const char *dev_path)
{
	int is_usb = 1;
	int idx = 0;

	if (s_serial) {
		usbh_serial_close(s_serial);
		s_serial = NULL;
	}
	s_opened_dev[0] = '\0';

	/* auto / empty / "default" → probe both families */
	if (!dev_path || !dev_path[0] ||
	    !strcmp(dev_path, "auto") || !strcmp(dev_path, "default")) {
		return port_open_auto();
	}

	/* explicit path: try it, then sibling ttyUSB↔ttyACM with same index */
	if (port_open_one(dev_path) == 0) {
		return 0;
	}
	printf("[aboot-port] open %s fail, try alternate name\n", dev_path);

	if (parse_tty_name(dev_path, &is_usb, &idx) == 0) {
		return port_try_pair(is_usb, idx);
	}

	/* unknown name: fall back to auto probe */
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
	int ret;

	if (!s_serial || !buf || !len) {
		return -1;
	}
	ret = usbh_serial_write(s_serial, buf, (uint32_t)len);
	return ret;
}

static int port_read(uint8_t *buf, size_t len, int timeout_ms)
{
	int ret;

	if (!s_serial || !buf || !len) {
		return -1;
	}
	(void)timeout_ms;
	ret = usbh_serial_read(s_serial, buf, (uint32_t)len);
	return ret;
}

#else /* no USB host serial in this build */

static int port_open(const char *dev_path)
{
	printf("[aboot-port] USBH serial not enabled, cannot open %s\n",
	       dev_path ? dev_path : "");
	printf("[aboot-port] enable CONFIG_CHERRYUSB_HOST_GSM and/or CDC_ACM\n");
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
