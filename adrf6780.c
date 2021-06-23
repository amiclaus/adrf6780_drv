// SPDX-License-Identifier: GPL-2.0+
/*
 * ADRF6780 driver
 *
 * Copyright 2021 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/clkscale.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/iio/sysfs.h>

/* ADRF6780 Register Map */
#define ADRF6780_REG_CONTROL			0x00
#define ADRF6780_REG_ALARM_READBACK		0x01
#define ADRF6780_REG_ALARM_MASKS		0x02
#define ADRF6780_REG_ENABLE			0x03
#define ADRF6780_REG_LINEARIZE			0x04
#define ADRF6780_REG_LO_PATH			0x05
#define ADRF6780_REG_ADC_CONTROL 		0x06
#define ADRF6780_REG_ADC_OUTPUT			0x0C

/* ADRF6780_REG_CONTROL Map */
#define ADRF6780_PARITY_EN_MSK       		BIT(15)
#define ADRF6780_PARITY_EN(x)			FIELD_PREP(ADRF6780_PARITY_EN_MSK, x)
#define ADRF6780_SOFT_RESET_MSK			BIT(14)
#define ADRF6780_SOFT_RESET(x)         		FIELD_PREP(ADRF6780_SOFT_RESET_MSK, x)
#define ADRF6780_CHIP_ID_MSK			GENMASK(11, 4)
#define ADRF6780_CHIP_ID             		0xA
#define ADRF6780_CHIP_REVISION_MSK		GENMASK(3, 0)
#define ADRF6780_CHIP_REVISION(x)        	FIELD_PREP(ADRF6780_CHIP_REVISION_MSK, x)


/* ADRF6780_REG_ALARM_READBACK Map */
#define ADRF6780_PARITY_ERROR_MSK       	BIT(15)
#define ADRF6780_PARITY_ERROR(x)         	FIELD_PREP(ADRF6780_PARITY_ERROR_MSK, x)
#define ADRF6780_TOO_FEW_ERRORS_MSK		BIT(14)
#define ADRF6780_TOO_FEW_ERRORS(x)         	FIELD_PREP(ADRF6780_TOO_FEW_ERRORS_MSK, x)
#define ADRF6780_TOO_MANY_ERRORS_MSK		BIT(13)
#define ADRF6780_TOO_MANY_ERRORS(x)         	FIELD_PREP(ADRF6780_TOO_MANY_ERRORS_MSK, x)
#define ADRF6780_ADDRESS_RANGE_ERROR_MSK	BIT(12)
#define ADRF6780_ADDRESS_RANGE_ERROR(x)         FIELD_PREP(ADRF6780_ADDRESS_RANGE_ERROR_MSK, x)

/* ADRF6780_REG_ENABLE Map */
#define ADRF6780_VGA_BUFFER_EN_MSK     		BIT(8)
#define ADRF6780_VGA_BUFFER_EN(x)      		FIELD_PREP(ADRF6780_VGA_BUFFER_EN_MSK, x)
#define ADRF6780_DETECTOR_EN_MSK       		BIT(7)
#define ADRF6780_DETECTOR_EN(x)         	FIELD_PREP(ADRF6780_DETECTOR_EN_MSK, x)
#define ADRF6780_LO_BUFFER_EN_MSK		BIT(6)
#define ADRF6780_LO_BUFFER_EN(x)         	FIELD_PREP(ADRF6780_LO_BUFFER_EN_MSK, x)
#define ADRF6780_IF_MODE_EN_MSK			BIT(5)
#define ADRF6780_IF_MODE_EN(x)         		FIELD_PREP(ADRF6780_IF_MODE_EN_MSK, x)
#define ADRF6780_IQ_MODE_EN_MSK			BIT(4)
#define ADRF6780_IQ_MODE_EN(x)         		FIELD_PREP(ADRF6780_IQ_MODE_EN_MSK, x)
#define ADRF6780_LO_X2_EN_MSK			BIT(3)
#define ADRF6780_LO_X2_EN(x)       		FIELD_PREP(ADRF6780_LO_X2_EN_MSK, x)
#define ADRF6780_LO_PPF_EN_MSK			BIT(2)
#define ADRF6780_LO_PPF_EN(x)         		FIELD_PREP(ADRF6780_LO_PPF_EN_MSK, x)
#define ADRF6780_LO_EN_MSK			BIT(1)
#define ADRF6780_LO_EN(x)         		FIELD_PREP(ADRF6780_LO_EN_MSK, x)
#define ADRF6780_UC_BIAS_EN_MSK			BIT(0)
#define ADRF6780_UC_BIAS_EN(x)         		FIELD_PREP(ADRF6780_UC_BIAS_EN_MSK, x)

