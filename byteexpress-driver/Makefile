# SPDX-License-Identifier: GPL-2.0

ccflags-y				+= -I$(src)

#obj-m                                   += custom_nvme.o
#custom_nvme-y                           += core.o ioctl.o sysfs.o pr.o trace.o multipath.o zns.o hwmon.o pci.o

obj-$(CONFIG_NVME_CORE)			+= nvme-core.o
obj-$(CONFIG_BLK_DEV_NVME)		+= custom_nvme.o
obj-$(CONFIG_NVME_FABRICS)		+= nvme-fabrics.o
obj-$(CONFIG_NVME_RDMA)			+= nvme-rdma.o
obj-$(CONFIG_NVME_FC)			+= nvme-fc.o
obj-$(CONFIG_NVME_TCP)			+= nvme-tcp.o
obj-$(CONFIG_NVME_APPLE)		+= nvme-apple.o

nvme-core-y				+= core.o ioctl.o sysfs.o pr.o
nvme-core-$(CONFIG_NVME_VERBOSE_ERRORS)	+= constants.o
nvme-core-$(CONFIG_TRACING)		+= trace.o
nvme-core-$(CONFIG_NVME_MULTIPATH)	+= multipath.o
nvme-core-$(CONFIG_BLK_DEV_ZONED)	+= zns.o
nvme-core-$(CONFIG_FAULT_INJECTION_DEBUG_FS)	+= fault_inject.o
nvme-core-$(CONFIG_NVME_HWMON)		+= hwmon.o
nvme-core-$(CONFIG_NVME_AUTH)		+= auth.o

custom_nvme-y					+= pci.o

nvme-fabrics-y				+= fabrics.o

nvme-rdma-y				+= rdma.o

nvme-fc-y				+= fc.o

nvme-tcp-y				+= tcp.o

nvme-apple-y				+= apple.o
