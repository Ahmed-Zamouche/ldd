module := foo

foo-objs += foo_main.o utils/ringbuffer.o 
obj-m += foo.o

# Comment/uncomment the following line to disable/enable debugging
DEBUG = y
# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	DEBFLAGS = -O -g -DFOO_DEBUG # "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

ccflags-y += -DFOO_MODULE_NAME=\"Foo\" \
-DFOO_DEV_LOOPBACK \
-UFOO_DEV_SINGLE_OPEN \
-DFOO_DEV_EXCLUSIVE_OPEN \
$(DEBFLAGS)

KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(PWD)/build
BUILD_DIR_MAKEFILE ?= $(PWD)/build/Makefile

all: $(BUILD_DIR_MAKEFILE)
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) modules
	
$(BUILD_DIR):
	mkdir -p "$@"
$(BUILD_DIR_MAKEFILE): $(BUILD_DIR)
	touch "$@"

.PHONY: tools
tools:
	mkdir -p build/tools
	gcc -O2 -fomit-frame-pointer -Wall -I$(KDIR) tools/setconsole.c -o  build/tools/setconsole
	#gcc -O2 -fomit-frame-pointer -Wall -I$(KDIR) tools/setlevel.c -o  build/tools/setlevel
	gcc -Wall foo_ioctl.c -o build/foo_ioctl
	gcc -Wall foo_fsync.c -o build/foo_fsync

clean:
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) clean

clang-format:
	clang-format -i foo_main.* utils/ringbuffer.*
load:
	# Clear the kernel log without echo
	sudo dmesg -C
	sudo ./foo_load.sh
unload:
	# Clear the kernel log without echo
	sudo dmesg -C
	-sudo ./foo_unload.sh
