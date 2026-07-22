/*
 * SMUX framing ported from aboot-tiny os/dev/smux.c (no Contiki).
 * TX/RX go through g_aboot_io (Melis usbh_serial / ttyUSB).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "aboot_internal.h"
#include "aboot_smux.h"

#ifdef CONFIG_RTTKERNEL
#include <rtthread.h>
#endif

#define SMUX_LINK_CHECK_COUNT 30 /* ~3s if poll every 100ms */

typedef enum {
	WAIT_FRAMESTART = 0,
	IN_FRAME,
	IN_ESCAPE
} line_state_t;

typedef enum {
	STAGE_INIT = 0,
	STAGE_HELLO,
	STAGE_RUNNING,
	STAGE_FAILED,
	STAGE_SUCCEEDED
} running_stage_t;

typedef enum {
	SMUX_FRAME_TYPE_STDIO = 0x00,
	SMUX_FRAME_TYPE_HELLO = 0x01,
	SMUX_FRAME_TYPE_HELLO_REPLY = 0x02,
	SMUX_FRAME_TYPE_ABOOT_CMD = 0x03,
	SMUX_FRAME_TYPE_ABOOT_DATA = 0x04,
	SMUX_FRAME_TYPE_HEART_BEAT = 0x05,
	SMUX_ESC_CHAR = 0x7D,
	SMUX_FRAME_DELIMITER = 0x7E
} smux_frame_type_t;

typedef struct {
	running_stage_t stage;
	uint8_t rxbuf[ABOOT_SMUX_FRAME_MTU];
	aboot_smux_rx_cb_t cmd_cb;
	aboot_smux_rx_cb_t data_cb;
	line_state_t state;
	size_t framesize;
	smux_frame_type_t frametype;
	uint16_t mtu;
	uint8_t *txbuf;
	size_t txbuf_size;
	size_t txbuf_used;
	size_t total_tx_size;
	int timeout_count;
} smux_t;

static smux_t *s_smux;

static void smux_process_incoming_data(const uint8_t *data, size_t len);

static void smux_delay_ms(int ms)
{
#ifdef CONFIG_RTTKERNEL
	rt_thread_mdelay(ms);
#else
	/* crude fallback */
	volatile int i;
	while (ms-- > 0) {
		for (i = 0; i < 10000; i++) {
		}
	}
#endif
}

static void smux_set_tx_size(size_t size)
{
	if (!s_smux) {
		return;
	}
	s_smux->total_tx_size = size;
	s_smux->txbuf_used = 0;
}

/* Drain host RX while TX runs — serial ringbuffer is only ~2KB. */
static void smux_drain_rx(void)
{
	uint8_t buf[256];
	int n;
	int guard = 64;

	if (!s_smux || !g_aboot_io) {
		return;
	}
	while (guard-- > 0) {
		n = g_aboot_io->read(buf, sizeof(buf), 0);
		if (n <= 0) {
			break;
		}
		smux_process_incoming_data(buf, (size_t)n);
	}
}

static void send_txbuf(void)
{
	size_t off = 0;
	const size_t chunk = 512;

	if (!s_smux || !s_smux->txbuf || !g_aboot_io) {
		return;
	}
	while (off < s_smux->txbuf_used) {
		size_t n = s_smux->txbuf_used - off;
		int wr;

		if (n > chunk) {
			n = chunk;
		}
		wr = g_aboot_io->write(s_smux->txbuf + off, n);
		if (wr <= 0) {
			aboot_notify_log("smux: usb write fail");
			break;
		}
		off += (size_t)wr;
		smux_drain_rx();
	}
	free(s_smux->txbuf);
	s_smux->txbuf = NULL;
	s_smux->txbuf_used = 0;
}

static int alloc_txbuf(void)
{
	if (!s_smux) {
		return -1;
	}
	s_smux->txbuf = (uint8_t *)malloc(s_smux->txbuf_size);
	if (!s_smux->txbuf) {
		aboot_notify_log("smux: txbuf malloc fail");
		return -1;
	}
	s_smux->txbuf_used = 0;
	return 0;
}

