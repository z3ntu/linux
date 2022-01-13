// SPDX-License-Identifier: GPL-2-Only
/* Copyright (c) 2022, The Linux Foundation. All rights reserved. */

#include <linux/device.h>
#include <linux/extcon.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define DRIVER_NAME "sm5708-charger"

#define mA(mAmp) ((mAmp) * 1000L)
#define mV(mVolt) ((mVolt) * 1000L)

#define SM5708_REG_INT(n)		(0 + (n))
#define SM5708_REG_INTMASK(n)   	(4 + (n))
#define SM5708_REG_STATUS(n)		(8 + (n))

#define SM5708_STATUS_VBUSPOK		BIT(0)
#define SM5708_STATUS_VBUSUVLO		BIT(1)
#define SM5708_STATUS_VBUSOVP		BIT(2)
#define SM5708_STATUS_VBUSLIMIT		BIT(3)
#define SM5708_STATUS_AICL		BIT(8)
#define SM5708_STATUS_BATOVP		BIT(9)
#define SM5708_STATUS_NOBAT		BIT(10)
#define SM5708_STATUS_CHGON		BIT(11)
#define SM5708_STATUS_Q4FULLON		BIT(12)
#define SM5708_STATUS_TOPOFF		BIT(13)
#define SM5708_STATUS_DONE		BIT(14)
#define SM5708_STATUS_WDTMROFF		BIT(15)
#define SM5708_STATUS_THEMREG		BIT(16)
#define SM5708_STATUS_THEMSHDN		BIT(17)
#define SM5708_STATUS_OTGFAIL		BIT(18)
#define SM5708_STATUS_DISLIMIT		BIT(19)
#define SM5708_STATUS_PRETMROFF		BIT(20)
#define SM5708_STATUS_FASTTMROFF	BIT(21)
#define SM5708_STATUS_LOWBATT		BIT(22)
#define SM5708_STATUS_NENQ4		BIT(23)
#define SM5708_STATUS_FLED1SHORT	BIT(24)
#define SM5708_STATUS_FLED1OPEN		BIT(25)
#define SM5708_STATUS_FLED2SHORT	BIT(26)
#define SM5708_STATUS_FLED2OPEN		BIT(27)
#define SM5708_STATUS_BOOSTPOK_NG	BIT(28)
#define SM5708_STATUS_BOOSTPOK		BIT(29)
#define SM5708_STATUS_ABSTMR1OFF	BIT(30)
#define SM5708_STATUS_SBPS		BIT(31)


#define SM5708_REG_CNTL			0x0c
#define REG_CNTL_MODE_MASK		GENMASK(2, 0)
#define REG_CNTL_MODE_SUSPEND		0
#define REG_CNTL_MODE_FACTORY		1
#define REG_CNTL_MODE_CHG_OFF		4
#define REG_CNTL_MODE_CHG_ON		5
#define REG_CNTL_MODE_FLASH_BOOST	6
#define REG_CNTL_MODE_USB_OTG		7

