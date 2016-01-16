#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/mfd/88pm860x.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/qpnp/qpnp-adc.h>

int32_t jrd_qpnp_vadc_read(enum qpnp_vadc_channels channel,struct qpnp_vadc_result *result);


//*************************************************
//* Струтура описания рабочих переменных драйвера
//*************************************************

struct battery_interface {
  char* bname;  
  int (*get_vbat_proc)(struct battery_interface*, int*);  
  int (*get_vntc_proc)(struct battery_interface*, int*);  
  int vbat;   
  int tbat;
  int charging_status;
  unsigned int	batt_health; 
  struct power_supply psy;
  struct device* dev; 
  struct device* parent;
};

//*************************************************
//*  Список параметров, поддерживаемых батарейкой
//*************************************************

static enum power_supply_property pmd9635_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

//*****************************************************
//*  Таблица соответствия напряжения и уровня заряда
//*****************************************************

struct capacity {
  int percent;
  int vmin;
  int vmax;
  int offset;
  int hysteresis;
};
  
// 
struct capacity battery_capacity_table[]= {
//  %       vmin     vmax   offset hysteresis
   {0,      3100,    3597,    0,    10},
   {1,      3598,    3672,    0,    10},
   {10,     3673,    3735,    0,    10},
   {20,     3736,    3757,    0,    10},
   {30,     3758,    3788,    0,    10},
   {40,     3789,    3832,    0,    10},
   {50,     3833,    3909,    0,    10},
   {60,     3910,    3988,    0,    10},
   {70,     3989,    4072,    0,    10},
   {80,     4073,    4156,    0,    10},
   {90,     4157,    4200,    0,    10},
   {100,    4201,    4500,    0,    10}
};   
#define battery_capacity_table_size 12



//**************************************
//* Возобновление работы модуля
//**************************************
int pmd9635_battery_resume(struct platform_device* pdev) {
return 0;
}

//**************************************
//* Приостановка модуля
//**************************************
int pmd9635_battery_suspend(struct platform_device* pdev, pm_message_t state) {
return 0;
}

//**************************************
//*  Чтение канала АЦП
//**************************************
int pmd9635_get_adc_value(int channel,int* val) {
  
const char* procname="pmd9635_get_adc_value";
int ret;
struct qpnp_vadc_result stor;

if (val == 0) {
  pr_err("%s: Pointer of val is null\n",procname);
  return -EINVAL;
}
ret=jrd_qpnp_vadc_read(channel,&stor);
*val=stor.physical;
if (ret == 0) return 0;
pr_err("%s: can't get adc value from channel %d, rc=%d",procname,channel,ret);
return ret;
}

