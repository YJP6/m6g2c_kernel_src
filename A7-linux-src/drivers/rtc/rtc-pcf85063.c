/*
 * An I2C driver for the Philips PCF85063 RTC
 * Copyright 2005-06 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * based on the other drivers in this same directory.
 *
 * http://www.semiconductors.philips.com/acrobat/datasheets/PCF85063-04.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/err.h>

#define DRV_VERSION "0.4.3"
#define CLKOUT_ON	_IO('p', 0x20)
#define CLKOUT_OFF	_IO('p', 0x21)

#define PCF85063_REG_ST1		0x00 /* status */
#define PCF85063_REG_ST2		0x01
#define PCF85063_REG_OFFSET 	0x02
#define PCF85063_REG_RAM_TYPE	0x03

#define PCF85063_REG_SC		0x04 /* datetime */
#define PCF85063_REG_MN		0x05
#define PCF85063_REG_HR		0x06
#define PCF85063_REG_DM		0x07
#define PCF85063_REG_DW		0x08
#define PCF85063_REG_MO		0x09
#define PCF85063_REG_YR		0x0A

#define PCF85063_REG_ASC		0x0B
#define PCF85063_REG_AMN		0x0C /* alarm */
#define PCF85063_REG_AHR		0x0D
#define PCF85063_REG_ADM		0x0E
#define PCF85063_REG_ADW		0x0F

#define PCF85063_REG_TM_VALUE	0x10	/* timer_value */
#define PCF85063_REG_TM_MODE	0x11	/* timer_mode */

#define PCF85063_OS_STOP		0x80 /*clock integrity is not guaranteed */

static struct i2c_driver pcf85063_driver;

struct pcf85063 {
	struct rtc_device *rtc;
	/*
	 * The meaning of MO_C bit varies by the chip type.
	 * From PCF85063 datasheet: this bit is toggled when the years
	 * register overflows from 99 to 00
	 *   0 indicates the century is 20xx
	 *   1 indicates the century is 19xx
	 * From RTC8564 datasheet: this bit indicates change of
	 * century. When the year digit data overflows from 99 to 00,
	 * this bit is set. By presetting it to 0 while still in the
	 * 20th century, it will be set in year 2000, ...
	 * There seems no reliable way to know how the system use this
	 * bit.  So let's do it heuristically, assuming we are live in
	 * 1970...2069.
	 */
//	int c_polarity;	/* 0: MO_C=1 means 19xx, otherwise MO_C=1 means 20xx */
//	int voltage_low; /* incicates if a low_voltage was detected */
};

/*
 * In the routines that deal directly with the pcf85063 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 */