#define SM5708_REG_VBUSCNTL		0x0d
#define REG_VBUSCNTL_INP_CURR_MASK	GENMASK(6, 0)
#define REG_VBUSCNTL_INP_CURR_RANGE	RANGE(mA(100), mA(25), mA(3275))
#define SM5708_REG_CHGCNTL1     	0x0f
#define SM5708_REG_CHGCNTL2		0x10
#define REG_CHGCNTL2_CHG_CURR_MASK	GENMASK(5, 0)
#define REG_CHGCNTL2_CHG_CURR_RANGE	RANGE(mA(100), mA(50), mA(3250))
#define SM5708_REG_CHGCNTL3		0x12
#define REG_CHGCNTL3_FLOAT_VOLT_MASK	GENMASK(5, 0)
#define REG_CHGCNTL3_FLOAT_VOLT_RANGE	RANGE(mV(3990), mV(10), mV(4620))
#define REG_CHGCNTL3_AUTOSTOP_EN	BIT(6)
#define SM5708_REG_CHGCNTL4		0x13
#define REG_CHGCNTL4_TOPOFF_MASK	GENMASK(3, 0)
#define REG_CHGCNTL4_TOPOFF_RANGE	RANGE(mA(100), mA(25), mA(475))
#define REG_CHGCNTL4_FREQ_MASK		GENMASK(5, 4)
#define REG_CHGCNTL4_FREQ_VAL_3000KHZ	(0 << 4)
#define REG_CHGCNTL4_FREQ_VAL_2400KHZ	(1 << 4)
#define REG_CHGCNTL4_FREQ_VAL_1500KHZ	(2 << 4)
#define REG_CHGCNTL4_FREQ_VAL_1800KHZ	(3 << 4)
#define SM5708_REG_CHGCNTL5		0x14
#define REG_CHGCNTL5_IQ3LIMIT_MASK	GENMASK(1, 0)
#define REG_CHGCNTL5_IQ3LIMIT_VAL_2000MA (0 << 0)
#define REG_CHGCNTL5_IQ3LIMIT_VAL_2800MA (1 << 0)
#define REG_CHGCNTL5_IQ3LIMIT_VAL_3500MA (2 << 0)
#define REG_CHGCNTL5_IQ3LIMIT_VAL_4000MA (3 << 0)
#define REG_CHGCNTL5_OTG_CURR_MASK	GENMASK(3, 2)
#define REG_CHGCNTL5_OTG_CURR_VAL_500MA	(0 << 2)
#define REG_CHGCNTL5_OTG_CURR_VAL_700MA	(1 << 2)
#define REG_CHGCNTL5_OTG_CURR_VAL_900MA	(2 << 2)
#define REG_CHGCNTL5_OTG_CURR_VAL_1P5A	(3 << 2)
#define SM5708_REG_CHGCNTL6		0x15
#define REG_CHGCNTL6_AICL_EN		BIT(5)
#define REG_CHGCNTL6_AICL_THRESH_MASK	GENMASK(7, 6)
#define REG_CHGCNTL6_AICL_THRESH_RANGE	RANGE(mV(4500), mV(100), mV(4800))
#define REG_CHGCNTL6_AUTOSET_EN		BIT(1)
#define SM5708_REG_CHGCNTL7		0x16
#define REG_CHGCNTL7_TOPOFFTMR_MASK	GENMASK(4, 3)
#define REG_CHGCNTL7_TOPOFFTMR_VAL_10M	(0 << 3)
#define REG_CHGCNTL7_TOPOFFTMR_VAL_20M	(1 << 3)
#define REG_CHGCNTL7_TOPOFFTMR_VAL_30M	(2 << 3)
#define REG_CHGCNTL7_TOPOFFTMR_VAL_45M	(3 << 3)

#define SM5708_REG_FLEDCNTL5		0x1f
#define SM5708_REG_FLEDCNTL6		0x20
#define REG_FLEDCNTL6_BOOST_VOLT_MASK	GENMASK(3, 0)
#define REG_FLEDCNTL6_BOOST_VOLT_RANGE	RANGE(mV(4000), mV(100), mV(5100))
#define SM5708_REG_DEVICEID		0x37

#define RANGE(start, step, end) (&(const struct field_range){ start, step, end })