/* ADRF6780_REG_LINEARIZE Map */
#define ADRF6780_RDAC_LINEARIZE_MSK     	GENMASK(7, 0)
#define ADRF6780_RDAC_LINEARIZE(x)      	FIELD_PREP(ADRF6780_RDAC_LINEARIZE_MSK, x)

/* ADRF6780_REG_LO_PATH Map */
#define ADRF6780_LO_SIDEBAND_MSK     		BIT(10)
#define ADRF6780_LO_SIDEBAND(x)      		FIELD_PREP(ADRF6780_LO_SIDEBAND_MSK, x)
#define ADRF6780_Q_PATH_PHASE_ACCURACY_MSK     	GENMASK(7, 4)
#define ADRF6780_Q_PATH_PHASE_ACCURACY(x)  	FIELD_PREP(ADRF6780_Q_PATH_PHASE_ACCURACY_MSK, x)
#define ADRF6780_I_PATH_PHASE_ACCURACY_MSK     	GENMASK(3, 0)
#define ADRF6780_I_PATH_PHASE_ACCURACY(x)      	FIELD_PREP(ADRF6780_I_PATH_PHASE_ACCURACY_MSK, x)

/* ADRF6780_REG_ADC_CONTROL Map */
#define ADRF6780_VDET_OUTPUT_SELECT_MSK    	BIT(3)
#define ADRF6780_VDET_OUTPUT_SELECT(x)     	FIELD_PREP(ADRF6780_VDET_OUTPUT_SELECT_MSK, x)
#define ADRF6780_ADC_START_MSK     		BIT(2)
#define ADRF6780_ADC_START(x)      		FIELD_PREP(ADRF6780_ADC_START_MSK, x)
#define ADRF6780_ADC_EN_MSK     		BIT(1)
#define ADRF6780_ADC_EN(x)  			FIELD_PREP(ADRF6780_ADC_EN_MSK, x)
#define ADRF6780_ADC_CLOCK_EN_MSK     		BIT(0)
#define ADRF6780_ADC_CLOCK_EN(x)      		FIELD_PREP(ADRF6780_ADC_CLOCK_EN_MSK, x)

/* ADRF6780_REG_ADC_OUTPUT Map */
#define ADRF6780_ADC_STATUS_MSK     		BIT(8)
#define ADRF6780_ADC_STATUS(x)  		FIELD_PREP(ADRF6780_ADC_STATUS_MSK, x)
#define ADRF6780_ADC_VALUE_MSK     		GENMASK(7, 0)
#define ADRF6780_ADC_VALUE(x)      		FIELD_PREP(ADRF6780_ADC_VALUE_MSK, x)

enum supported_parts {
	ADRF6780,
};

struct adrf6780_dev {
	struct spi_device	*spi;
	struct clk 		*clkin;
	struct clock_scale	clkscale;
	/* Protect against concurrent accesses to the device */
	struct mutex		lock;
	struct notifier_block	nb;
	u64			clkin_freq;
	bool			parity_en;
	bool			vga_buff_en;
	bool			det_en;
	bool			lo_buff_en;
	bool			if_mode_en;
	bool			iq_mode_en;
	bool			lo_x2_en;
	bool			lo_ppf_en;
	bool			lo_en;
	bool			uc_bias_en;
	bool			lo_en;
	bool			lo_sideband;
	bool			vga_buff_en;
};

