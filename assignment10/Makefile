obj-m := spinlock_module.o 

spinlock_module-objs := spinlock_module.o ocalclock.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
