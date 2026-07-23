#include <string.h>

#include "aboot_api.h"

static const char *aboot_api_dev_or_auto(const char *dev)
{
	if (!dev || !dev[0] || !strcmp(dev, "auto")) {
		return "auto";
	}
	return dev;
}

int aboot_api_init(const aboot_io_t *io, const aboot_cbs_t *cbs)
{
	if (!io) {
		io = aboot_port_usbh_serial();
	}
	return aboot_core_init_ex(io, NULL, NULL, cbs);
}

void aboot_api_deinit(void)
{
	aboot_core_deinit();
}

void aboot_api_set_callbacks(const aboot_cbs_t *cbs)
{
	aboot_set_callbacks(cbs);
}

void aboot_api_set_verbose(int on)
{
	aboot_set_verbose(on);
}

int aboot_api_connect(const char *dev)
{
	int ret;

	dev = aboot_api_dev_or_auto(dev);
	ret = aboot_core_connect(dev);
	aboot_core_disconnect();
	return ret;
}

int aboot_api_download(const char *img_path, const char *dev, int verbose)
{
	int ret;

	if (!img_path || !img_path[0]) {
		return -1;
	}

	aboot_set_verbose(verbose);
	dev = aboot_api_dev_or_auto(dev);

	ret = aboot_core_connect(dev);
	if (ret < 0) {
		return ret;
	}

	ret = aboot_core_download_file(img_path, 1);
	aboot_core_disconnect();
	return ret;
}

int aboot_api_at(const char *dev, const char *cmd, int timeout_ms)
{
	if (!dev || !dev[0] || !cmd || !cmd[0]) {
		return -1;
	}
	return aboot_core_at(dev, cmd, timeout_ms);
}