static int adrf6780_spi_read(struct adrf6780_dev *dev, unsigned int reg,
			      unsigned int *val)
{
	int ret;
	unsigned int cnt, temp;
	struct spi_transfer t = {0};
	u8 data[3];

	data[0] = 0x80 | (reg << 1);
	data[1] = 0x0;
	data[2] = 0x0;

	t.rx_buf = &data[0];
	t.tx_buf = &data[0];
	t.len = 3;

	ret = spi_sync_transfer(dev->spi, &t, 1);
	if (ret < 0)
		return ret;

	temp = ((data[0] | 0x80 | (reg << 1)) << 16) |
		(data[1] << 8) | data[2];

	if (dev->parity_en) {
		cnt = hweight_long(temp);
		if (!(cnt % 2))
			return -EINVAL;
	}

	*val = (temp >> 1) & 0xFFFF;

	return ret;
}

static int adrf6780_spi_write(struct adrf6780_dev *dev,
				      unsigned int reg,
				      unsigned int val)
{
	unsigned int cnt;
	u8 data[3];

	val = (val << 1);

	if (dev->parity_en) {
		cnt = hweight_long((reg << 17) | val);
		if (cnt % 2 == 0)
			val |= 0x1;
	}

	data[0] = (reg << 1) | (val >> 16);
	data[1] = val >> 8;
	data[2] = val;

	return spi_write(dev->spi, &data[0], 3);
}

static int __adrf6780_spi_update_bits(struct adrf6780_dev *dev, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	int ret;
	unsigned int data, temp;

	ret = adrf6780_spi_read(dev, reg, &data);
	if (ret < 0)
		return ret;

	temp = (data & ~mask) | (val & mask);

	return adrf6780_spi_write(dev, reg, temp);
}

static int adrf6780_spi_update_bits(struct adrf6780_dev *dev, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	int ret;

	mutex_lock(&dev->lock);
	ret = __adrf6780_spi_update_bits(dev, reg, mask, val);
	mutex_unlock(&dev->lock);
	return ret;
}


static int adrf6780_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct adrf6780_dev *dev = iio_priv(indio_dev);
	unsigned int temp;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = adrf6780_spi_update_bits(dev, ADRF6780_REG_ENABLE,
						ADRF6780_DETECTOR_EN_MSK,
						ADRF6780_DETECTOR_EN(1));
		if (ret < 0)
			return ret;

		ret = adrf6780_spi_update_bits(dev, ADRF6780_REG_ADC_CONTROL,
						ADRF6780_ADC_EN_MSK,
						ADRF6780_ADC_EN(1));
		if (ret < 0)
			return ret;
		
		ret = adrf6780_spi_update_bits(dev, ADRF6780_REG_ADC_CONTROL,
						ADRF6780_ADC_CLOCK_EN_MSK,
						ADRF6780_ADC_CLOCK_EN(1));
		if (ret < 0)
			return ret;
		
		ret = adrf6780_spi_update_bits(dev, ADRF6780_REG_ADC_CONTROL,
						ADRF6780_ADC_START_MSK,
						ADRF6780_ADC_START(1));
		if (ret < 0)
			return ret;

		udelay(200);

		ret = adrf6780_spi_read(dev, ADRF6780_REG_ADC_OUTPUT, &temp);
		if (ret < 0)
			return ret;

		if (!(temp & ADRF6780_ADC_STATUS_MSK))
			return -EINVAL;
		
		ret = adrf6780_spi_update_bits(dev, ADRF6780_REG_ADC_CONTROL,
						ADRF6780_ADC_START_MSK,
						ADRF6780_ADC_START(0));
		
		ret = adrf6780_spi_read(dev, ADRF6780_REG_ADC_OUTPUT, &temp);
		if (ret < 0)
			return ret;
		
		*val = temp & ADRF6780_ADC_VALUE_MSK;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int adrf6780_reg_access(struct iio_dev *indio_dev,
				unsigned int reg,
				unsigned int write_val,
				unsigned int *read_val)
{
	struct adrf6780_dev *dev = iio_priv(indio_dev);

	if (read_val)
		return adrf6780_spi_read(dev, reg, read_val);
	else
		return adrf6780_spi_write(dev, reg, write_val);
}