static void smux_send_frame(const uint8_t *data, size_t size, smux_frame_type_t frame_type)
{
	smux_t *smux = s_smux;
	size_t used;

	if (!smux || !data) {
		return;
	}

	smux->total_tx_size -= size;
	used = smux->txbuf_used;

	while (size > 0) {
		if (!smux->txbuf) {
			if (alloc_txbuf() < 0) {
				return;
			}
			used = 0;
		}
		/* need room for delimiter + esc type + delimiter */
		if (used + 5 > smux->txbuf_size) {
			smux->txbuf_used = used;
			send_txbuf();
			used = 0;
			continue;
		}
		{
			uint8_t *p = smux->txbuf + used;
			uint16_t mtu;

			*p++ = SMUX_FRAME_DELIMITER;
			used++;
			if (frame_type) {
				*p++ = SMUX_ESC_CHAR;
				*p++ = (uint8_t)(frame_type ^ 0x20);
				used += 2;
			}
			mtu = smux->mtu;
			while (size && used + 3 < smux->txbuf_size && mtu--) {
				uint8_t c = *data++;

				switch (c) {
				case SMUX_FRAME_DELIMITER:
					*p++ = SMUX_ESC_CHAR;
					*p++ = (uint8_t)(SMUX_FRAME_DELIMITER ^ 0x20);
					used += 2;
					break;
				case SMUX_ESC_CHAR:
					*p++ = SMUX_ESC_CHAR;
					*p++ = (uint8_t)(SMUX_ESC_CHAR ^ 0x20);
					used += 2;
					break;
				default:
					*p++ = c;
					used++;
					break;
				}
				size--;
			}
			*p++ = SMUX_FRAME_DELIMITER;
			used++;
			/*
			 * Melis USB host: flush every SMUX frame (PC aboot-tiny batches).
			 * Large unflushed TX starves RX and overflows usbh_serial ringbuffer
			 * → desync → "lost frame".
			 */
			if (!size && smux->total_tx_size == 0 && (used % 512 == 0)) {
				*p++ = SMUX_FRAME_DELIMITER;
				used++;
			}
			smux->txbuf_used = used;
			send_txbuf();
			used = 0;
		}
	}
	smux->txbuf_used = used;
}

void aboot_smux_set_aboot_data_size(size_t size)
{
	smux_set_tx_size(size);
}

void aboot_smux_write_aboot_data(const uint8_t *data, size_t len)
{
	smux_send_frame(data, len, SMUX_FRAME_TYPE_ABOOT_DATA);
}

void aboot_smux_write_aboot_cmd(const uint8_t *data, size_t len)
{
	smux_set_tx_size(len);
	smux_send_frame(data, len, SMUX_FRAME_TYPE_ABOOT_CMD);
}

static void _reset_state(smux_t *smux)
{
	smux->state = WAIT_FRAMESTART;
	smux->frametype = 0;
	smux->framesize = 0;
}

static void _handle_char(smux_t *smux, char c)
{
	size_t limit;

	switch (smux->frametype) {
	case SMUX_FRAME_TYPE_STDIO:
	case SMUX_FRAME_TYPE_HELLO:
	case SMUX_FRAME_TYPE_HELLO_REPLY:
	case SMUX_FRAME_TYPE_ABOOT_CMD:
	case SMUX_FRAME_TYPE_ABOOT_DATA:
	case SMUX_FRAME_TYPE_HEART_BEAT:
		limit = smux->mtu;
		if (limit == 0 || limit > sizeof(smux->rxbuf)) {
			limit = sizeof(smux->rxbuf);
		}
		if (smux->framesize < limit) {
			smux->rxbuf[smux->framesize++] = (uint8_t)c;
		} else {
			aboot_notify_log("smux: lost frame (mtu)");
			_reset_state(smux);
		}
		break;
	default:
		break;
	}
}

