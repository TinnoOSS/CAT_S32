/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>

#include "flashlight.h"
#include "flashlight-dt.h"
#include "flashlight-core.h"

/* device tree should be defined in flashlight-dt.h */
#ifndef AW3643_DTNAME
#define AW3643_DTNAME "mediatek,flashlights_aw3643"
#endif
#ifndef AW3643_DTNAME_I2C
#define AW3643_DTNAME_I2C "mediatek,strobe_main"
#endif

#define AW3643_NAME "flashlights_aw3643"

/* define registers */
#define AW3643_REG_ENABLE (0x01)
#define AW3643_MASK_ENABLE_LED1 (0x01)
#define AW3643_MASK_ENABLE_LED2 (0x02)
#define AW3643_DISABLE (0x00)
#define AW3643_ENABLE_LED1 (0x01)
#define AW3643_ENABLE_LED1_TORCH (0x09)
#define AW3643_ENABLE_LED1_FLASH (0x0D)
#define AW3643_ENABLE_LED2 (0x02)
#define AW3643_ENABLE_LED2_TORCH (0x0A)
#define AW3643_ENABLE_LED2_FLASH (0x0E)

#define AW3643_REG_TORCH_LEVEL_LED1 (0x05)
#define AW3643_REG_FLASH_LEVEL_LED1 (0x03)
#define AW3643_REG_TORCH_LEVEL_LED2 (0x06)
#define AW3643_REG_FLASH_LEVEL_LED2 (0x04)

#define AW3643_REG_TIMING_CONF (0x08)
#define AW3643_TORCH_RAMP_TIME (0x10)
#define AW3643_FLASH_TIMEOUT   (0x0F)

/* define channel, level */
#define AW3643_CHANNEL_NUM 2
#define AW3643_CHANNEL_CH1 0
#define AW3643_CHANNEL_CH2 1
#define AW3643_CURRENT_NUM 30
#define AW3643_LEVEL_NUM 26
#define AW3643_LEVEL_TORCH 5

/* define mutex and work queue */
static DEFINE_MUTEX(aw3643_mutex);
static struct work_struct aw3643_work_ch1;
static struct work_struct aw3643_work_ch2;
#if 0
/* define pinctrl */
#define AW3643_PINCTRL_PIN_HWEN 0
#define AW3643_PINCTRL_PINSTATE_LOW 0
#define AW3643_PINCTRL_PINSTATE_HIGH 1
#define AW3643_PINCTRL_STATE_HWEN_HIGH "hwen_high"
#define AW3643_PINCTRL_STATE_HWEN_LOW  "hwen_low"
static struct pinctrl *aw3643_pinctrl;
static struct pinctrl_state *aw3643_hwen_high;
static struct pinctrl_state *aw3643_hwen_low;
#endif
struct pinctrl *flashlightpinctrl = NULL;
struct pinctrl_state *flashlight_en_h = NULL;
struct pinctrl_state *flashlight_en_l = NULL;

/* define usage count */
static int use_count;

/* define i2c */
static struct i2c_client *aw3643_i2c_client;

/* platform data */
struct aw3643_platform_data {
	u8 torch_pin_enable;         /* 1: TX1/TORCH pin isa hardware TORCH enable */
	u8 pam_sync_pin_enable;      /* 1: TX2 Mode The ENVM/TX2 is a PAM Sync. on input */
	u8 thermal_comp_mode_enable; /* 1: LEDI/NTC pin in Thermal Comparator Mode */
	u8 strobe_pin_disable;       /* 1: STROBE Input disabled */
	u8 vout_mode_enable;         /* 1: Voltage Out Mode enable */
};

/* aw3643 chip data */
struct aw3643_chip_data {
	struct i2c_client *client;
	struct aw3643_platform_data *pdata;
	struct mutex lock;
	u8 last_flag;
	u8 no_pdata;
};
#define FLASH_DRV_INFO(format, args...) pr_info("[%s] " format, __func__, ##args)