static const struct iio_info adrf6780_info = {
	.read_raw = adrf6780_read_raw,
	.write_raw = adrf6780_write_raw,
	.debugfs_reg_access = &adrf6780_reg_access,
};

static int adrf6780_freq_change(struct notifier_block *nb, unsigned long flags, void *data)
{
	struct adrf6780_dev *dev = container_of(nb, struct adrf6780_dev, nb);
	struct clk_notifier_data *cnd = data;

	/* cache the new rate */
	dev->clkin_freq = clk_get_rate_scaled(cnd->clk, &dev->clkscale);

	return notifier_from_errno(adrf6780_update_quad_filters(dev));
}

static void adrf6780_clk_notifier_unreg(void *data)
{
	struct adrf6780_dev *dev = data;

	clk_notifier_unregister(dev->clkin, &dev->nb);
}

#define ADRF6780_CHAN(_channel) {			\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)	\
}

#define ADMV1013_CHAN(_channel, rf_comp) {			\
	.type = IIO_ALTVOLTAGE,					\
	.modified = 1,						\
	.output = 1,						\
	.indexed = 1,						\
	.channel2 = IIO_MOD_##rf_comp,				\
	.channel = _channel,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PHASE)		\
		BIT(IIO_CHAN_INFO_OFFSET)			\
	}

static const struct iio_chan_spec adrf6780_channels[] = {
	ADRF6780_CHAN(0),
	ADMV1013_CHAN(0, I),
	ADMV1013_CHAN(0, Q),
};

static int adrf6780_init(struct adrf6780_dev *dev)
{
	int ret;
	unsigned int chip_id, enable_reg, enable_reg_msk;
	struct spi_device *spi = dev->spi;
	bool temp_parity = dev->parity_en;

	/* Perform a software reset */
	ret = __adrf6780_spi_update_bits(dev, ADRF6780_REG_CONTROL,
				 ADRF6780_SOFT_RESET_MSK,
				 ADRF6780_SOFT_RESET(1));
	if (ret < 0) {
		dev_err(&spi->dev, "ADRF6780 SPI software reset failed.\n");
		return ret;
	}

	ret = __adrf6780_spi_update_bits(dev, ADRF6780_REG_CONTROL,
				 ADRF6780_SOFT_RESET_MSK,
				 ADRF6780_SOFT_RESET(0));
	if (ret < 0) {
		dev_err(&spi->dev, "ADRF6780 SPI software reset disable failed.\n");
		return ret;
	}
	
	ret = __adrf6780_spi_update_bits(dev, ADRF6780_REG_CONTROL,
				 ADRF6780_PARITY_EN_MSK,
				 ADRF6780_PARITY_EN(temp_parity));
	if (ret < 0)
		return ret;

	dev->parity_en = temp_parity;

	ret = adrf6780_spi_read(dev, ADRF6780_REG_CONTROL, &chip_id);
	if (ret < 0)
		return ret;

	chip_id = (chip_id & ADRF6780_CHIP_ID_MSK) >> 4;
	if (chip_id != ADRF6780_CHIP_ID)
		return -EINVAL;

	return ret;
}

static void adrf6780_clk_disable(void *data)
{
	struct adrf6780_dev *dev = data;

	clk_disable_unprepare(dev->clkin);
}

