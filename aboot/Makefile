obj-y += aboot_core.o
obj-y += aboot_preamble.o
obj-y += aboot_smux.o
obj-y += aboot_transport.o
obj-y += aboot_download.o
obj-y += port_usbh_serial.o


# CherryUSB headers for port_usbh_serial.c (usbh_serial.h → usbh_core.h / usb_cdc.h)
# srctree = melis/source → ../../rtos-components = lichee/rtos-components
CHERRYUSB_DIR := $(srctree)/../../rtos-components/thirdparty/cherry_usb
ccflags-y += -I$(src)
ccflags-y += -I$(CHERRYUSB_DIR)
ccflags-y += -I$(CHERRYUSB_DIR)/core
ccflags-y += -I$(CHERRYUSB_DIR)/common
ccflags-y += -I$(CHERRYUSB_DIR)/class/serial
ccflags-y += -I$(CHERRYUSB_DIR)/class/cdc
ccflags-y += -I$(CHERRYUSB_DIR)/class/hub
ccflags-y += -I$(CHERRYUSB_DIR)/osal
ccflags-y += -I$(CHERRYUSB_DIR)/platform
ccflags-y += -I$(CHERRYUSB_DIR)/platform/allwinner
ccflags-y += -I$(CHERRYUSB_DIR)/platform/allwinner/rtthread
