# include header files for MTK
ccflags-y += -I$(srctree)/drivers/spi/mediatek/mt6797/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/ipio_touch_driver/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/
ccflags-y += -I$(srctree)/tinno/common_features/dev_info/
ccflags-y += -Wall

BUILD_INFAE := i2c
BUILD_PLATFORM := mtk
BUILD_MODULE := n

ifeq ($(BUILD_PLATFORM),mtk)
platform=ilitek_plat_mtk
endif

ifeq ($(BUILD_PLATFORM),qcom)
platform=ilitek_plat_qcom
endif

ifeq ($(BUILD_INFAE),i2c)
interface=ilitek_i2c
endif

ifeq ($(BUILD_INFAE),spi)
interface=ilitek_spi
endif

ifeq ($(BUILD_MODULE),n)
obj-y += ilitek_main.o \
	$(interface).o \
	$(platform).o \
	ilitek_ic.o \
	ilitek_touch.o \
	ilitek_mp.o \
	ilitek_fw.o \
	ilitek_node.o
else
	obj-m += ilitek.o
	ilitek-y := ilitek_main.o \
		$(interface).o \
		$(platform).o \
		ilitek_ic.o \
		ilitek_touch.o \
		ilitek_mp.o \
		ilitek_fw.o \
		ilitek_node.o

#KERNEL_DIR= /home/likewise-open/ILI/1061279/workplace/rk3288_sdk/kernel
#all:
#	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
#clean:
#	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
endif