static int adrf6780_dt_parse(struct adrf6780_dev *dev)
{
	int ret;
	struct spi_device *spi = dev->spi;

	dev->parity_en = of_property_read_bool(spi->dev.of_node, "adi,parity-en");
	dev->vga_buff_en = of_property_read_bool(spi->dev.of_node, "adi,vga-buff-en");
	dev->det_en = of_property_read_bool(spi->dev.of_node, "adi,det-en");
	dev->lo_buff_en = of_property_read_bool(spi->dev.of_node, "adi,lo-buff-en");
	dev->if_mode_en = of_property_read_bool(spi->dev.of_node, "adi,if-mode-en");
	dev->iq_mode_en = of_property_read_bool(spi->dev.of_node, "adi,iq-mode-en");
	dev->lo_x2_en = of_property_read_bool(spi->dev.of_node, "adi,lo-x2-en");
	dev->lo_ppf_en = of_property_read_bool(spi->dev.of_node, "adi,lo-ppf-en");
	dev->lo_en = of_property_read_bool(spi->dev.of_node, "adi,lo-en");
	dev->uc_bias_en = of_property_read_bool(spi->dev.of_node, "adi,uc-bias-en");
	dev->lo_en = of_property_read_bool(spi->dev.of_node, "adi,lo-en");
	dev->lo_sideband = of_property_read_bool(spi->dev.of_node, "adi,lo-sideband");
	dev->vdet_out_en = of_property_read_bool(spi->dev.of_node, "adi,vdet-out-en");

	dev->clkin = devm_clk_get(&spi->dev, "lo_in");
	if (IS_ERR(dev->clkin))
		return PTR_ERR(dev->clkin);

	return of_clk_get_scale(spi->dev.of_node, NULL, &dev->clkscale);

static int adrf6780_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adrf6780_dev *dev;
	struct clock_scale dev_clkscale;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*dev));
	if (!indio_dev)
		return -ENOMEM;

	dev = iio_priv(indio_dev);

	dev->parity_en = of_property_read_bool(spi->dev.of_node, "adi,parity-enable");

	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adrf6780_info;
	indio_dev->name = "adrf6780";
	indio_dev->channels = adrf6780_channels;
	indio_dev->num_channels = ARRAY_SIZE(adrf6780_channels);

	dev->spi = spi;

	dev->clkin = devm_clk_get(&spi->dev, "lo_in");
	if (IS_ERR(dev->clkin)) {
		return PTR_ERR(dev->clkin);
	}

	ret = clk_prepare_enable(dev->clkin);
	if (ret < 0) {
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, adrf6780_clk_disable, dev);
	if (ret < 0) {
		return ret;
	}

	of_clk_get_scale(spi->dev.of_node, "lo_in", &dev_clkscale);

	dev->clkscale = &dev_clkscale;

	dev->clkin_freq = clk_get_rate_scaled(dev->clkin, dev->clkscale);
	dev->nb.notifier_call = adrf6780_freq_change;
	ret = clk_notifier_register(dev->clkin, &dev->nb);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, adrf6780_clk_notifier_unreg, dev);
	if (ret < 0) {
		return ret;
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}


static int adrf6780_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct spi_device_id adrf6780_id[] = {
	{ "adrf6780", ADRF6780 },
	{}
};
MODULE_DEVICE_TABLE(spi, adrf6780_id);

static const struct of_device_id adrf6780_of_match[] = {
	{ .compatible = "adi,adrf6780" },
	{},
};
MODULE_DEVICE_TABLE(of, adrf6780_of_match);

static struct spi_driver adrf6780_driver = {
	.driver = {
			.name = "adrf6780",
			.of_match_table = adrf6780_of_match,
		},
	.probe = adrf6780_probe,
	.remove = adrf6780_remove,
	.id_table = adrf6780_id,
};
module_spi_driver(adrf6780_driver);

MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com");
MODULE_DESCRIPTION("Analog Devices ADRF6780");
MODULE_LICENSE("GPL v2");