static void _end_of_frame(smux_t *smux)
{
	switch (smux->frametype) {
	case SMUX_FRAME_TYPE_STDIO:
		if (smux->stage == STAGE_INIT) {
			if (smux->framesize == ABOOT_SMUX_PREAMBLE_SIZE &&
			    !strncmp((const char *)smux->rxbuf, ABOOT_SMUX_PREAMBLE_UABT,
				     ABOOT_SMUX_PREAMBLE_SIZE)) {
				smux->stage = STAGE_HELLO;
				aboot_notify_log("smux: got UABT, enter HELLO");
				break;
			}
		}
		smux->rxbuf[smux->framesize < ABOOT_SMUX_FRAME_MTU ? smux->framesize : ABOOT_SMUX_FRAME_MTU - 1] = '\0';
		if (aboot_device_log_is_quiet()) {
			aboot_device_log_drop();
		} else {
			aboot_notify_log((const char *)smux->rxbuf);
		}
		break;

	case SMUX_FRAME_TYPE_HELLO_REPLY:
		if (smux->stage == STAGE_HELLO) {
			if (smux->framesize == sizeof(smux->mtu)) {
				char msg[64];

				smux->mtu = ((uint16_t)smux->rxbuf[0] << 8) | smux->rxbuf[1];
				if (smux->mtu == 0 || smux->mtu > ABOOT_SMUX_FRAME_MTU) {
					smux->mtu = ABOOT_SMUX_FRAME_MTU;
				}
				smux->stage = STAGE_RUNNING;
				aboot_notify_status("RUNNING", 0);
				snprintf(msg, sizeof(msg), "smux: HELLO_REPLY ok, RUNNING mtu=%u",
					 (unsigned)smux->mtu);
				aboot_notify_log(msg);
			}
		} else {
			aboot_notify_log("smux: unexpected HELLO_REPLY");
		}
		break;

	case SMUX_FRAME_TYPE_ABOOT_CMD:
		if (smux->cmd_cb) {
			smux->cmd_cb(smux->rxbuf, smux->framesize);
		}
		break;

	case SMUX_FRAME_TYPE_HEART_BEAT:
		break;

	case SMUX_FRAME_TYPE_ABOOT_DATA:
		if (smux->data_cb) {
			smux->data_cb(smux->rxbuf, smux->framesize);
		}
		break;

	default:
		break;
	}

	smux->timeout_count = 0;
	_reset_state(smux);
}

