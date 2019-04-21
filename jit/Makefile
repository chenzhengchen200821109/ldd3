obj-m := hello.o

PWD := $(shell pwd)
KVERSION := $(shell uname -r)
KDIRECTORY := /lib/modules/$(KVERSION)/build

module:
	$(MAKE) -C $(KDIRECTORY) M=$(PWD)

clean:
	rm *.o *.ko *.mod.c modules.order Module.symvers