/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int aw3643_pinctrl_init(struct platform_device *pdev)
{

	FLASH_DRV_INFO("enter\n");
	flashlightpinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(flashlightpinctrl)) {
		printk("IS_ERR(flashlightpinctrl) \n");
		return  -1;
	}

	flashlight_en_l= pinctrl_lookup_state(flashlightpinctrl, "flashlightpin_en0");
	if (IS_ERR(flashlight_en_l)) {
		printk("IS_ERR(flashlight_en_l) \n");
		return -1;
	}
	flashlight_en_h= pinctrl_lookup_state(flashlightpinctrl, "flashlightpin_en1");
	if (IS_ERR(flashlight_en_h)) {
		printk("IS_ERR(flashlight_en_h) \n");
		return -1;
	}

	FLASH_DRV_INFO("exit\n");
	return 0;

}


#if 0
static int aw3643_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (IS_ERR(aw3643_pinctrl)) {
		printk("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case AW3643_PINCTRL_PIN_HWEN:
		if (state == AW3643_PINCTRL_PINSTATE_LOW && !IS_ERR(aw3643_hwen_low))
			pinctrl_select_state(aw3643_pinctrl, aw3643_hwen_low);
		else if (state == AW3643_PINCTRL_PINSTATE_HIGH && !IS_ERR(aw3643_hwen_high))
			pinctrl_select_state(aw3643_pinctrl, aw3643_hwen_high);
		else
			printk("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		printk("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	printk("pin(%d) state(%d)\n", pin, state);

	return ret;
}

#endif


static int aw3643_decouple_mode;
static int aw3643_keepstate_decouple_mode;

//IFLASH(mA)≈(Brightness Code*11.72mA)+11.35mA
static const int aw3643_current[AW3643_CURRENT_NUM] = {//y=25 + 50*x
	23,   46,   69,   93,   116,  140,  163,  198,
	245,  304,  351,  398,  444,  503,  550,  597,
	655,  702,  749,  796,  855,  902,  948,  995,
	1054, 1101
};

//ITORCH(mA)≈(Brightness Code*2.91mA)+2.55mA
static const int aw3643_torch_current[AW3643_CURRENT_NUM] = {
	23,  46,  69,  93,  116, 140, 163
};

//ITORCH(mA)≈(Brightness Code*2.91mA)+2.55mA
static const unsigned char aw3643_torch_level[AW3643_LEVEL_NUM] = {
	0x7,  0xe,  0x16,  0x1f,  0x26,  0x2f,  0x37};

//IFLASH(mA)≈(Brightness Code*11.72mA)+11.35mA
static const unsigned char aw3643_flash_level[AW3643_LEVEL_NUM] = {
	0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x10, 0x14, 0x19,
	0x1D, 0x21, 0x25, 0x2A, 0x2E, 0x32, 0x37, 0x3B, 0x3F, 0x43,
	0x48, 0x4C, 0x50, 0x54, 0x59, 0x5D};

static volatile unsigned char aw3643_reg_enable;
static volatile int aw3643_level_ch1 = -1;
static volatile int aw3643_level_ch2 = -1;

static int aw3643_is_torch(int level)
{
	if (level >= AW3643_LEVEL_TORCH)
		return -1;

	return 0;
}

static int aw3643_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= AW3643_LEVEL_NUM)
		level = AW3643_LEVEL_NUM - 1;

	return level;
}

/* i2c wrapper function */
static int aw3643_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	struct aw3643_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		printk("failed writing at 0x%02x\n", reg);

	return ret;
}

#if 0
static int aw3643_flash_read(struct i2c_client *client, u8 reg)
{
	int ret;
	//char data = 0;
	struct aw3643_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);
	FLASH_DRV_INFO("%s reg:0x%x val:0x%x\n", __func__, reg, ret);
	if (ret < 0)
		pr_info("failed reading at 0x%02x\n", reg);

	return ret;
}
#endif

/* flashlight enable function */
static int aw3643_enable_ch1(void)
{
	unsigned char reg, val;

	reg = AW3643_REG_ENABLE;
	if (!aw3643_is_torch(aw3643_level_ch1)) {
		/* torch mode */
		aw3643_reg_enable |= AW3643_ENABLE_LED1_TORCH;
	} else {
		/* flash mode */
		aw3643_reg_enable |= AW3643_ENABLE_LED1_FLASH;
	}
	val = aw3643_reg_enable;

	return aw3643_write_reg(aw3643_i2c_client, reg, val);
}

