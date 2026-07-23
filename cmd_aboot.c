#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <hal_cmd.h>

#include "aboot_api.h"

static void aboot_msh_on_log(const char *message, void *ctx)
{
	(void)ctx;
	aboot_log_printf("%s\n", message ? message : "");
}

static void aboot_msh_on_progress(aboot_progress_src_t src, int percent,
				  void *ctx)
{
	(void)ctx;
	aboot_log_printf("%s progress %d%%\n",
			 src == ABOOT_PROG_DEVICE ? "device" : "script",
			 percent);
}

static void aboot_msh_on_status(const char *status, int error, void *ctx)
{
	(void)ctx;
	aboot_log_printf("status=%s err=%d\n", status ? status : "", error);
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  aboot <image> [dev] [-v]         connect + download ASR aboot image\n");
	printf("  aboot connect [dev]              open + SMUX handshake only\n");
	printf("  aboot at <dev> <cmd> [timeout_ms]  send raw AT (no SMUX)\n");
	printf("  aboot help\n");
	printf("dev: /dev/ttyACM0 | auto (default, probes ttyACM0..3)\n");
	printf("-v: print modem INFO (Psram/Preboot); default muted for speed\n");
	printf("Example:\n");
	printf("  aboot /mnt/D/fw.bin auto\n");
	printf("  aboot /mnt/E/firmware.bin auto -v\n");
	printf("  aboot connect /dev/ttyACM0\n");
	printf("  aboot at /dev/ttyACM0 AT\n");
}

static int cmd_aboot(int argc, char **argv)
{
	const char *dev = "auto";
	const char *img = NULL;
	int ret;
	int i;
	int verbose = 0;
	aboot_cbs_t cbs = {
		.on_status = aboot_msh_on_status,
		.on_progress = aboot_msh_on_progress,
		.on_log = aboot_msh_on_log,
		.ctx = NULL,
	};

	if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h")) {
		usage();
		return 0;
	}

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
			verbose = 1;
		}
	}

	if (aboot_api_init(NULL, &cbs) < 0) {
		printf("aboot_api_init fail\n");
		return -1;
	}

	if (!strcmp(argv[1], "connect")) {
		if (argc >= 3 && argv[2][0] != '-') {
			dev = argv[2];
		}
		ret = aboot_api_connect(dev);
		aboot_api_deinit();
		return ret;
	}

	if (!strcmp(argv[1], "at")) {
		const char *at_cmd;
		int timeout_ms = 3000;

		if (argc < 4) {
			printf("usage: aboot at <dev> <cmd> [timeout_ms]\n");
			aboot_api_deinit();
			return -1;
		}
		dev = argv[2];
		at_cmd = argv[3];
		if (argc >= 5 && argv[4][0] != '-') {
			timeout_ms = atoi(argv[4]);
		}
		ret = aboot_api_at(dev, at_cmd, timeout_ms);
		aboot_api_deinit();
		return ret;
	}

	/* aboot <image> [dev] [-v] */
	img = argv[1];
	if (argc >= 3 && argv[2][0] != '-') {
		dev = argv[2];
	}

	ret = aboot_api_download(img, dev, verbose);
	aboot_api_deinit();
	return ret;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_aboot, __cmd_aboot, aboot download to modem);
