#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/rtc.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/qpnp/qpnp-adc.h>

#include "battery_core.h"
#include "charger_core.h"


//********************************************
//* хранилище зарегистрированных зарядников  *
//********************************************
static struct charger_core_interface* registered_chip[10]={0,0,0,0,0,0,0,0,0,0}; 
static int registered_count=0;


//********************************************
//* Получение информации об источнике питания
//********************************************
int charger_core_get_adapter(struct adapter *ada) {

int online,rc,current_max,scope,voltage_now;  
union power_supply_propval prop;


if (ada == 0) return -EPERM;
// для безымянных адаптеров
if (ada->name == 0) {
  ada->max_ma=0;
  return 0;
}  
// если имя имеется - ищем power supply* по имени.
if (ada->psy == 0) ada->psy=power_supply_get_by_name(ada->name);
if (ada->psy != 0) {
  rc=ada->psy->get_property(ada->psy,POWER_SUPPLY_PROP_ONLINE,&prop);
  if (rc == 0) online=prop.intval;
  else  online=0;
  rc=ada->psy->get_property(ada->psy,POWER_SUPPLY_PROP_CURRENT_MAX,&prop);
  if (rc != 0) current_max=0;
  else    current_max=prop.intval/1000;

  rc=ada->psy->get_property(ada->psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&prop);
  if (rc != 0) voltage_now=0;
  else voltage_now=prop.intval/1000;
  
  rc=ada->psy->get_property(ada->psy,POWER_SUPPLY_PROP_SCOPE,&prop);
  if (rc == 0) scope=prop.intval;
  else scope=0;
  
  if (online == 0) current_max=0;
}
else current_max=0;
ada->max_ma=current_max;
if (ada->name == 0) return 0;
if (strlen(ada->name) != 0) 
   pr_info("adapter[%s]: psy=%08x scope=%d, online=%d, current_max=%dmA, voltage_now=%dmV\n",ada->name,ada->psy,scope,online,current_max,voltage_now);
return 0;
}

  


//********************************************
//*  Приостановка зарядки 
//********************************************
int charger_core_suspend_charging(void *self) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;
int rc;

if (self == 0) return -EINVAL;
chip=api->self;
if (chip == 0) return 0;
if (api->enable_charge_fn == 0) return 0;
if (chip->charging_state != POWER_SUPPLY_STATUS_CHARGING) return -EINVAL; 
if (chip->charging_suspend != 0) return -EINVAL;

rc=(*api->enable_charge_fn)(api->parent,0);
if (rc == 0) chip->charging_suspend=1;
return rc;
}


//********************************************
//*  Возобновление зарядки 
//********************************************
int charger_core_resume_charging(void *self) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;
int rc;
  
if (self == 0) return -EINVAL;
chip=api->self;
if (chip == 0) return 0;
if (api->enable_charge_fn == 0) return 0;
if (chip->charging_state != POWER_SUPPLY_STATUS_CHARGING) return -EINVAL; 
if (chip->charging_suspend == 0) return -EINVAL;
  
rc=(*api->enable_charge_fn)(api->parent,1);
if (rc == 0) chip->charging_suspend=1;
return rc;
}

//********************************************
//*  Установка зарядного тока
//********************************************
int charger_core_set_charging_current(void *self, int mA) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;
int rc;
int max_src_ma;
int max_bat_ma;
int max_ma;
int enable;

if ((self == 0) || (mA<0)) return -EINVAL;
chip=api->self;
//pr_err(" setchg: chip=%08x  api=%08x\n",chip,api);
if (api == 0) return -EINVAL;
if (api->enable_charge_fn == 0) return -EINVAL;

api->ad_usb.af12=2;
charger_core_get_adapter(&api->ad_usb);	

api->ad128.af12=2;
charger_core_get_adapter(&api->ad128);	

api->ad144.af12=2;
charger_core_get_adapter(&api->ad144);	

api->ad160.af12=2;
charger_core_get_adapter(&api->ad160);	

max_src_ma=max(api->ad_usb.max_ma,api->ad128.max_ma);
max_src_ma=max(max_src_ma,api->ad144.max_ma);

max_bat_ma=min(chip->ichg_max, chip->ibat_max);
max_ma=min(max_src_ma, max_bat_ma);
max_ma=min(max_ma,mA);
if (max_ma == 0) enable=0;
else enable=1;

rc=0;
if (api->set_current_limit_fn != 0) {
  rc=(*api->set_current_limit_fn)(api->parent,max_ma);
}

if (api->enable_charge_fn != 0) {
  rc=(*api->enable_charge_fn)(api->parent, enable);
}
if (rc != 0) {
  pr_err("failed to set charging current(%dmA) at driver layer!\n",max_ma);
  return rc;
}
chip->charging_suspend=0;
chip->charging_done=0;
chip->ichg_now=max_ma;
chip->charging_state=( (enable==0) ? POWER_SUPPLY_STATUS_NOT_CHARGING : POWER_SUPPLY_STATUS_CHARGING);
pr_info("ichg=%dmA at %s\n",max_ma,(enable==0?"not_charging":"charging"));
return 0;
}