static int aw3643_enable_ch2(void)
{
	unsigned char reg, val;

	reg = AW3643_REG_ENABLE;
	if (!aw3643_is_torch(aw3643_level_ch2)) {
		/* torch mode */
		aw3643_reg_enable |= AW3643_ENABLE_LED2_TORCH;
		FLASH_DRV_INFO("enable torch mode 0x%x\n", AW3643_REG_ENABLE);
	} else {
		/* flash mode */
		aw3643_reg_enable |= AW3643_ENABLE_LED2_FLASH;
		FLASH_DRV_INFO("enable flash mode 0x%x\n", AW3643_REG_ENABLE);
	}
	val = aw3643_reg_enable;
	FLASH_DRV_INFO("evel_ch2 %d", aw3643_level_ch2);
	return aw3643_write_reg(aw3643_i2c_client, reg, val);
}

static int aw3643_enable(int channel)
{
	if (channel == AW3643_CHANNEL_CH1)
		aw3643_enable_ch1();
	else if (channel == AW3643_CHANNEL_CH2)
		aw3643_enable_ch2();
	else {
		printk("Error channel\n");
		return -1;
	}

	return 0;
}

/* flashlight disable function */
static int aw3643_disable_ch1(void)
{
	unsigned char reg, val;

	reg = AW3643_REG_ENABLE;
	if (aw3643_reg_enable & AW3643_MASK_ENABLE_LED2) {
		/* if LED 2 is enable, disable LED 1 */
		aw3643_reg_enable &= (~AW3643_ENABLE_LED1);
	} else {
		/* if LED 2 is enable, disable LED 1 and clear mode */
		aw3643_reg_enable &= (~AW3643_ENABLE_LED1_FLASH);
	}
	val = aw3643_reg_enable;

	return aw3643_write_reg(aw3643_i2c_client, reg, val);
}

static int aw3643_disable_ch2(void)
{
	unsigned char reg, val;

	reg = AW3643_REG_ENABLE;
	if (aw3643_reg_enable & AW3643_MASK_ENABLE_LED1) {
		/* if LED 1 is enable, disable LED 2 */
		aw3643_reg_enable &= (~AW3643_ENABLE_LED2);
	} else {
		/* if LED 1 is enable, disable LED 2 and clear mode */
		aw3643_reg_enable &= (~AW3643_ENABLE_LED2_FLASH);
	}
	val = aw3643_reg_enable;
	FLASH_DRV_INFO("val 0x%x",val);
	return aw3643_write_reg(aw3643_i2c_client, reg, val);
}

static int aw3643_disable(int channel)
{
	if (channel == AW3643_CHANNEL_CH1)
		aw3643_disable_ch1();
	else if (channel == AW3643_CHANNEL_CH2)
		aw3643_disable_ch2();
	else {
		printk("Error channel\n");
		return -1;
	}

	return 0;
}

/* set flashlight level */
static int aw3643_set_level_ch1(int level)
{
	int ret;
	unsigned char reg, val;

	level = aw3643_verify_level(level);
	FLASH_DRV_INFO("level %d\n",level);
	/* set torch brightness level */
	reg = AW3643_REG_TORCH_LEVEL_LED1;
	val = aw3643_torch_level[level];
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);

	aw3643_level_ch1 = level;

	/* set flash brightness level */
	reg = AW3643_REG_FLASH_LEVEL_LED1;
	val = aw3643_flash_level[level];
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);

	return ret;
}

int aw3643_set_level_ch2(int level)
{
	int ret;
	unsigned char reg, val;
	//unsigned char regLel,regEnable;
	level = aw3643_verify_level(level);
	FLASH_DRV_INFO("level2 %d \n",level);
	/* set torch brightness level */
	reg = AW3643_REG_TORCH_LEVEL_LED2;
	val = aw3643_torch_level[level];
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);

	aw3643_level_ch2 = level;

	/* set flash brightness level */
	reg = AW3643_REG_FLASH_LEVEL_LED2;
	val = aw3643_flash_level[level];
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);
	//regLel = aw3643_flash_read(aw3643_i2c_client, AW3643_REG_FLASH_LEVEL_LED2);
	//regEnable = aw3643_flash_read(aw3643_i2c_client, AW3643_REG_ENABLE);
	//FLASH_DRV_INFO("led2 level %d  val:0x%x  reglev: 0x%x  eablereg:0x%x\n",level, val, regLel, regEnable);

	return ret;
}

