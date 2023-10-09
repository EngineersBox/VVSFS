obj-m += vvsfs.o 

KDIR=/lib/modules/$(shell uname -r)/build 
all: kernel_mod mkfs.vvsfs 

mkfs.vvsfs: mkfs.vvsfs.c
	gcc -Wall -o $@ $<

kernel_mod:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

clean: 
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean
	rm -f mkfs.vvsfs  

load_driver: kernel_mod
	- sudo rmmod vvsfs
	sudo insmod vvsfs.ko