static void smux_process_incoming_data(const uint8_t *data, size_t len)
{
	smux_t *smux = s_smux;
	size_t i;

	if (!smux || !data) {
		return;
	}

	for (i = 0; i < len; i++) {
		uint8_t c = data[i];

		switch (smux->state) {
		case WAIT_FRAMESTART:
			if (c == SMUX_FRAME_DELIMITER) {
				_reset_state(smux);
				smux->state = IN_FRAME;
			}
			break;

		case IN_FRAME:
			if (c == SMUX_ESC_CHAR) {
				smux->state = IN_ESCAPE;
			} else if (c == SMUX_FRAME_DELIMITER) {
				if (smux->framesize) {
					_end_of_frame(smux);
				}
			} else {
				_handle_char(smux, (char)c);
			}
			break;

		case IN_ESCAPE:
			switch (c) {
			case (SMUX_FRAME_DELIMITER ^ 0x20):
				_handle_char(smux, SMUX_FRAME_DELIMITER);
				break;
			case (SMUX_ESC_CHAR ^ 0x20):
				_handle_char(smux, SMUX_ESC_CHAR);
				break;
			case (SMUX_FRAME_TYPE_STDIO ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_STDIO;
				break;
			case (SMUX_FRAME_TYPE_HELLO ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_HELLO;
				break;
			case (SMUX_FRAME_TYPE_HELLO_REPLY ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_HELLO_REPLY;
				break;
			case (SMUX_FRAME_TYPE_ABOOT_CMD ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_ABOOT_CMD;
				break;
			case (SMUX_FRAME_TYPE_ABOOT_DATA ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_ABOOT_DATA;
				break;
			case (SMUX_FRAME_TYPE_HEART_BEAT ^ 0x20):
				smux->frametype = SMUX_FRAME_TYPE_HEART_BEAT;
				break;
			default:
				break;
			}
			smux->state = IN_FRAME;
			break;
		}
	}
}

static void smux_tick_handshake(void)
{
	smux_t *smux = s_smux;

	if (!smux) {
		return;
	}

	if (smux->stage == STAGE_INIT) {
		smux_set_tx_size(ABOOT_SMUX_PREAMBLE_SIZE);
		smux_send_frame((const uint8_t *)ABOOT_SMUX_PREAMBLE_UABT,
				ABOOT_SMUX_PREAMBLE_SIZE, SMUX_FRAME_TYPE_STDIO);
	} else if (smux->stage == STAGE_HELLO) {
		uint16_t mtu_be = (uint16_t)((smux->mtu << 8) | (smux->mtu >> 8));

		smux_set_tx_size(2);
		smux_send_frame((uint8_t *)&mtu_be, 2, SMUX_FRAME_TYPE_HELLO);
	} else if (smux->stage == STAGE_RUNNING) {
		/* link alive counter reset on any frame */
	} else {
		if (++smux->timeout_count > SMUX_LINK_CHECK_COUNT) {
			smux->stage = STAGE_FAILED;
			aboot_notify_status("FAILED", -1);
			aboot_notify_log("smux: link lost");
		}
	}
}

int aboot_smux_poll(int timeout_ms)
{
	uint8_t buf[256];
	int n;

	if (!s_smux || !g_aboot_io) {
		return -1;
	}

	n = g_aboot_io->read(buf, sizeof(buf), timeout_ms);
	if (n > 0) {
		smux_process_incoming_data(buf, (size_t)n);
	}
	return n;
}

int aboot_smux_is_running(void)
{
	return s_smux && s_smux->stage == STAGE_RUNNING;
}

void aboot_smux_register_cmd_callback(aboot_smux_rx_cb_t cb)
{
	if (s_smux) {
		s_smux->cmd_cb = cb;
	}
}

void aboot_smux_register_data_callback(aboot_smux_rx_cb_t cb)
{
	if (s_smux) {
		s_smux->data_cb = cb;
	}
}

int aboot_smux_init(void)
{
	smux_t *smux;

	if (s_smux) {
		aboot_smux_exit();
	}

	smux = (smux_t *)malloc(sizeof(smux_t));
	if (!smux) {
		aboot_notify_log("smux: malloc fail");
		return -1;
	}
	memset(smux, 0, sizeof(*smux));
	smux->stage = STAGE_INIT;
	smux->state = WAIT_FRAMESTART;
	smux->txbuf_size = ABOOT_SMUX_TXBUF_SIZE;
	smux->frametype = SMUX_FRAME_TYPE_ABOOT_CMD;
	smux->mtu = ABOOT_SMUX_FRAME_MTU;
	s_smux = smux;

	aboot_notify_log("smux: init ok");
	return 0;
}

void aboot_smux_exit(void)
{
	if (!s_smux) {
		return;
	}
	if (s_smux->txbuf) {
		free(s_smux->txbuf);
		s_smux->txbuf = NULL;
	}
	free(s_smux);
	s_smux = NULL;
	aboot_notify_log("smux: exit");
}

int aboot_smux_handshake(int timeout_ms)
{
	int elapsed = 0;
	const int slice = 100;

	if (!s_smux || !g_aboot_io) {
		return -1;
	}

	aboot_notify_status("CONNECTING", 0);
	aboot_notify_log("smux: handshake start");

	while (elapsed < timeout_ms) {
		if (s_smux->stage == STAGE_RUNNING) {
			return 0;
		}
		if (s_smux->stage == STAGE_FAILED) {
			return -1;
		}

		/* every 100ms: send INIT/HELLO tick + poll RX */
		smux_tick_handshake();
		aboot_smux_poll(slice);
		smux_delay_ms(slice);
		elapsed += slice;
	}

	aboot_notify_log("smux: handshake timeout");
	aboot_notify_status("FAILED", -1);
	s_smux->stage = STAGE_FAILED;
	return -1;
}
