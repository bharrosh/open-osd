OSD_INC=`pwd`/include
LIBOSD=drivers/scsi/osd
EXOFS=fs/exofs

# Kbuild part (Embeded in this Makefile)
obj-m := $(LIBOSD)/ $(EXOFS)/

# Makefile for out-of-tree builds
KSRC ?= /lib/modules/$(shell uname -r)/build
KBUILD_OUTPUT ?=
ARCH ?=

# this is the basic Kbuild out-of-tree invocation, with the M= option
KBUILD_BASE = +$(MAKE) -C $(KSRC) M=`pwd` KBUILD_OUTPUT=$(KBUILD_OUTPUT) ARCH=$(ARCH)

all: osd_drivers osd_lib

clean: osd_drivers_clean osd_lib_clean

osd_drivers:
	$(KBUILD_BASE) OSD_INC=$(OSD_INC) modules

osd_drivers_clean:
	$(KBUILD_BASE) clean

osd_lib: ;
	$(MAKE) -C lib/

osd_lib_clean: ;
	$(MAKE) -C lib/ clean
