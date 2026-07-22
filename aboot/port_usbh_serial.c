#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "aboot_core.h"

#if defined(CONFIG_CHERRYUSB_HOST_GSM) || defined(CONFIG_CHERRYUSB_HOST_CDC_ACM)
#include "usbh_serial.h"
#define ABOOT_HAVE_USBH_SERIAL 1
#endif

#ifdef ABOOT_HAVE_USBH_SERIAL
static struct usbh_serial *s_serial;

static int port_open(const char *dev_path)
{
	struct usbh_serial_termios termios;

	if (!dev_path) {
		return -1;
	}
	if (s_serial) {
		usbh_serial_close(s_serial);
		s_serial = NULL;
	}

	s_serial = usbh_serial_open(dev_path, USBH_SERIAL_O_RDWR);
	if (!s_serial) {
		printf("[aboot-port] open %s fail\n", dev_path);
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

	printf("[aboot-port] open %s ok\n", dev_path);
	return 0;
}

static void port_close(void)
{
	if (s_serial) {
		usbh_serial_close(s_serial);
		s_serial = NULL;
	}
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
	(void)timeout_ms; /* termios.rx_timeout already set; refine later */
	ret = usbh_serial_read(s_serial, buf, (uint32_t)len);
	return ret;
}

#else /* no USB host serial in this build */

static int port_open(const char *dev_path)
{
	printf("[aboot-port] USBH serial not enabled, cannot open %s\n",
	       dev_path ? dev_path : "");
	printf("[aboot-port] enable CONFIG_CHERRYUSB_HOST_GSM (or CDC_ACM)\n");
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
