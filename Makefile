obj-y += main.o
obj-y += cmd_aboot.o
obj-y += aboot/

ccflags-y += -I$(src)/aboot
