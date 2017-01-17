ifneq ($(KERNELRELEASE),)
	obj-m := thruput.o
	thruput-objs := thruput_test_core.o thruput_test_common.o thruput_test_arch_igb.o
else
	KERNELDIR ?= /usr/src/linux-headers-4.4.17/
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod.c *mod.o *.order *.symvers
endif