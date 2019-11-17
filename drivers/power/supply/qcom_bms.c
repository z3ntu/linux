// SPDX-License-Identifier: GPL

/*
 * Qualcomm Battery Monitoring System driver
 *
 * Copyright (C) 2018 Craig Tatlor <ctatlor97@gmail.com>
 * Copyright (C) 2019 Luca Weiss <luca@z3ntu.xyz>
 */

#define DEBUG

#include <linux/module.h>
#include <linux/fixp-arith.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/iio/consumer.h>

#define REG_BMS_OCV_FOR_SOC_DATA0	0x90
#define REG_BMS_SHDW_CC_DATA0		0xA8
#define REG_BMS_CC_DATA_CTL		0x42
#define REG_BMS_CC_CLEAR_CTL		0x4

#define BMS_HOLD_OREG_DATA		BIT(0)
#define BMS_CLEAR_SHDW_CC		BIT(6)

#define BMS_CC_READING_RESOLUTION_N	542535
#define BMS_CC_READING_RESOLUTION_D	10000
#define BMS_CC_READING_TICKS		56
#define BMS_SLEEP_CLK_HZ		32764

#define SECONDS_PER_HOUR		3600
#define TEMPERATURE_COLS		5 // TODO Remove

/* lookup table for battery temperature -> full charge capacity conversion */
struct bms_fcc_lut {
	s8 temp_legend[TEMPERATURE_COLS];
	u32 lut[TEMPERATURE_COLS];
};

struct bms_device_info {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply_desc bat_desc;
	struct power_supply_battery_info info;
	struct bms_fcc_lut fcc_lut;
	struct iio_channel *adc;
	struct mutex bms_output_lock;
	u32 base_addr;

	int ocv_thr_irq;
	u32 ocv;
};

static bool between(int left, int right, int val)
{
	if (left <= val && val <= right)
		return true;

	if (left >= val && val >= right)
		return true;

	return false;
}

static int interpolate_capacity(int temp, u32 ocv,
				struct power_supply_battery_info *info)
{
	// let's assume temp=15°C, ocv=4000
	// i = temp capacity index
	// i2 = capacity index at temperature 'j' ('j-1 < temp < j')
	// i3 = capacity index at temperature 'j-1' ('j-1 < temp < j')
	// j = temperature index
	// pcj = percentage at temperature j
	// pcj_minus_one = percentage at temperature j-1
	int pcj_minus_one = 0, pcj = 0, i2 = 0, i3 = 0, i, j;

	// find the `j` index of temperature which is the next highest to `temp` (e.g. actual 15°C -> 25°C)
	for (j = 0; j < POWER_SUPPLY_OCV_TEMP_MAX; j++)
		if (temp <= info->ocv_temp[j])
			break;

	// Just debug print our data
// 	int g;
// 	for (g = 0; g < POWER_SUPPLY_OCV_TEMP_MAX; g++) {
// 		if(info->ocv_table_size[g] == -EINVAL) {
// 			printk("reached -EINVAL for ocv_table_size, breaking...\n");
// 			break;
// 		}
// 		printk("---- for index %d ----\n", g);
// 		printk("ocv_temp: %d\n", info->ocv_temp[g]);
// 		printk("ocv_table_size %d\n", info->ocv_table_size[g]);
// 		printk("ocv_table: %d , %d\n", info->ocv_table[g]->ocv, info->ocv_table[g]->capacity);
// 		int f;
// 		for (f = 0; f < info->ocv_table_size[g]; f++) {
// 			printk("table value: %d , %d\n", info->ocv_table[g][f].ocv, info->ocv_table[g][f].capacity);
// 		}
// 	}

	// TODO Maybe use power_supply_ocv2cap_simple or one of the helpers there?

	// if current ocv value is higher than the ocv value for 100% at the `j` temperature
	// return 100%
	if (ocv >= info->ocv_table[j][0].ocv)
		return info->ocv_table[j][0].capacity;
	// if current ocv value is lower than the ocv value at 0% at the `j` temperature
	// return 0%
	if (ocv <= info->ocv_table[j][info->ocv_table_size[j]-1].ocv)
		return info->ocv_table[j][info->ocv_table_size[j]-1].capacity;

	// same for temperature j-1
	if (ocv >= info->ocv_table[j-1][0].ocv)
		return info->ocv_table[j-1][0].capacity;
	if (ocv <= info->ocv_table[j-1][info->ocv_table_size[j-1]-1].ocv)
		return info->ocv_table[j-1][info->ocv_table_size[j-1]-1].capacity;

	// iterate through the ocv values at temperature j
	for (i = 0; i < info->ocv_table_size[j]-1; i++) {
		// if our ocv is between capacity `i` and capacity `i+1` (e.g. 75% & 70%) at temp `j`, set i2
		if (between(info->ocv_table[j][i].ocv,
			    info->ocv_table[j][i+1].ocv, ocv)) {
			i2 = i;
			break;
		}
	}