static int aw3643_set_level(int channel, int level)
{

	FLASH_DRV_INFO("chn %d lev %d\n", channel, level);
	if (channel == AW3643_CHANNEL_CH1)
		aw3643_set_level_ch1(level);
	else if (channel == AW3643_CHANNEL_CH2)
		aw3643_set_level_ch2(level);
	else {
		printk("Error channel\n");
		return -1;
	}

	return 0;
}

static int aw3643_set_scenario(int scenario)
{
	/* set decouple mode */
	aw3643_decouple_mode = scenario & FLASHLIGHT_SCENARIO_DECOUPLE_MASK;
	aw3643_keepstate_decouple_mode =
		scenario & FLASHLIGHT_SCENARIO_KEEPSTATE_DECOUPLE_MASK;
	return 0;
}
/* flashlight init */
int aw3643_init(void)
{
	int ret;
	unsigned char reg, val;
	FLASH_DRV_INFO("E");
	pinctrl_select_state(flashlightpinctrl, flashlight_en_h);

	/* clear enable register */
	reg = AW3643_REG_ENABLE;
	val = AW3643_DISABLE;
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);

	aw3643_reg_enable = val;

	/* set torch current ramp time and flash timeout */
	reg = AW3643_REG_TIMING_CONF;
	val = AW3643_TORCH_RAMP_TIME | AW3643_FLASH_TIMEOUT;
	ret = aw3643_write_reg(aw3643_i2c_client, reg, val);
	FLASH_DRV_INFO("X");
	return ret;
}

/* flashlight uninit */
int aw3643_uninit(void)
{
	aw3643_disable(AW3643_CHANNEL_CH1);
	aw3643_disable(AW3643_CHANNEL_CH2);
	//pinctrl_select_state(flashlightpinctrl, flashlight_mode_l);
	pinctrl_select_state(flashlightpinctrl, flashlight_en_l);
	//pinctrl_select_state(flashlightpinctrl, flashlight_ext1_l);

	//aw3643_pinctrl_set(AW3643_PINCTRL_PIN_HWEN, AW3643_PINCTRL_PINSTATE_LOW);

	return 0;
}


/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static struct hrtimer aw3643_timer_ch1;
static struct hrtimer aw3643_timer_ch2;
static unsigned int aw3643_timeout_ms[AW3643_CHANNEL_NUM];

static void aw3643_work_disable_ch1(struct work_struct *data)
{
	FLASH_DRV_INFO("ht work queue callback\n");
	aw3643_disable_ch1();
}

static void aw3643_work_disable_ch2(struct work_struct *data)
{
	FLASH_DRV_INFO("lt work queue callback\n");
	aw3643_disable_ch2();
}