#define FIELD_PREP_RANGE_CLAMP(field, value) \
	FIELD_PREP(field##_MASK, __regfield_encode_range(field##_RANGE, value))

#define FIELD_GET_RANGE(field, value) \
	__regfield_decode_range(field##_RANGE, FIELD_GET(field##_MASK, value))

struct field_range {
	int start;
	int step;
	int end;
};

static int __regfield_encode_range(const struct field_range *r, int val)
{
	return (clamp(val, r->start, r->end) - r->start) / r->step;
}

static int __regfield_decode_range(const struct field_range *r, int val)
{
	return r->start + val * r->step;
}

static int range_check_value(const struct field_range *r, int val)
{
	return (val >= r->start) && (val <= r->end);
}

struct sm5708_vreg {
	struct sm5708_charger *charger;
	unsigned voltage;
	unsigned load;
	unsigned enabled;
};

struct sm5708_charger {
	struct device *dev;
	struct extcon_dev *extcon;
	struct notifier_block extcon_nb;
	struct power_supply *psy;
	struct power_supply_battery_info *info;
	struct gpio_desc *charge_en_gpio;
	struct regmap *regmap;

#define MODE_USBOTG		(1 << 0)
#define MODE_FLASH_BOOST	(1 << 1)
#define MODE_CHARGE		(1 << 2)

	unsigned short chip_mode;
	struct mutex mode_lock;
	bool fast_charge;

	struct work_struct otg_fail_work;
	struct delayed_work otg_work;

	u32 status;

	unsigned int input_current_max_ua;
};

static enum power_supply_property sm5708_properties[] = {
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static int sm5708_switch_mode(struct sm5708_charger* charger, unsigned short mode, bool enable)
{
	int boost_output = mV(4500);
	int otg_current = REG_CHGCNTL5_OTG_CURR_VAL_500MA;
	int op_mode = REG_CNTL_MODE_CHG_ON;
	unsigned short new_mode;
	int ret;

	mutex_lock(&charger->mode_lock);

	if (enable)
		new_mode = charger->chip_mode | mode;
	else
		new_mode = charger->chip_mode & ~mode;

	if (new_mode == charger->chip_mode)
		goto out_unlock;

	switch(new_mode & ~MODE_CHARGE) {
	case MODE_FLASH_BOOST | MODE_USBOTG:
		otg_current = REG_CHGCNTL5_OTG_CURR_VAL_900MA;
		fallthrough;
	case MODE_FLASH_BOOST:
		op_mode = REG_CNTL_MODE_FLASH_BOOST;
		break;
	case MODE_USBOTG:
		otg_current = REG_CHGCNTL5_OTG_CURR_VAL_900MA;
		boost_output = mV(5100);
		op_mode = REG_CNTL_MODE_USB_OTG;
		break;
	default:
		break;
	}

	if (op_mode == REG_CNTL_MODE_CHG_ON && !(new_mode & MODE_CHARGE))
		op_mode = REG_CNTL_MODE_SUSPEND;

	ret = ret ?: regmap_update_bits(charger->regmap, SM5708_REG_FLEDCNTL6,
			REG_FLEDCNTL6_BOOST_VOLT_MASK,
			FIELD_PREP_RANGE_CLAMP(REG_FLEDCNTL6_BOOST_VOLT, boost_output));

	ret = ret ?: regmap_update_bits(charger->regmap, SM5708_REG_CHGCNTL5,
			REG_CHGCNTL5_OTG_CURR_MASK, otg_current);
	
	ret = ret ?: regmap_update_bits(charger->regmap, SM5708_REG_CNTL,
			REG_CNTL_MODE_MASK, op_mode);

	if (!ret)
		gpiod_set_value(charger->charge_en_gpio, !!(new_mode & MODE_CHARGE));

	charger->chip_mode = new_mode;

out_unlock:
	mutex_unlock(&charger->mode_lock);

	return ret;
}

static int sm5708_regulator_enable(struct regulator_dev *rdev)
{
	return sm5708_switch_mode(rdev_get_drvdata(rdev), rdev_get_id(rdev), true);
}

static int sm5708_regulator_disable(struct regulator_dev *rdev)
{
	return sm5708_switch_mode(rdev_get_drvdata(rdev), rdev_get_id(rdev), false);
}

static int sm5708_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct sm5708_charger *charger = rdev_get_drvdata(rdev);

	return !!(charger->chip_mode & rdev_get_id(rdev));
}

static const struct regulator_ops sm5708_vreg_ops = {
	.enable			= sm5708_regulator_enable,
	.disable		= sm5708_regulator_disable,
	.is_enabled		= sm5708_regulator_is_enabled,
};

static const struct regulator_desc sm5708_regulators[] = {
	{
		.id = MODE_USBOTG,
		.name = "otg-vbus",
		.of_match = "otg-vbus",
		.ops = &sm5708_vreg_ops,
		.supply_name = "vin",
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.id = MODE_FLASH_BOOST,
		.name = "flash-boost",
		.of_match = "flash-boost",
		.ops = &sm5708_vreg_ops,
		.supply_name = "vin",
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int sm5708_read32(struct sm5708_charger *charger, unsigned int reg, u32 *out)
{
	unsigned char values[4];
	int ret;

	ret = regmap_bulk_read(charger->regmap, reg, &values, 4);
	if (ret < 0)
		return ret;

	*out = (u32) values[0] | (u32) values[1] << 8 |
		(u32) values[2] << 16 | (u32) values[3] << 24;

	return 0;
}

static int sm5708_read_status(struct sm5708_charger *charger)
{
	return sm5708_read32(charger, SM5708_REG_STATUS(0), &charger->status);
}

static int sm5708_psy_status(struct sm5708_charger *charger)
{
	if (charger->status & (SM5708_STATUS_TOPOFF | SM5708_STATUS_DONE))
		return POWER_SUPPLY_STATUS_FULL;
	if ((charger->status & SM5708_STATUS_CHGON) &&
	    (charger->status & SM5708_STATUS_VBUSPOK))
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int sm5708_psy_charge_type(struct sm5708_charger *charger)
{
	switch (sm5708_psy_status(charger)) {
	case POWER_SUPPLY_STATUS_FULL:
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case POWER_SUPPLY_STATUS_CHARGING:
		return charger->fast_charge ?
			POWER_SUPPLY_CHARGE_TYPE_FAST :
			POWER_SUPPLY_CHARGE_TYPE_STANDARD;
	default:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}
}

static int sm5708_psy_health(struct sm5708_charger *charger)
{

	if (charger->status & SM5708_STATUS_VBUSOVP)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	if (charger->status & SM5708_STATUS_VBUSUVLO)
		return POWER_SUPPLY_HEALTH_DEAD;
	if (charger->status & SM5708_STATUS_VBUSPOK)
		return POWER_SUPPLY_HEALTH_GOOD;

	return POWER_SUPPLY_HEALTH_UNKNOWN;
}

static int sm5708_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct sm5708_charger *charger = power_supply_get_drvdata(psy);
	unsigned int regval;
	int ret;

	ret = sm5708_read_status(charger);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = charger->chip_mode & MODE_CHARGE ?
			POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
			POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!(charger->status & SM5708_STATUS_VBUSPOK);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !(charger->status & SM5708_STATUS_NOBAT);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sm5708_psy_status(charger);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = sm5708_psy_charge_type(charger);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = sm5708_psy_health(charger);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = charger->info->constant_charge_current_max_ua;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = charger->input_current_max_ua;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = regmap_read(charger->regmap, SM5708_REG_CHGCNTL4, &regval);
		if (ret)
			return ret;

		val->intval = FIELD_GET_RANGE(REG_CHGCNTL4_TOPOFF, regval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = regmap_read(charger->regmap, SM5708_REG_CHGCNTL2, &regval);
		if (ret)
			return ret;

		val->intval = FIELD_GET_RANGE(REG_CHGCNTL2_CHG_CURR, regval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = regmap_read(charger->regmap, SM5708_REG_VBUSCNTL, &regval);
		if (ret)
			return ret;

		val->intval = FIELD_GET_RANGE(REG_VBUSCNTL_INP_CURR, regval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		ret = regmap_read(charger->regmap, SM5708_REG_CHGCNTL3, &regval);
		if (ret)
			return ret;

		val->intval = FIELD_GET_RANGE(REG_CHGCNTL3_FLOAT_VOLT, regval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sm5708_set_property(struct power_supply *psy,
				enum power_supply_property psp, const union power_supply_propval *val)
{
	struct sm5708_charger *charger = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval && (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE))
			return -EINVAL;

		sm5708_switch_mode(charger, MODE_CHARGE, !val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return regmap_update_bits(charger->regmap, SM5708_REG_CHGCNTL4,
			REG_CHGCNTL4_TOPOFF_MASK,
			FIELD_PREP_RANGE_CLAMP(REG_CHGCNTL4_TOPOFF, val->intval));
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return regmap_update_bits(charger->regmap, SM5708_REG_CHGCNTL2,
			REG_CHGCNTL2_CHG_CURR_MASK,
			FIELD_PREP_RANGE_CLAMP(REG_CHGCNTL2_CHG_CURR, val->intval));
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		return regmap_update_bits(charger->regmap, SM5708_REG_VBUSCNTL,
			REG_VBUSCNTL_INP_CURR_MASK,
			FIELD_PREP_RANGE_CLAMP(REG_VBUSCNTL_INP_CURR, val->intval));
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return regmap_update_bits(charger->regmap, SM5708_REG_CHGCNTL3,
			REG_CHGCNTL3_FLOAT_VOLT_MASK,
			FIELD_PREP_RANGE_CLAMP(REG_CHGCNTL3_FLOAT_VOLT, val->intval));
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t sm5708_irq_handler(int irq, void *data)
{
	struct sm5708_charger *charger = data;
	static u32 intreg, maskreg, statusreg;

	sm5708_read32(charger, SM5708_REG_INT(0), &intreg);
	sm5708_read32(charger, SM5708_REG_INTMASK(0), &maskreg);
	sm5708_read32(charger, SM5708_REG_STATUS(0), &statusreg);

	dev_info(charger->dev, "INT %x MASK %x STATUS %x\n", intreg, maskreg, statusreg);

	return IRQ_HANDLED;
}

static void sm5708_charger_initialize(struct sm5708_charger *charger)
{
	struct regmap *rmap = charger->regmap;
	union power_supply_propval prop;

	regmap_update_bits(rmap, SM5708_REG_CHGCNTL4, REG_CHGCNTL4_FREQ_MASK,
			REG_CHGCNTL4_FREQ_VAL_1500KHZ);
	regmap_update_bits(rmap, SM5708_REG_CHGCNTL5, REG_CHGCNTL5_IQ3LIMIT_MASK,
			REG_CHGCNTL5_IQ3LIMIT_VAL_3500MA);
	regmap_update_bits(rmap, SM5708_REG_CHGCNTL7, REG_CHGCNTL7_TOPOFFTMR_MASK,
			REG_CHGCNTL7_TOPOFFTMR_VAL_45M);
	regmap_update_bits(rmap, SM5708_REG_CHGCNTL6, REG_CHGCNTL6_AICL_THRESH_MASK |
			REG_CHGCNTL6_AICL_EN | REG_CHGCNTL6_AUTOSET_EN,
			FIELD_PREP_RANGE_CLAMP(REG_CHGCNTL6_AICL_THRESH, mA(4500)) |
			REG_CHGCNTL6_AICL_EN);
	regmap_set_bits(rmap, SM5708_REG_CHGCNTL3, REG_CHGCNTL3_AUTOSTOP_EN);

	prop.intval = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR, &prop);

	prop.intval = charger->input_current_max_ua;
	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_CURRENT_MAX, &prop);

	prop.intval = charger->info->constant_charge_current_max_ua;
	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &prop);

	prop.intval = charger->info->charge_term_current_ua;
	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &prop);

	prop.intval = charger->info->voltage_max_design_uv;
	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &prop);
}

static int sm5708_extcon_notifier(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct sm5708_charger *charger = container_of(nb, struct sm5708_charger, extcon_nb);
	union power_supply_propval inCurr, chgCurr;

	charger->fast_charge = extcon_get_state(charger->extcon, EXTCON_CHG_USB_DCP);

	inCurr.intval = charger->fast_charge ? charger->input_current_max_ua : mA(500);
	chgCurr.intval = charger->fast_charge ? charger->info->constant_charge_current_max_ua : mA(500);

	power_supply_set_property(charger->psy, POWER_SUPPLY_PROP_CURRENT_AVG, &inCurr);
	power_supply_set_property(charger->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &chgCurr);

	return NOTIFY_DONE;
}

static int sm5708_property_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR;
}

static const struct power_supply_desc sm5708_charger_desc = {
	.name		= DRIVER_NAME,
	.type		= POWER_SUPPLY_TYPE_USB_DCP,
	.properties	= sm5708_properties,
	.num_properties	= ARRAY_SIZE(sm5708_properties),
	.get_property	= sm5708_get_property,
	.set_property	= sm5708_set_property,
	.property_is_writeable = sm5708_property_is_writeable,
};

static int sm5708_charger_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_config = { 0 };
	struct device *dev = &pdev->dev;
	struct sm5708_charger *charger;
	struct regulator_dev *rdev;
	unsigned int regval;
	int ret, irq, i;

	charger = devm_kzalloc(dev, sizeof(*charger), GFP_KERNEL);
	if (IS_ERR_OR_NULL(charger))
		return -ENOMEM;

	mutex_init(&charger->mode_lock);

	charger->regmap = dev_get_regmap(dev->parent, NULL);
	charger->dev = dev;
	charger->extcon_nb.notifier_call = sm5708_extcon_notifier;

	//INIT_WORK(&charger->otg_fail_work, sm5708_otg_fail_work);

	dev_set_drvdata(dev, charger);
	psy_config.drv_data = charger;

	ret = of_property_read_u32(dev->of_node, "input-current-max-microamp",
			&charger->input_current_max_ua);

	if (ret || !range_check_value(REG_VBUSCNTL_INP_CURR_RANGE,
				charger->input_current_max_ua))
		return ret ?: -ERANGE;

	charger->charge_en_gpio = devm_gpiod_get_optional(dev, "charge-enable", GPIOD_OUT_LOW);
	if (IS_ERR(charger->charge_en_gpio))
		return dev_err_probe(dev, PTR_ERR(charger->charge_en_gpio),
				"failed to get charge-enable gpio\n");

	charger->extcon = extcon_get_edev_by_phandle(charger->dev, 0);
	if (IS_ERR(charger->extcon))
		return PTR_ERR(charger->extcon);

	charger->psy = devm_power_supply_register(&pdev->dev,
			&sm5708_charger_desc, &psy_config);
	if (IS_ERR(charger->psy))
		return dev_err_probe(dev, PTR_ERR(charger->psy),
				"failed to register power supply\n");;

	ret = power_supply_get_battery_info(charger->psy, &charger->info);
	if (ret)
		return dev_err_probe(dev, -EINVAL, "failed to get battery info\n");

	if (!range_check_value(REG_CHGCNTL2_CHG_CURR_RANGE, charger->info->constant_charge_current_max_ua) ||
	    !range_check_value(REG_CHGCNTL3_FLOAT_VOLT_RANGE, charger->info->voltage_max_design_uv) ||
	    !range_check_value(REG_CHGCNTL4_TOPOFF_RANGE, charger->info->charge_term_current_ua))
		return -ERANGE;

	ret = regmap_read(charger->regmap, SM5708_REG_DEVICEID, &regval);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device id\n");

	dev_info(charger->dev, "Device ID: %#04x\n", regval);

	sm5708_charger_initialize(charger);
	sm5708_extcon_notifier(&charger->extcon_nb, 0, NULL);

	irq = of_irq_get(dev->of_node, 0);
	if (!irq)
		return dev_err_probe(dev, -EINVAL, "interrupt is missing\n");

	ret = devm_request_threaded_irq(dev, irq, NULL, sm5708_irq_handler,
			IRQF_ONESHOT | IRQF_TRIGGER_LOW, DRIVER_NAME, charger);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq\n");

	ret = devm_extcon_register_notifier_all(charger->dev, charger->extcon,
			&charger->extcon_nb);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sm5708_regulators); i++ ) {
		struct regulator_config cfg = {
			.dev = dev,
			.driver_data = charger,
		};

		rdev = devm_regulator_register(dev, &sm5708_regulators[i], &cfg);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev), "failed to register regulators");
	}

	return 0;
}

static const struct of_device_id sm5708_charger_match[] = {
	{ .compatible = "siliconmitus,sm5708-charger" },
	{ },
};
MODULE_DEVICE_TABLE(of, sm5708_charger_match);

static struct platform_driver sm5708_charger_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= sm5708_charger_match,
	},
	.probe = sm5708_charger_probe,
};

module_platform_driver(sm5708_charger_driver);
MODULE_DESCRIPTION("Siliconmitus SM5708 Charger Driver");
MODULE_LICENSE("GPL v2");