	// iterate through the ocv values at temperature j-1
	for (i = 0; i < info->ocv_table_size[j-1]-1; i++) {
		// if our ocv is between capacity `i` and capacity `i+1` (e.g. 75% & 70%) at temp `j-1` (next lower temperature (e.g. 25°C -> 0°C), set i3
		if (between(info->ocv_table[j-1][i].ocv,
			    info->ocv_table[j-1][i+1].ocv, ocv)) {
			i3 = i;
			break;
		}
	}

	/* interpolate two ocv values */
	// interpolate between e.g. 4038 & 3996 (75% and 70% at 25°C) for temperature j
	// x = ocv ; y = cap%
	// point 0 = (4038|75)
	// point 1 = (3996|70)
	// find y (capacity%) value for ocv x = 4000 => 70.47%
	pcj = fixp_linear_interpolate(info->ocv_table[j][i2].ocv,
				      info->ocv_table[j][i2].capacity,
				      info->ocv_table[j][i2+1].ocv,
				      info->ocv_table[j][i2+1].capacity,
				      ocv);

	// interpolate between e.g. 4051 & 3986 (80% and 75% at 0°C)
	// x = ocv ; y = cap%
	// point 0 = (4051|80)
	// point 1 = (3986|75)
	// find y (capacity%) value for ocv x = 4000 => 76.07%
	pcj_minus_one = fixp_linear_interpolate(info->ocv_table[j-1][i3].ocv,
						info->ocv_table[j-1][i3].capacity,
						info->ocv_table[j-1][i3+1].ocv,
						info->ocv_table[j-1][i3+1].capacity,
						ocv);

	/* interpolate them with the battery temperature */
	// x = temp°C ; y = ocv
	// point 0 = (0|pcj_minus_one=76)
	// point 1 = (25|pcj=70)
	// find percentage for temp x = 15°C => 72%
	return fixp_linear_interpolate(info->ocv_temp[j-1],
				       pcj_minus_one,
				       info->ocv_temp[j],
				       pcj,
				       temp);
}

static int interpolate_fcc(int temp, struct bms_fcc_lut *fcc_lut)
{
	int i;

	// find the next highest temperature to the current one
	for (i = 0; i < TEMPERATURE_COLS; i++)
		if (temp <= fcc_lut->temp_legend[i])
			break;

	// x = temp°C ; y = max capacity in mAh
	// point 0 = (0|2396)
	// point 1 = (25|2404)
	// find fcc for temp x = 15°C => 2400.8 mAh
	return fixp_linear_interpolate(fcc_lut->temp_legend[i-1],
			     fcc_lut->lut[i-1],
			     fcc_lut->temp_legend[i],
			     fcc_lut->lut[i],
			     temp);
}

static int bms_lock_output_data(struct bms_device_info *di)
{
	int ret;

	ret = regmap_update_bits(di->regmap, di->base_addr +
				 REG_BMS_CC_DATA_CTL,
				 BMS_HOLD_OREG_DATA, BMS_HOLD_OREG_DATA);
	if (ret) {
		dev_err(di->dev, "failed to lock bms output: %d\n", ret);
		return ret;
	}

	/*
	 * Sleep for at least 100 microseconds here to make sure
	 * there has been at least three cycles of the sleep clock
	 * so that the registers are correctly locked.
	 */
	usleep_range(100, 1000);

	return 0;
}

static int bms_unlock_output_data(struct bms_device_info *di)
{
	int ret;

	ret = regmap_update_bits(di->regmap, di->base_addr +
				 REG_BMS_CC_DATA_CTL,
				 BMS_HOLD_OREG_DATA, 0);
	if (ret) {
		dev_err(di->dev, "failed to unlock bms output: %d\n", ret);
		return ret;
	}

	return 0;
}

static int bms_read_ocv(struct bms_device_info *di, u32 *ocv)
{
	int ret;
	u16 read_ocv;

	mutex_lock(&di->bms_output_lock);

	ret = bms_lock_output_data(di);
	if (ret)
		goto err_lock;

	ret = regmap_bulk_read(di->regmap, di->base_addr +
			       REG_BMS_OCV_FOR_SOC_DATA0, &read_ocv, 2);
	if (ret) {
		dev_err(di->dev, "open circuit voltage read failed: %d\n", ret);
		goto err_read;
	}

	/* read_ocv has to be divided by 10 to result in millivolt */
	dev_dbg(di->dev, "read open circuit voltage of: %d mV\n", read_ocv / 10);

	/* convert read_ocv to microvolt */
	*ocv = read_ocv * 100;

err_read:
	bms_unlock_output_data(di);

err_lock:
	mutex_unlock(&di->bms_output_lock);

	return ret;
}