static enum hrtimer_restart aw3643_timer_func_ch1(struct hrtimer *timer)
{
	schedule_work(&aw3643_work_ch1);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart aw3643_timer_func_ch2(struct hrtimer *timer)
{
	schedule_work(&aw3643_work_ch2);
	return HRTIMER_NORESTART;
}

int aw3643_timer_start(int channel, ktime_t ktime)
{
	if (channel == AW3643_CHANNEL_CH1)
		hrtimer_start(&aw3643_timer_ch1, ktime, HRTIMER_MODE_REL);
	else if (channel == AW3643_CHANNEL_CH2)
		hrtimer_start(&aw3643_timer_ch2, ktime, HRTIMER_MODE_REL);
	else {
		printk("Error channel\n");
		return -1;
	}

	return 0;
}

int aw3643_timer_cancel(int channel)
{
	if (channel == AW3643_CHANNEL_CH1)
		hrtimer_cancel(&aw3643_timer_ch1);
	else if (channel == AW3643_CHANNEL_CH2)
		hrtimer_cancel(&aw3643_timer_ch2);
	else {
		printk("Error channel\n");
		return -1;
	}

	return 0;
}

static int dutylevel = 0;
/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int aw3643_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;


	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	/* verify channel */
	if (channel < 0 || channel >= AW3643_CHANNEL_NUM) {
		printk("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		printk("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		aw3643_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		aw3643_set_level(channel, fl_arg->arg);
		dutylevel =  (int)fl_arg->arg;
		FLASH_DRV_INFO("FLASH_IOC_SET_DUTY(%d):%d %d\n",
				channel, (int)fl_arg->arg,dutylevel);
		break;
	case FLASH_IOC_SET_SCENARIO:
		pr_debug("FLASH_IOC_SET_SCENARIO(%d): %d\n",
				channel, (int)fl_arg->arg);
		aw3643_set_scenario(fl_arg->arg);
		break;
	case FLASH_IOC_GET_DUTY_CURRENT:
		pr_debug("FLASH_IOC_GET_DUTY_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		 fl_arg->arg = aw3643_current[dutylevel];
		break;
	case FLASH_IOC_GET_DUTY_NUMBER:
		pr_debug("FLASH_IOC_GET_DUTY_NUMBER(%d): %d\n",
				channel, (int)fl_arg->arg);
		  fl_arg->arg = AW3643_LEVEL_NUM-1;
		break;
	case FLASH_IOC_SET_ONOFF:
		FLASH_DRV_INFO("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if (fl_arg->arg == 1) {
			if (aw3643_timeout_ms[channel]) {
				ktime = ktime_set(aw3643_timeout_ms[channel] / 1000,
						(aw3643_timeout_ms[channel] % 1000) * 1000000);
				aw3643_timer_start(channel, ktime);
			}
			aw3643_enable(channel);
		} else {
			aw3643_disable(channel);
			aw3643_timer_cancel(channel);
		}
		break;

	default:
		FLASH_DRV_INFO("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int aw3643_open(void)
{
	FLASH_DRV_INFO("E");
	//pinctrl_select_state(flashlightpinctrl, flashlight_en_h);
	/* Actual behavior move to set driver function since power saving issue */
	return 0;
}

static int aw3643_release(void)
{
	/* uninit chip and clear usage count */
	mutex_lock(&aw3643_mutex);
	use_count--;
	if (!use_count)
		aw3643_uninit();
	if (use_count < 0)
		use_count = 0;
	mutex_unlock(&aw3643_mutex);

	FLASH_DRV_INFO("Release: %d\n", use_count);

	return 0;
}

static int aw3643_set_driver(int set)
{
	/* init chip and set usage count */
	mutex_lock(&aw3643_mutex);
	if (!use_count)
		aw3643_init();
	use_count++;
	mutex_unlock(&aw3643_mutex);

	FLASH_DRV_INFO("Set driver: %d\n", use_count);

	return 0;
}

static ssize_t aw3643_strobe_store(struct flashlight_arg arg)
{
	aw3643_set_driver(0);
	aw3643_set_level(arg.ct, arg.level);
	aw3643_enable(arg.ct);
	msleep(arg.dur);
	aw3643_disable(arg.ct);
	aw3643_release();
	//aw3643_set_driver(0);
	return 0;
}

static struct flashlight_operations aw3643_ops = {
	aw3643_open,
	aw3643_release,
	aw3643_ioctl,
	aw3643_strobe_store,
	aw3643_set_driver
};


/******************************************************************************
 * I2C device and driver
 *****************************************************************************/
static int aw3643_chip_init(struct aw3643_chip_data *chip)
{
	/* NOTE: Chip initialication move to "set driver" operation for power saving issue.
	 * aw3643_init();
	 */
	pinctrl_select_state(flashlightpinctrl, flashlight_en_h);
	return 0;
}

static int aw3643_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct aw3643_chip_data *chip;
	struct aw3643_platform_data *pdata = client->dev.platform_data;
	int err;
	//pinctrl_select_state(flashlightpinctrl, flashlight_en_h);
	FLASH_DRV_INFO("%s Probe start.\n",__func__);

	/* check i2c */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk("Failed to check i2c functionality.\n");
		err = -ENODEV;
		goto err_out;
	}

	/* init chip private data */
	chip = kzalloc(sizeof(struct aw3643_chip_data), GFP_KERNEL);
	if (!chip) {
		err = -ENOMEM;
		goto err_out;
	}
	chip->client = client;

	/* init platform data */
	if (!pdata) {
		pdata = kzalloc(sizeof(struct aw3643_platform_data), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err_init_pdata;
		}
		chip->no_pdata = 1;
	}
	chip->pdata = pdata;
	i2c_set_clientdata(client, chip);
	aw3643_i2c_client = client;

	/* init mutex and spinlock */
	mutex_init(&chip->lock);

	/* init work queue */
	INIT_WORK(&aw3643_work_ch1, aw3643_work_disable_ch1);
	INIT_WORK(&aw3643_work_ch2, aw3643_work_disable_ch2);

	/* init timer */
	hrtimer_init(&aw3643_timer_ch1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw3643_timer_ch1.function = aw3643_timer_func_ch1;
	hrtimer_init(&aw3643_timer_ch2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw3643_timer_ch2.function = aw3643_timer_func_ch2;
	aw3643_timeout_ms[AW3643_CHANNEL_CH1] = 100;
	aw3643_timeout_ms[AW3643_CHANNEL_CH2] = 100;

	/* init chip hw */
	aw3643_chip_init(chip);
	pr_info("%s aw3643 flashlight_dev_register  \n",__func__);
	/* register flashlight operations */
	if (flashlight_dev_register(AW3643_NAME, &aw3643_ops)) {
		printk("aw3643 Failed to register flashlight device.\n");
		err = -EFAULT;
		goto err_free;
	}

	/* clear usage count */
	use_count = 0;

	printk("%s: aw3643 flashlight i2c Probe done.\n", __func__);

	return 0;

err_free:
	kfree(chip->pdata);
err_init_pdata:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
err_out:
	return err;
}

static int aw3643_i2c_remove(struct i2c_client *client)
{
	struct aw3643_chip_data *chip = i2c_get_clientdata(client);

	pr_info("Remove start.\n");

	/* flush work queue */
	flush_work(&aw3643_work_ch1);
	flush_work(&aw3643_work_ch2);

	/* unregister flashlight operations */
	flashlight_dev_unregister(AW3643_NAME);

	/* free resource */
	if (chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);

	pr_info("Remove done.\n");

	return 0;
}

static const struct i2c_device_id aw3643_i2c_id[] = {
	{AW3643_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, aw3643_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id aw3643_i2c_of_match[] = {
	{.compatible = AW3643_DTNAME_I2C},
	{},
};
MODULE_DEVICE_TABLE(of, aw3643_i2c_of_match);
#endif

static struct i2c_driver aw3643_i2c_driver = {
	.driver = {
		   .name = AW3643_NAME,
#ifdef CONFIG_OF
		   .of_match_table = aw3643_i2c_of_match,
#endif
		   },
	.probe = aw3643_i2c_probe,
	.remove = aw3643_i2c_remove,
	.id_table = aw3643_i2c_id,
};


/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
static int aw3643_probe(struct platform_device *dev)
{
	pr_info("aw3643_probe start.\n");

	/* init pinctrl */
	if (aw3643_pinctrl_init(dev)) {
		printk("Failed to init pinctrl.\n");
		return -1;
	}

	if (i2c_add_driver(&aw3643_i2c_driver)) {
		printk("Failed to add i2c driver.\n");
		return -1;
	}

	printk("aw3643_probe  done.\n");

	return 0;
}

static int aw3643_remove(struct platform_device *dev)
{
	printk("aw3643_remove Remove start.\n");

	i2c_del_driver(&aw3643_i2c_driver);

	printk("aw3643_remove Remove done.\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id aw3643_of_match[] = {
	{.compatible = AW3643_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, aw3643_of_match);
#else
static struct platform_device aw3643_platform_device[] = {
	{
		.name = AW3643_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, aw3643_platform_device);
#endif

static struct platform_driver aw3643_platform_driver = {
	.probe = aw3643_probe,
	.remove = aw3643_remove,
	.driver = {
		.name = AW3643_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = aw3643_of_match,
#endif
	},
};

static int __init flashlight_aw3643_init(void)
{
	int ret;

	printk("flashlight_aw3643-Init start.\n");

#ifndef CONFIG_OF
	ret = platform_device_register(&aw3643_platform_device);
	if (ret) {
		printk("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&aw3643_platform_driver);
	if (ret) {
		printk("Failed to register platform driver\n");
		return ret;
	}

	printk("flashlight_aw3643 Init done.\n");

	return 0;
}

static void __exit flashlight_aw3643_exit(void)
{
	printk("flashlight_aw3643-Exit start.\n");

	platform_driver_unregister(&aw3643_platform_driver);

	printk("flashlight_aw3643 Exit done.\n");
}

module_init(flashlight_aw3643_init);
module_exit(flashlight_aw3643_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Wang <Simon-TCH.Wang@mediatek.com>");
MODULE_DESCRIPTION("MTK Flashlight AW3643 Driver");

