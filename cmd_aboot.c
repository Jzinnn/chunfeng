#include <stdio.h>
#include <string.h>
#include <hal_cmd.h>

#include "aboot_core.h"

static void aboot_msh_cb(const aboot_message_t *msg, void *ctx)
{
	(void)ctx;
	if (!msg) {
		return;
	}
	switch (msg->event) {
	case ABOOT_EVT_LOG:
		printf("[aboot] %s\n", msg->u.message ? msg->u.message : "");
		break;
	case ABOOT_EVT_PROGRESS:
		printf("[aboot] progress %d%%\n", msg->u.progress);
		break;
	case ABOOT_EVT_STATUS:
		printf("[aboot] status=%s err=%d\n",
		       msg->u.status ? msg->u.status : "", msg->error);
		break;
	default:
		break;
	}
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  aboot <image> [dev]     connect + download ASR aboot image\n");
	printf("  aboot connect [dev]     open + SMUX handshake only\n");
	printf("  aboot help\n");
	printf("Default dev: /dev/ttyUSB0\n");
	printf("Example:\n");
	printf("  aboot /mnt/D/fw.bin /dev/ttyUSB0\n");
}

static int cmd_aboot(int argc, char **argv)
{
	const char *dev = "/dev/ttyUSB0";
	const char *img = NULL;
	int ret;

	if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h")) {
		usage();
		return 0;
	}

	if (aboot_core_init(aboot_port_usbh_serial(), aboot_msh_cb, NULL) < 0) {
		printf("aboot_core_init fail\n");
		return -1;
	}

	if (!strcmp(argv[1], "connect")) {
		if (argc >= 3) {
			dev = argv[2];
		}
		ret = aboot_core_connect(dev);
		aboot_core_disconnect();
		aboot_core_deinit();
		return ret;
	}

	/* aboot <image> [dev] */
	img = argv[1];
	if (argc >= 3) {
		dev = argv[2];
	}

	ret = aboot_core_connect(dev);
	if (ret < 0) {
		aboot_core_deinit();
		return ret;
	}

	ret = aboot_core_download_file(img, 1);
	aboot_core_disconnect();
	aboot_core_deinit();
	return ret;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_aboot, __cmd_aboot, aboot download to modem);