static int bms_read_cc(struct bms_device_info *di, s64 *cc_uah)
{
	int ret;
	s64 cc_raw_s36, cc_raw, cc_uv, cc_pvh;

	mutex_lock(&di->bms_output_lock);

	ret = bms_lock_output_data(di);
	if (ret)
		goto err_lock;

	ret = regmap_bulk_read(di->regmap, di->base_addr +
			       REG_BMS_SHDW_CC_DATA0,
			       &cc_raw_s36, 5);
	if (ret) {
		dev_err(di->dev, "coulomb counter read failed: %d\n", ret);
		goto err_read;
	}

	ret = bms_unlock_output_data(di);
	if (ret)
		goto err_lock;

	mutex_unlock(&di->bms_output_lock);

	cc_raw = sign_extend32(cc_raw_s36, 28);

	/* convert raw value to µV */
	cc_uv = div_s64(cc_raw * BMS_CC_READING_RESOLUTION_N,
			BMS_CC_READING_RESOLUTION_D);

	/* convert µV to picovolt hours */
	cc_pvh = div_s64(cc_uv * BMS_CC_READING_TICKS * 100000,
			 BMS_SLEEP_CLK_HZ * SECONDS_PER_HOUR);

	/* divide by impedance */
	*cc_uah = div_s64(cc_pvh, 10000);

	dev_dbg(di->dev, "read coulomb counter value of: %lld uAh\n", *cc_uah);

	return 0;

err_read:
	bms_unlock_output_data(di);

err_lock:
	mutex_unlock(&di->bms_output_lock);

	return ret;
}

static void bms_reset_cc(struct bms_device_info *di)
{
	int ret;

	mutex_lock(&di->bms_output_lock);

	ret = regmap_update_bits(di->regmap, di->base_addr +
				 REG_BMS_CC_CLEAR_CTL,
				 BMS_CLEAR_SHDW_CC,
				 BMS_CLEAR_SHDW_CC);
	if (ret) {
		dev_err(di->dev, "coulomb counter reset failed: %d\n", ret);
		goto err_lock;
	}

	/* wait at least three sleep cycles for cc to reset */
	usleep_range(100, 1000);

	ret = regmap_update_bits(di->regmap, di->base_addr +
				 REG_BMS_CC_CLEAR_CTL,
				 BMS_CLEAR_SHDW_CC, 0);
	if (ret)
		dev_err(di->dev, "coulomb counter re-enable failed: %d\n", ret);

err_lock:
	mutex_unlock(&di->bms_output_lock);
}

static int bms_calculate_capacity(struct bms_device_info *di, int *capacity)
{
	unsigned long fcc;
	int ret, temp, ocv_capacity, temp_degc;
	s64 cc = 0;

	ret = iio_read_channel_raw(di->adc, &temp);
	if (ret < 0) {
		dev_err(di->dev, "failed to read temperature: %d\n", ret);
		return ret;
	}

	temp_degc = DIV_ROUND_CLOSEST(temp, 1000);

	dev_dbg(di->dev, "read temperature of: %d °C\n", temp_degc);

	// read uAh (maybe 1000000 uAh - 1000 mAh?)
	ret = bms_read_cc(di, &cc);
	if (ret < 0) {
		dev_err(di->dev, "failed to read coulomb counter: %d\n", ret);
		return ret;
	}

	/* interpolate capacity (in %) from open circuit voltage */
	// get 'perfect' percentage for ocv at temperature according to table => 72%
	// |    -20°C      |      0°C      |     25°C      |     40°C      |     60°C      |
	// | ---- | ------ | ---- | ------ | ---- | ------ | ---- | ------ | ---- | ------ |
	// | 100% | 4334mV | 100% | 4332mV | 100% | 4327mV | 100% | 4324mV | 100% | 4316mV |
	// |  95% | 4184mV |  95% | 4234mV |  95% | 4249mV |  95% | 4250mV |  95% | 4246mV |
	// |  90% | 4094mV |  90% | 4162mV |  90% | 4186mV |  90% | 4188mV |  90% | 4186mV |
	// |  85% | 4020mV |  85% | 4100mV |  85% | 4128mV |  85% | 4132mV |  85% | 4132mV |
	// |  ... | ...... |  ... | ...... |  ... | ...... |  ... | ...... |  ... | ...... |
	// |   1% | 3040mV |   1% | 3120mV |   1% | 3160mV |   1% | 3155mV |   1% | 3138mV |
	// |   0% | 3000mV |   0% | 3012mV |   0% | 3000mV |   0% | 3000mV |   0% | 3005mV |
	ocv_capacity = interpolate_capacity(temp_degc, di->ocv,
					    &di->info);

	/* interpolate the full charge capacity (in μAh) from temperature */
	// get the capacity in uAh at 100% for our temperature => 2400 mAh / 2400800 μAh
	fcc = interpolate_fcc(temp_degc, &di->fcc_lut);

	/* append coulomb counter to capacity */
	// (2400800 μAh * 72%) / 100 = 1728576 μAh = 1728 mAh
	*capacity = DIV_ROUND_CLOSEST(fcc * ocv_capacity, 100);
	// (1728576 μAh - $cc μAh) * 100 / 2400800 μAh => 30.34%
	*capacity = div_s64((*capacity - cc) * 100, fcc);

	return 0;
}

