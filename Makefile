OSD_INC=`pwd`/include
LIBOSD=drivers/scsi/osd

# Kbuild part (Embeded in this Makefile)
obj-m := $(LIBOSD)/

# Makefile for out-of-tree builds
KSRC ?= /lib/modules/$(shell uname -r)/build
KBUILD_OUTPUT ?=
ARCH ?=

# this is the basic Kbuild out-of-tree invocation, with the M= option
KBUILD_BASE = +$(MAKE) -C $(KSRC) M=`pwd` KBUILD_OUTPUT=$(KBUILD_OUTPUT) ARCH=$(ARCH)

all: osd_drivers

clean: osd_drivers_clean

osd_drivers:
	$(KBUILD_BASE) OSD_INC=$(OSD_INC) modules

osd_drivers_clean:
	$(KBUILD_BASE) clean
