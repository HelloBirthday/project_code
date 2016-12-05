
/*
 * Regulator driver for eup3265 DCDC chip for rk32xx
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.

 * Based on eup3265.c that is work by zhangqing<zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#if 1
#define DBG_INFO(x...)	printk(KERN_INFO x)
#else
#define DBG_INFO(x...)
#endif
#define PM_CONTROL

#define EUP3265_SPEED 200*1000
#define eup3265_NUM_REGULATORS 1

struct eup3265 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	int irq_base;
	int chip_irq;
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	struct regmap *regmap;
};

struct eup3265_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};
struct eup3265_board {
	int irq;
	int irq_base;
	struct regulator_init_data *eup3265_init_data[eup3265_NUM_REGULATORS];
	struct device_node *of_node[eup3265_NUM_REGULATORS];
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};

struct eup3265_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

struct eup3265_platform_data {
	int ono;
	int num_regulators;
	struct eup3265_regulator_subdev *regulators;
	
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};
struct eup3265 *g_eup3265;

static int eup3265_reg_read(struct eup3265 *eup3265, u8 reg);
static int eup3265_set_bits(struct eup3265 *eup3265, u8 reg, u16 mask, u16 val);


#define EUP3265_BUCK1_SET_VOL_BASE 0x11
#define EUP3265_BUCK1_SLP_VOL_BASE 0x10
#define EUP3265_CONTR_REG1 0x13
#define EUP3265_COM_REGISTER  0X14


#define BUCK_VOL_MASK 0x7f
#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x7f

const static int buck_voltage_map[] = {
600000, 606250, 612500, 618750, 625000, 631250, 637500, 643750, 650000, 656250, 662500,
668750, 675000, 681250, 687500, 693750, 700000, 706250, 712500, 718750, 725000,
731250, 737500, 743750, 750000, 756250, 762500, 768750, 775000, 781250, 787500,
793750, 800000, 806250, 812500, 818750, 825000, 831250, 837500, 843750, 850000,
856250, 862500, 868750, 875000, 881250, 887500, 893750, 900000, 906250, 912500,
918750, 925000, 931250, 937500, 943750, 950000, 956250, 962500, 968750, 975000,
981250, 987500, 993750, 1000000, 1006250, 1012500, 1018750, 1025000, 1031250, 1037500,
1043750, 1050000, 1056250, 1062500, 1068750, 1075000, 1081250, 1087500, 1093750, 1100000,
1106250, 1112500, 1118750, 1125000, 1131250, 1137500, 1143750, 1150000, 1156250, 1162500,
1168750, 1175000, 1181250, 1187500, 1193750, 1200000, 1206250, 1212500, 1218750, 1225000,
1231250, 1237500, 1243750, 1250000, 1256250, 1262500, 1268750, 1275000, 1281250, 1287500,
1293750, 1300000, 1306250, 1312500, 1318750, 1325000, 1331250, 1337500, 1343750, 1350000,
1356250, 1362500, 1368750, 1375000, 1381250, 1387500, 1393750,
};

static int eup3265_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(buck_voltage_map))
		return -EINVAL;
	return  buck_voltage_map[index];
}
static int eup3265_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 val;
	u16 mask=0x80;	
	val = eup3265_reg_read(eup3265, EUP3265_BUCK1_SET_VOL_BASE);
	if (val < 0)
		return val;
	 val=val&~0x7f;
	if (val & mask)
		return 1;
	else
		return 0; 	
}
static int eup3265_dcdc_enable(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask=0x80;	

	return eup3265_set_bits(eup3265, EUP3265_BUCK1_SET_VOL_BASE, mask, 0x80);

}
static int eup3265_dcdc_disable(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask=0x80;
	 return eup3265_set_bits(eup3265, EUP3265_BUCK1_SET_VOL_BASE, mask, 0);
}
static int eup3265_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 reg = 0;
	int val;
	reg = eup3265_reg_read(eup3265,EUP3265_BUCK1_SET_VOL_BASE);
	reg &= BUCK_VOL_MASK;
	val = buck_voltage_map[reg];	
	return val;
}
static int eup3265_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;
	
	if (min_uV < vol_map[VOL_MIN_IDX] ||
	    min_uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= min_uV)
			break;
        }

	if (vol_map[val] > max_uV)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);

	ret = eup3265_set_bits(eup3265, EUP3265_BUCK1_SET_VOL_BASE ,BUCK_VOL_MASK, val);
	if(ret < 0)
		printk("###################WARNING:set voltage is error!voltage set is %d mv %d\n",vol_map[val],ret);
	
	return ret;
}

static unsigned int eup3265_dcdc_get_mode(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask = 0x40;
	u16 val;
	val = eup3265_reg_read(eup3265, EUP3265_COM_REGISTER);
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;

}
static int eup3265_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask = 0x40;

	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return eup3265_set_bits(eup3265,EUP3265_COM_REGISTER, mask, mask);
	case REGULATOR_MODE_NORMAL:
		return eup3265_set_bits(eup3265, EUP3265_COM_REGISTER, mask, 0);
	default:
		printk("error:dcdc_eup3265 only auto and pwm mode\n");
		return -EINVAL;
	}
}
static int eup3265_dcdc_set_voltage_time_sel(struct regulator_dev *dev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = eup3265_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = eup3265_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*4, 10000);
}
static int eup3265_dcdc_suspend_enable(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask=0x80;	
	
	return eup3265_set_bits(eup3265, EUP3265_BUCK1_SLP_VOL_BASE, mask, 0x80);

}
static int eup3265_dcdc_suspend_disable(struct regulator_dev *dev)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask=0x80;
	 return eup3265_set_bits(eup3265, EUP3265_BUCK1_SLP_VOL_BASE, mask, 0);
}
static int eup3265_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;
	
	if (uV < vol_map[VOL_MIN_IDX] ||
	    uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= uV)
			break;
        }

	if (vol_map[val] > uV)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);
	ret = eup3265_set_bits(eup3265,EUP3265_BUCK1_SLP_VOL_BASE ,BUCK_VOL_MASK, val);	
	return ret;
}


static int eup3265_dcdc_set_suspend_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct eup3265 *eup3265 = rdev_get_drvdata(dev);
	u16 mask = 0x40;

	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return eup3265_set_bits(eup3265,EUP3265_COM_REGISTER, mask, mask);
	case REGULATOR_MODE_NORMAL:
		return eup3265_set_bits(eup3265, EUP3265_COM_REGISTER, mask, 0);
	default:
		printk("error:dcdc_eup3265 only auto and pwm mode\n");
		return -EINVAL;
	}
}

static struct regulator_ops eup3265_dcdc_ops = { 
	.set_voltage = eup3265_dcdc_set_voltage,
	.get_voltage = eup3265_dcdc_get_voltage,
	.list_voltage= eup3265_dcdc_list_voltage,
	.is_enabled = eup3265_dcdc_is_enabled,
	.enable = eup3265_dcdc_enable,
	.disable = eup3265_dcdc_disable,
	.get_mode = eup3265_dcdc_get_mode,
	.set_mode = eup3265_dcdc_set_mode,
	.set_suspend_voltage = eup3265_dcdc_set_sleep_voltage,
	.set_suspend_enable = eup3265_dcdc_suspend_enable,
	.set_suspend_disable = eup3265_dcdc_suspend_disable,
	.set_suspend_mode = eup3265_dcdc_set_suspend_mode,
	.set_voltage_time_sel = eup3265_dcdc_set_voltage_time_sel,
};
static struct regulator_desc regulators[] = {

        {
		.name = "SY_DCDC1",
		.id = 0,
		.ops = &eup3265_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int eup3265_i2c_read(struct i2c_client *i2c, char reg, int count,	u16 *dest)
{
      int ret;
    struct i2c_adapter *adap;
    struct i2c_msg msgs[2];

    if(!i2c)
		return ret;

	if (count != 1)
		return -EIO;  
  
    adap = i2c->adapter;		
    
    msgs[0].addr = i2c->addr;
    msgs[0].buf = &reg;
    msgs[0].flags = i2c->flags;
    msgs[0].len = 1;
    msgs[0].scl_rate = EUP3265_SPEED;
    
    msgs[1].buf = (u8 *)dest;
    msgs[1].addr = i2c->addr;
    msgs[1].flags = i2c->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = EUP3265_SPEED;
    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %d msgs[1].buf = %d\n",__FUNCTION__,__LINE__,*(msgs[1].buf));

	return ret;   
}

static int eup3265_i2c_write(struct i2c_client *i2c, char reg, int count, const u16 src)
{
	int ret=-1;
	
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];

	if(!i2c)
		return ret;
	if (count != 1)
		return -EIO;
    
	adap = i2c->adapter;		
	tx_buf[0] = reg;
	tx_buf[1] = src;
	
	msg.addr = i2c->addr;
	msg.buf = &tx_buf[0];
	msg.len = 1 +1;
	msg.flags = i2c->flags;   
	msg.scl_rate = EUP3265_SPEED;	

	ret = i2c_transfer(adap, &msg, 1);
	return ret;	
}

static int eup3265_reg_read(struct eup3265 *eup3265, u8 reg)
{
	u16 val = 0;
	int ret;

	mutex_lock(&eup3265->io_lock);

	ret = eup3265_i2c_read(eup3265->i2c, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	if (ret < 0){
		mutex_unlock(&eup3265->io_lock);
		return ret;
	}
	mutex_unlock(&eup3265->io_lock);

	return val & 0xff;	
}

static int eup3265_set_bits(struct eup3265 *eup3265, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&eup3265->io_lock);

	ret = eup3265_i2c_read(eup3265->i2c, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	if (ret < 0){
                mutex_unlock(&eup3265->io_lock);
                return ret;
        }
	tmp = (tmp & ~mask) | val;
	ret = eup3265_i2c_write(eup3265->i2c, reg, 1, tmp);
	DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	if (ret < 0){
                mutex_unlock(&eup3265->io_lock);
                return ret;
        }
	ret = eup3265_i2c_read(eup3265->i2c, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	if (ret < 0){
                mutex_unlock(&eup3265->io_lock);
                return ret;
        }
	mutex_unlock(&eup3265->io_lock);

	return 0;//ret;	
}

#ifdef CONFIG_OF
static struct of_device_id eup3265_of_match[] = {
	{ .compatible = "eup,eup3265"},
	{ },
};
MODULE_DEVICE_TABLE(of, eup3265_of_match);
#endif
#ifdef CONFIG_OF
static struct of_regulator_match eup3265_reg_matches[] = {
	{ .name = "eup3265_dcdc1" ,.driver_data = (void *)0},
};

static struct eup3265_board *eup3265_parse_dt(struct eup3265 *eup3265)
{
	struct eup3265_board *pdata;
	struct device_node *regs;
	struct device_node *eup3265_np;
	int count;
	DBG("%s,line=%d\n", __func__,__LINE__);	
	
	eup3265_np = of_node_get(eup3265->dev->of_node);
	if (!eup3265_np) {
		printk("could not find pmic sub-node\n");
		return NULL;
	}
	
	regs = of_find_node_by_name(eup3265_np, "regulators");
	if (!regs)
		return NULL;
	count = of_regulator_match(eup3265->dev, regs, eup3265_reg_matches,
				   eup3265_NUM_REGULATORS);
	of_node_put(regs);
	pdata = devm_kzalloc(eup3265->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	pdata->eup3265_init_data[0] = eup3265_reg_matches[0].init_data;
	pdata->of_node[0] = eup3265_reg_matches[0].of_node;
	
	DBG("%s,line=%d\n", __func__,__LINE__);	
	return pdata;
}

#else
static struct eup3265_board *eup3265_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif

static int eup3265_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct eup3265 *eup3265;	
	struct eup3265_board *pdev ;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *sy_rdev;
	struct regulator_init_data *reg_data;
	const char *rail_name = NULL;
	int ret;
	
	DBG("%s,line=%d\n", __func__,__LINE__);	

	if (i2c->dev.of_node) {
		match = of_match_device(eup3265_of_match, &i2c->dev);
		if (!match) {
			printk("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	eup3265 = devm_kzalloc(&i2c->dev,sizeof(struct eup3265), GFP_KERNEL);
	if (eup3265 == NULL) {
		ret = -ENOMEM;		
		goto err;
	}
	eup3265->i2c = i2c;
	eup3265->dev = &i2c->dev;
	i2c_set_clientdata(i2c, eup3265);
	g_eup3265 = eup3265;
		
	mutex_init(&eup3265->io_lock);	

	//ret = eup3265_reg_read(eup3265,EUP3265_ID1_REG);
	//if ((ret <0) ||(ret ==0xff) ||(ret ==0)){
	//	printk("The device is not eup3265 %x \n",ret);
		//goto err;
	//}
	

	if (eup3265->dev->of_node)
		pdev = eup3265_parse_dt(eup3265);
	
	if (pdev) {
		eup3265->num_regulators = eup3265_NUM_REGULATORS;
		eup3265->rdev = kcalloc(eup3265_NUM_REGULATORS,sizeof(struct regulator_dev *), GFP_KERNEL);
		if (!eup3265->rdev) {
			return -ENOMEM;
		}
		/* Instantiate the regulators */
		reg_data = pdev->eup3265_init_data[0];
		config.dev = eup3265->dev;
		config.driver_data = eup3265;
		if (eup3265->dev->of_node)
			config.of_node = pdev->of_node[0];
			if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[0].name;
			reg_data->supply_regulator = rail_name;
	
		config.init_data =reg_data;
		sy_rdev = regulator_register(&regulators[0],&config);
		if (IS_ERR(sy_rdev)) {
			printk("failed to register regulator\n");
		goto err;
		}
		eup3265->rdev[0] = sy_rdev;
	}
	DBG("%s,line=%d\n", __func__,__LINE__);	
	return 0;
err:
	return ret;	

}

static int  eup3265_i2c_remove(struct i2c_client *i2c)
{
	struct eup3265 *eup3265 = i2c_get_clientdata(i2c);

	if (eup3265->rdev[0])
		regulator_unregister(eup3265->rdev[0]);
	i2c_set_clientdata(i2c, NULL);
	return 0;
}

static const struct i2c_device_id eup3265_i2c_id[] = {
       { "eup3265", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, eup3265_i2c_id);

static struct i2c_driver eup3265_i2c_driver = {
	.driver = {
		.name = "eup3265",
		.owner = THIS_MODULE,
		.of_match_table =of_match_ptr(eup3265_of_match),
	},
	.probe    = eup3265_i2c_probe,
	.remove   = eup3265_i2c_remove,
	.id_table = eup3265_i2c_id,
};

static int __init eup3265_module_init(void)
{
	int ret;
	ret = i2c_add_driver(&eup3265_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(eup3265_module_init);

static void __exit eup3265_module_exit(void)
{
	i2c_del_driver(&eup3265_i2c_driver);
}
module_exit(eup3265_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("eup3265 PMIC driver");