//**************************************
//*  Чтение напряжения аккумулятора
//**************************************
int pmd9635_battery_get_vbat(struct battery_interface* batdata, int* val) {

int vbat_channel;  
int ret;

if ((batdata == 0) || (val == 0)) return -EINVAL;

vbat_channel=batdata->vbat;
ret=pmd9635_get_adc_value(vbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vbat: can't get battery voltage, rc=%d\n",ret);
return ret;
}

//**************************************
//*  Чтение температуры аккумулятора
//**************************************
int pmd9635_battery_get_vntc(struct battery_interface* batdata, int* val) {

int tbat_channel;  
int ret;

if ((batdata == 0) || (val == 0)) return -EINVAL;

tbat_channel=batdata->tbat;
ret=pmd9635_get_adc_value(tbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vntc: can't get battery temperature, rc=%d\n",ret);
return ret;
}

//**************************************
//*  Чтение заряда аккумулятора
//**************************************
int pmd9635_battery_get_capacity(struct battery_interface* batdata, int* val) {
  
int i,volt;

pmd9635_battery_get_vbat(batdata, &volt);
volt/=1000; // переводим в милливольты
for(i=0;i<battery_capacity_table_size;i++) {
  if ((volt>battery_capacity_table[i].vmin) && (volt<battery_capacity_table[i].vmax)) {
    *val=battery_capacity_table[i].percent;
//    pr_err("pmd9635_battery_get_capacity: volt=%i percent=%i",volt,*val);
//    return 1;
  }  
}
//pr_err("pmd9635_battery_get_capacity: error, volt=%i",volt);
return 0;
}

//**************************************
//*  Получение параметров батарейки
//**************************************
static int pdm9635_bat_get_property(struct power_supply *ps,enum power_supply_property psp,
				union power_supply_propval *val) {
  
struct battery_interface* batdata=container_of(ps, struct battery_interface, psy);

switch (psp) {
  case POWER_SUPPLY_PROP_STATUS:
    val->intval = batdata->charging_status;
    break;
    
  case POWER_SUPPLY_PROP_HEALTH:
    val->intval = batdata->batt_health;
    break;
    
  case POWER_SUPPLY_PROP_PRESENT:
    val->intval = 1;
    break;
    
  case POWER_SUPPLY_PROP_TEMP:
//    pmd9635_battery_get_vntc(batdata,&val->intval);
  val->intval=14;
    break;
    
  case POWER_SUPPLY_PROP_ONLINE:
    val->intval = 1;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    pmd9635_battery_get_vbat(batdata,&val->intval);
    break;
    
  case POWER_SUPPLY_PROP_CAPACITY:
    pmd9635_battery_get_capacity(batdata,&val->intval);
//    val->intval=80;
    break;
    
  case POWER_SUPPLY_PROP_TECHNOLOGY:
    val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
    break;
    
  case POWER_SUPPLY_PROP_CURRENT_NOW:
    val->intval = 0;
    break;
    
  default:
    return -EINVAL;
}
return 0;
}

  
  
//***********************************************
//*  Конструктор модуля
//***********************************************
static int pmd9635_battery_probe(struct platform_device *pdev) {


const char* procname="pmd9635_battery_probe"; 
static char* bname="battery";
int ret;

struct battery_interface* batdata;
int vbat_channel, tbat_channel;
struct device* dparent;

if ((pdev == 0) || (pdev->dev.of_node == 0)) return -EINVAL;

batdata=kmalloc(sizeof(struct battery_interface),__GFP_ZERO|GFP_KERNEL);
if (batdata == 0) {
  pr_err("%s: Can't allocate memory!",procname);
  return -ENOMEM;
}

dparent=pdev->dev.parent;
batdata->parent=dparent;

batdata->bname=bname;

batdata->psy.name="pmd9635-battery";
batdata->psy.type=POWER_SUPPLY_TYPE_BATTERY;
batdata->psy.use_for_apm=1;
batdata->psy.get_property = pdm9635_bat_get_property;
batdata->psy.properties = pmd9635_battery_props,
batdata->psy.num_properties = ARRAY_SIZE(pmd9635_battery_props),
batdata->batt_health=POWER_SUPPLY_HEALTH_GOOD;
batdata->charging_status=POWER_SUPPLY_STATUS_NOT_CHARGING;
dev_set_drvdata(&pdev->dev,batdata);

if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,vbat-channel", &vbat_channel, 1) != 0) {
  pr_err("%s: failed to get vbat channel!\n",procname);
  return -EPERM;
}
batdata->vbat=vbat_channel;
  
if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,tbat-channel", &tbat_channel, 1) != 0) {
  pr_err("%s: failed to get tbat channel!\n",procname);
  return -EPERM;
}
batdata->tbat=tbat_channel;

if ((vbat_channel<0) && (tbat_channel<0)) {
  dev_set_drvdata(&pdev->dev,0);
  kfree(batdata);
  return 0;
}
 
if (vbat_channel>=0)  batdata->get_vbat_proc=&pmd9635_battery_get_vbat;
 else batdata->get_vbat_proc=0;

if (tbat_channel>=0)  batdata->get_vntc_proc=&pmd9635_battery_get_vntc;
 else batdata->get_vntc_proc=0;
 
ret = power_supply_register(&pdev->dev, &batdata->psy);
if (ret != 0) {
  pr_err("%s: fail to register battery core, rc=%d!\n",procname,ret);
  dev_set_drvdata(dparent,0);
  kfree(batdata);
  return ret;
}
 
printk(KERN_ERR "%s: vbat_channel=%d, tbat_channel=%d\n",procname,vbat_channel,tbat_channel);
return 0;
}


//**************************************
//* Деструктор модуля
//**************************************
static int pmd9635_battery_remove(struct platform_device *pdev) {
struct device* dparent;
struct battery_interface* batdata;

dparent=&pdev->dev;
batdata=dev_get_drvdata(dparent);

kfree(batdata);
return 0;
}

//**************************************
//*  Структуры данных описания модуля
//**************************************

struct of_device_id pmd9635_battery_match={
  .compatible="qcom,pmd9635-battery"
};  

static struct platform_driver pmd9635_battery_driver = {
	.driver = {
		   .name = "pmd9635-battery",
		   .owner = THIS_MODULE,
		   .of_match_table = &pmd9635_battery_match
	},
	.probe = pmd9635_battery_probe,
	.remove = pmd9635_battery_remove,
	.suspend = pmd9635_battery_suspend,
 	.resume = pmd9635_battery_resume
};

module_platform_driver(pmd9635_battery_driver);

MODULE_DESCRIPTION("pmd9635 Battery driver");
MODULE_LICENSE("GPL");