static int pcf85063_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
//	struct pcf85063 *pcf85063 = i2c_get_clientdata(client);
	unsigned char buf[16] = { PCF85063_REG_ST1 };

	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.len = 1,
			.buf = buf
		},
		{/* read status + date */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 16,
			.buf = buf
		},
	};

	/* read registers */
	if ((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	if (buf[PCF85063_REG_SC] & PCF85063_OS_STOP) {
		dev_info(&client->dev,
			"clock integrity is not guaranteed.\n");
	}

	dev_dbg(&client->dev,
		"%s: raw data is st1=%02x, st2=%02x, offset=%02x, "
		"ram_byte=%02x, sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, wday=%02x, mon=%02x, year=%02x\n",
		__func__,
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7],
		buf[8], buf[9], buf[10]);

	tm->tm_sec = bcd2bin(buf[PCF85063_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF85063_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF85063_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF85063_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF85063_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF85063_REG_MO] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(buf[PCF85063_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return 0;
}

static int pcf85063_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	int i, err;
	unsigned char buf[12];

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* hours, minutes and seconds */
	buf[PCF85063_REG_SC] = bin2bcd(tm->tm_sec);
	buf[PCF85063_REG_MN] = bin2bcd(tm->tm_min);
	buf[PCF85063_REG_HR] = bin2bcd(tm->tm_hour);

	buf[PCF85063_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[PCF85063_REG_MO] = bin2bcd(tm->tm_mon + 1);

	/* year and century */
	buf[PCF85063_REG_YR] = bin2bcd(tm->tm_year % 100);

	buf[PCF85063_REG_DW] = tm->tm_wday & 0x07;

	/* write register's data */
	for (i = 0; i < 7; i++) {
		unsigned char data[2] = { PCF85063_REG_SC + i,
						buf[PCF85063_REG_SC + i] };

		err = i2c_master_send(client, data, sizeof(data));
		if (err != sizeof(data)) {
			dev_err(&client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	return 0;
}

static int pcf85063_rtc_ioctl(struct device *dev, unsigned int cmd, \
								unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char buf[2];
	unsigned char val;
	int err;

	buf[0] = PCF85063_REG_ST2;

	switch(cmd) {
	case CLKOUT_ON:
		val = 0x00;
		buf[1] = val;
		err = i2c_master_send(client, buf, sizeof(buf));
		if(err != sizeof(buf)) {
			dev_err(&client->dev, \
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, buf[0], buf[1]);
			return -EIO;
		}
		break;
	case CLKOUT_OFF:
		val = 0x07;
		buf[1] = val;
		err = i2c_master_send(client, buf, sizeof(buf));
		if(err != sizeof(buf)) {
			dev_err(&client->dev, \
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, buf[0], buf[1]);
			return -EIO;
		}
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int pcf85063_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return pcf85063_get_datetime(to_i2c_client(dev), tm);
}

static int pcf85063_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return pcf85063_set_datetime(to_i2c_client(dev), tm);
}

static ssize_t pcf85063_rtc_show(struct device *dev, \
					struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char data[2] = { PCF85063_REG_ST2};

	struct i2c_msg msgs[] = {
		{ /* setup read ptr */
			.addr = client->addr,
			.len  = 1,
			.buf  = data
		},
		{ /* read reg_st2 */
			.addr  = client->addr,
			.flags = I2C_M_RD,
			.len   = 1,
			.buf   = data
		},
	};

	/* read reg st2 */
	if((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	if(data[0] & 0x07)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "rtc clk_out disable");
	else
		return scnprintf(buf, PAGE_SIZE, "%s\n", "rtc clk_out enable");
}

static ssize_t pcf85063_rtc_store(struct device *dev, \
			struct device_attribute *attr, const char *buf, size_t count)
{
	switch(buf[0]) {
	case '1':	/* enable rtc clk_out */
		pcf85063_rtc_ioctl(dev, CLKOUT_ON, 0);
		break;
	case '0':
		pcf85063_rtc_ioctl(dev, CLKOUT_OFF, 0);
		break;
	default:
		printk(KERN_WARNING "Not support args\n");
	}
	return count;
}

static DEVICE_ATTR(clk_out_ctl, S_IWUSR | S_IRUGO, pcf85063_rtc_show,pcf85063_rtc_store);

static const struct rtc_class_ops pcf85063_rtc_ops = {
	.ioctl		= pcf85063_rtc_ioctl,
	.read_time	= pcf85063_rtc_read_time,
	.set_time	= pcf85063_rtc_set_time,
};

static int pcf85063_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf85063 *pcf85063;
	int ret;

	dev_dbg(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf85063 = devm_kzalloc(&client->dev, sizeof(struct pcf85063),
				GFP_KERNEL);
	if (!pcf85063)
		return -ENOMEM;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	i2c_set_clientdata(client, pcf85063);

	pcf85063->rtc = devm_rtc_device_register(&client->dev,
				pcf85063_driver.driver.name,
				&pcf85063_rtc_ops, THIS_MODULE);

	ret = device_create_file(&client->dev, &dev_attr_clk_out_ctl);
	if(ret != 0)
		dev_err(&client->dev, "%s: device create file err\n", __func__);

	return PTR_ERR_OR_ZERO(pcf85063->rtc);
}

static const struct i2c_device_id pcf85063_id[] = {
	{ "pcf85063", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf85063_id);

#ifdef CONFIG_OF
static const struct of_device_id pcf85063_of_match[] = {
	{ .compatible = "nxp,pcf85063" },
	{}
};
MODULE_DEVICE_TABLE(of, pcf85063_of_match);
#endif

static struct i2c_driver pcf85063_driver = {
	.driver		= {
		.name	= "rtc-pcf85063",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pcf85063_of_match),
	},
	.probe		= pcf85063_probe,
	.id_table	= pcf85063_id,
};

module_i2c_driver(pcf85063_driver);

MODULE_AUTHOR("Codebreaker <shenguiting@zlg.cn>");
MODULE_DESCRIPTION("NXP PCF85063/Epson RTC85064 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
