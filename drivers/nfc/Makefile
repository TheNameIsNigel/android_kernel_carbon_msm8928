#
# Makefile for nfc devices
#

obj-$(CONFIG_PN544_NFC)		+= pn544.o
obj-$(CONFIG_NFC_PN533)		+= pn533.o
obj-y += pn547.o
obj-$(CONFIG_NFC_WILINK)	+= nfcwilink.o
obj-$(CONFIG_NFC_QNCI)		+= nfc-nci.o
obj-y	+= bcm2079x-i2c.o
obj-y	+= nfc_adaptive.o
ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