/*
 * Return power_supply property
 */
static int bms_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct bms_device_info *di = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bms_calculate_capacity(di, &val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (val->intval == INT_MAX || val->intval == INT_MIN)
		ret = -EINVAL;

	return ret;
}

static enum power_supply_property bms_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
};

static irqreturn_t bms_ocv_thr_irq_handler(int irq, void *dev_id)
{
	struct bms_device_info *di = dev_id;

	if (bms_read_ocv(di, &di->ocv) < 0)
		return IRQ_HANDLED;

	bms_reset_cc(di);
	return IRQ_HANDLED;
}

static int bms_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct bms_device_info *di;
	struct power_supply *bat;
	int ret;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;

	di->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!di->regmap) {
		dev_err(di->dev, "Unable to get regmap\n");
		return -EINVAL;
	}

	di->adc = devm_iio_channel_get(&pdev->dev, "temp");
	if (IS_ERR(di->adc))
		return PTR_ERR(di->adc);

	ret = of_property_read_u32(di->dev->of_node, "reg", &di->base_addr);
	if (ret < 0)
		return ret;

	ret = of_property_read_u8_array(di->dev->of_node,
						 "qcom,fcc-temp-legend-celsius",
						 (u8 *)di->fcc_lut.temp_legend,
						 TEMPERATURE_COLS);
	if (ret < 0) {
		dev_err(di->dev, "no full charge capacity temperature legend found\n");
		return ret;
	}

	ret = of_property_read_u32_array(di->dev->of_node,
						  "qcom,fcc-lut-microamp-hours",
						  di->fcc_lut.lut,
						  TEMPERATURE_COLS);
	if (ret < 0) {
		dev_err(di->dev, "no full charge capacity lut array found\n");
		return ret;
	}

	ret = bms_read_ocv(di, &di->ocv);
	if (ret < 0) {
		dev_err(di->dev, "failed to read initial open circuit voltage: %d\n",
			ret);
		return ret;
	}

	mutex_init(&di->bms_output_lock);

	di->ocv_thr_irq = platform_get_irq_byname(pdev, "ocv_thr");

	ret = devm_request_threaded_irq(di->dev, di->ocv_thr_irq, NULL,
					bms_ocv_thr_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					pdev->name, di);
	if (ret < 0) {
		dev_err(di->dev, "failed to request handler for open circuit voltage threshold IRQ\n");
		return ret;
	}

	di->bat_desc.name = "bms";
	di->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat_desc.properties = bms_props;
	di->bat_desc.num_properties = ARRAY_SIZE(bms_props);
	di->bat_desc.get_property = bms_get_property;

	psy_cfg.drv_data = di;
	psy_cfg.of_node = di->dev->of_node;

	bat = devm_power_supply_register(di->dev, &di->bat_desc, &psy_cfg);
	if (IS_ERR(bat)) {
		dev_err(di->dev, "failed to register battery: %ld\n", PTR_ERR(bat));
		return PTR_ERR(bat);
	}

	ret = power_supply_get_battery_info(bat, &di->info);
	if (ret < 0) {
		dev_err(di->dev, "failed to get battery info: %d\n", ret);
		return ret;
	}
	// Validate that ocv_temp & ocv_table was populated
	if (di->info.ocv_table_size[0] == -EINVAL || di->info.ocv_table_size[1] == -EINVAL) {
		dev_err(di->dev, "failed to get ocv table: %d\n", ret);
		return ret; // FIXME
	}

	return PTR_ERR_OR_ZERO(bat);
}

static const struct of_device_id bms_of_match[] = {
	{ .compatible = "qcom,pm8941-bms", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bms_of_match);

static struct platform_driver bms_driver = {
	.probe = bms_probe,
	.driver = {
		.name = "qcom-bms",
		.of_match_table = of_match_ptr(bms_of_match),
	},
};
module_platform_driver(bms_driver);

MODULE_AUTHOR("Craig Tatlor <ctatlor97@gmail.com>");
MODULE_DESCRIPTION("Qualcomm BMS driver");
MODULE_LICENSE("GPL");