//********************************************
//*  Чтение текущего тока зарядки
//********************************************
int charger_core_get_charging_current(void *self, int *mA) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;

if ((self == 0) || (mA == 0)) return -EINVAL;
chip=api->self;
if (chip == 0) return -EINVAL;
*mA=chip->ichg_now;
return 0;
}

//************************************************
//*  Получение информационной структуры зарядника
//************************************************
int charger_core_get_charger_info(void *self, struct charger_info *info) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;

if ((self == 0) || (info == 0)) return -EINVAL;
chip=api->self;
if (chip == 0) return -EINVAL;

info->charger_status=chip->charging_state;
info->ichg_now=chip->ichg_now;
info->charging_done=chip->charging_done;
if ((api->ad_usb.max_ma > 0) || (api->ad128.max_ma > 0) || (api->ad144.max_ma > 0)) 
  info->ada_connected=1;
else info->ada_connected=0;
return 0;
}

//********************************************
//* Приостановка перезарядки
//********************************************
int  charger_core_suspend_recharging(void *self) {
  
if (self == 0) return -EINVAL;
return -EPERM;
}


//********************************************
//* Возобновление перезарядки
//********************************************
int charger_core_resume_recharging(void* self) {
if (self == 0) return -EINVAL;
return -EPERM;
}


//********************************************
//*    Обработчик событий
//********************************************
int charger_core_notify_event(void *self, int event, void *params) {
  
struct charger_interface* api=self;
struct charger_core_interface* chip;
int ma;
int rc;

if (self == 0) return -EINVAL;
chip=api->self;
if (chip == 0) return -EINVAL;

switch(event) {
  case 1:
    // информация об окончании зарядки
    chip->charging_done=1;
  case 2:
  case 3:
    return 0;
    // установка нового зарядного тока
  case 4: 
    if (params == 0) return 0;
    ma=*((int*)params);
    if (ma == chip->ibat_max) return 0;
    chip->ibat_max=ma;
    if (chip->charging_state != POWER_SUPPLY_STATUS_CHARGING) return 0;
    if (api->set_charging_current == 0) return 0;
    rc=api->set_charging_current(api,ma);    
    if (rc != 0) pr_err("failed to adjust charging current %dmA to %dmA\n",chip->ichg_now,chip->ibat_max);
    return rc;
  default:
    pr_err("no such event(%d)!",event);
    return -EPERM;
}
}
    
    
    
//********************************************
//* Регистрация драйвера зарядника
//********************************************
int charger_core_register(struct device* dev, struct charger_interface* api) {

struct charger_core_interface* chip;
int i; 

if ((dev == 0) || (api == 0)) return -EINVAL;
if (api->parent == 0) return -EINVAL;  // нет собственной управляющей структуры

chip=kzalloc(sizeof(struct charger_core_interface),GFP_KERNEL);
if (chip == 0) {
  pr_err("cannot allocate memory!\n");
  return -ENOMEM;
}
//pr_err("register chip=%08x api=%08x\n",chip,api);
chip->dev=dev;
mutex_init(&chip->mutx);
chip->api=api;
chip->charging_suspend=0;
chip->charging_done=0;
chip->ichg_max=2000;
chip->charging_state=3;
chip->ibat_max=2000;
chip->irechg_max=2000;
chip->recharging_state=3;
chip->ichg_now=0;
chip->recharging_suspend=0;	

api->self=chip;  // обратная связь от интерфейса charger_core_interface к интерфейсу charger_interface
api->suspend_charging=charger_core_suspend_charging;
api->resume_charging = charger_core_resume_charging;
api->set_charging_current = charger_core_set_charging_current;
api->get_charging_current = charger_core_get_charging_current;
api->get_charger_info = charger_core_get_charger_info;
api->suspend_recharging = charger_core_suspend_recharging;
api->resume_recharging = charger_core_resume_recharging;
api->set_recharging_current = 0;
api->notify_event = charger_core_notify_event;

if (registered_count <9) {
  for (i=0;i<10;i++) {
    if (registered_chip[i] != 0) continue;
    registered_chip[i]=chip;
    registered_count++;
    break;
  }  
}
pr_info("Charger Core Version 4.1.5 (Built at %s %s)!",__DATE__,__TIME__);
return 0; 
}

//*************************************************8
//* Поиск зарядника по имени
//*************************************************8
struct charger_core_interface* charger_core_get_charger_interface_by_name(const unsigned char* name) {

int i;
  
if (name == 0) return 0;
if (name[0] == 0) return 0;

for (i=0;i<10;i++) {
  if (registered_chip[i] == 0) continue;
  if (strcmp(registered_chip[i]->api->ext_name_battery,name) == 0) return registered_chip[i];
}
return 0;
}

