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
#include <linux/workqueue.h>
#include <asm/delay.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "battery_core.h"
#include "charger_core.h"

//#define pr_fmt(fmt) "%s: " fmt, __func__
umode_t battery_attr_is_visible(struct kobject *kobj,struct attribute *attr, int attrno);
ssize_t battery_show_property(struct device *dev,struct  device_attribute *attr, char* buf);
ssize_t battery_store_property(struct device *dev,struct device_attribute *attr, const char* buf, size_t count);


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
struct capacity battery_capacity_table[12]= {
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




//*****************************************************
//*  Таблица перевода напряжения в температуру
//*****************************************************
struct ntc_tvm {
   int tntc;
   int tnvc;
};

struct ntc_tvm ntc_tvm_tables[] = {
// температура  напряжение
     {-45,      1800000},         
     {-40,      1693138},
     {-35,      1665952},
     {-30,      1633668},
     {-25,      1595877},
     {-20,      1551873},
     {-15,      1501569},
     {-10,      1444173},
     {-5,       1379919},
     {0,        1308687},
     {5,        1231323},
     {10,       1149570},
     {15,       1065303},
     {20,       979678},
     {25,       895000},
     {30,       812338},
     {35,       733267},
     {40,       658735},
     {45,       589543},
     {50,       525875},
     {55,       467795},
     {60,       415297},
     {65,       368123},
     {70,       326146},
     {75,       288699},
     {80,       255758},
     {85,       226817},
     {90,       201430},
     {95,       179128},
     {100,      159466},
     {105,      142204},
     {110,      127049},
     {115,      113656},
     {120,      101957},
     {125,      91546}
};    

#define ntc_table_size 35

//*****************************************************
//* Идентификаторы событий монитора
//*****************************************************
enum  {
  BATTERY_EVENT_NONE,                    // 0
  BATTERY_EVENT_CRITICAL_VOLTAGE,        // 1
  BATTERY_EVENT_LOW_VOLTAGE,             // 2  
  BATTERY_EVENT_RESUME_CHARGING,         // 3
  BATTERY_EVENT_LOW_TEMPERATURE,         // 4
  BATTERY_EVENT_RECOVER_FROM_LOW_TEMP,   // 5
  BATTERY_EVENT_HIGH_TEMPERATURE,        // 6
  BATTERY_EVENT_RECOVER_FROM_HIGH_TEMP,  // 7
  BATTERY_EVENT_POWEROFF_TEMPERATURE,    // 8
  BATTERY_EVENT_PLUGIN,                  // 9
  BATTERY_EVENT_PLUGOUT                  // 10
};  

//*****************************************************
//* Описания событий монитора
//*****************************************************

char* battery_event_strings[]= {
  "BATTERY_EVENT_NONE",
  "BATTERY_EVENT_CRITICAL_VOLTAGE",
  "BATTERY_EVENT_LOW_VOLTAGE",
  "BATTERY_EVENT_RESUME_CHARGING",
  "BATTERY_EVENT_LOW_TEMPERATURE",
  "BATTERY_EVENT_RECOVER_FROM_LOW_TEMP",
  "BATTERY_EVENT_HIGH_TEMPERATURE",
  "BATTERY_EVENT_RECOVER_FROM_HIGH_TEMP",
  "BATTERY_EVENT_POWEROFF_TEMPERATURE",
  "BATTERY_EVENT_PLUGIN",
  "BATTERY_EVENT_PLUGOUT"
};  

//*****************************************************
//* Описание состосяний работоспособности батарейки
//*****************************************************
char* battery_health_strings[]= {
 "Unknown",
 "Good Health",
 "Overheat",
 "Dead",
 "Over Voltage",
 "Unspecified Failure",
 "Cold"
}; 

//*****************************************************
//*   Таблица sysfs-атрибутов
//*****************************************************

/*
static DEVICE_ATTR(capacity,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(ntc,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(precharge_voltage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(poweroff_voltage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(low_voltage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(recharge_voltage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(charge_done_votage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(temp_low_poweroff,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(temp_low_disable_charge,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(temp_high_disable_charge,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(temp_high_poweroff,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(temp_error_margin,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(charging_monitor_period,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(discharging_monitor_period,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(vbat_max,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(ibat_max,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(test_mode,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(disable_charging,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(high_voltage,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(capacity_changed_margin,0,battery_show_property, battery_store_property);  
static DEVICE_ATTR(debug_mode,0,battery_show_property, battery_store_property);  

static struct attribute* battery_attrs[]={
  &dev_attr_capacity.attr,
  &dev_attr_ntc.attr,
  &dev_attr_precharge_voltage.attr,
  &dev_attr_poweroff_voltage.attr,
  &dev_attr_low_voltage.attr,
  &dev_attr_recharge_voltage.attr,
  &dev_attr_charge_done_votage.attr,
  &dev_attr_temp_low_poweroff.attr,
  &dev_attr_temp_low_disable_charge.attr,
  &dev_attr_temp_high_disable_charge.attr,
  &dev_attr_temp_high_poweroff.attr,
  &dev_attr_temp_error_margin.attr,
  &dev_attr_charging_monitor_period.attr,
  &dev_attr_discharging_monitor_period.attr,
  &dev_attr_vbat_max.attr,
  &dev_attr_ibat_max.attr,
  &dev_attr_test_mode.attr,
  &dev_attr_disable_charging.attr,
  &dev_attr_high_voltage.attr,
  &dev_attr_capacity_changed_margin.attr,
  &dev_attr_debug_mode.attr,
  0
};
*/

static struct device_attribute battery_dev_attrs[21]={
 {{"capacity", 0},                   &battery_show_property, &battery_store_property},
 {{"ntc", 0},                        battery_show_property, battery_store_property},
 {{"precharge_voltage", 0},          battery_show_property, battery_store_property},
 {{"poweroff_voltage", 0},           battery_show_property, battery_store_property},
 {{"low_voltage", 0},                battery_show_property, battery_store_property},
 {{"recharge_voltage", 0},           battery_show_property, battery_store_property},
 {{"charge_done_votage", 0},         battery_show_property, battery_store_property},
 {{"temp_low_poweroff", 0},          battery_show_property, battery_store_property},
 {{"temp_low_disable_charge", 0},    battery_show_property, battery_store_property},
 {{"temp_high_disable_charge", 0},   battery_show_property, battery_store_property},
 {{"temp_high_poweroff", 0},         battery_show_property, battery_store_property},
 {{"temp_error_margin", 0},          battery_show_property, battery_store_property},
 {{"charging_monitor_period", 0},    battery_show_property, battery_store_property},
 {{"discharging_monitor_period", 0}, battery_show_property, battery_store_property},
 {{"vbat_max", 0},                   battery_show_property, battery_store_property},
 {{"ibat_max", 0},                   battery_show_property, battery_store_property},
 {{"test_mode", 0},                  battery_show_property, battery_store_property},
 {{"disable_charging", 0},           battery_show_property, battery_store_property},
 {{"high_voltage", 0},               battery_show_property, battery_store_property},
 {{"capacity_changed_margin", 0},    battery_show_property, battery_store_property},
 {{"debug_mode", 0},                 battery_show_property, battery_store_property}
};

static struct attribute* battery_attrs[]={
  &battery_dev_attrs[0].attr,
  &battery_dev_attrs[1].attr,
  &battery_dev_attrs[2].attr,
  &battery_dev_attrs[3].attr,
  &battery_dev_attrs[4].attr,
  &battery_dev_attrs[5].attr,
  &battery_dev_attrs[6].attr,
  &battery_dev_attrs[7].attr,
  &battery_dev_attrs[8].attr,
  &battery_dev_attrs[9].attr,
  &battery_dev_attrs[10].attr,
  &battery_dev_attrs[11].attr,
  &battery_dev_attrs[12].attr,
  &battery_dev_attrs[13].attr,
  &battery_dev_attrs[14].attr,
  &battery_dev_attrs[15].attr,
  &battery_dev_attrs[16].attr,
  &battery_dev_attrs[17].attr,
  &battery_dev_attrs[18].attr,
  &battery_dev_attrs[19].attr,
  &battery_dev_attrs[20].attr,
  0
};



static struct attribute_group battery_attr_group={
  "parameters", 
  battery_attr_is_visible, 
  battery_attrs
}; 

//*****************************************************
//*  Таблица параметров батарейки
//*****************************************************
static enum power_supply_property battery_core_power_props[] = {
  POWER_SUPPLY_PROP_STATUS, 
  POWER_SUPPLY_PROP_PRESENT, 
  POWER_SUPPLY_PROP_TEMP, 
  POWER_SUPPLY_PROP_HEALTH, 
  POWER_SUPPLY_PROP_VOLTAGE_NOW, 
  POWER_SUPPLY_PROP_VOLTAGE_AVG,
  POWER_SUPPLY_PROP_CAPACITY, 
  POWER_SUPPLY_PROP_VOLTAGE_MAX, 
  POWER_SUPPLY_PROP_CURRENT_MAX, 
  POWER_SUPPLY_PROP_CURRENT_NOW
};  

//*****************************************************
//*  Проверка доступности записи уазанного параметра
//*****************************************************
int battery_core_property_is_writeable(struct power_supply *psy,enum power_supply_property psp) {
  
if ((psp ==  POWER_SUPPLY_PROP_PRESENT) || 
    (psp == POWER_SUPPLY_PROP_VOLTAGE_NOW) || 
    (psp == POWER_SUPPLY_PROP_TEMP))
    return 1;
return 0;
}

//*****************************************************
//* Чтение параметра батареи
//*****************************************************
int battery_core_get_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val) {
  
struct battery_core_interface* bat=container_of(psy, struct battery_core_interface, psy);  

switch(psp){
  case POWER_SUPPLY_PROP_STATUS:
    val->intval=bat->status;
    break;
  
  case POWER_SUPPLY_PROP_HEALTH:
    val->intval=bat->health;
    break;
    
  case POWER_SUPPLY_PROP_PRESENT:  
    val->intval=bat->present;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_MAX:  
    val->intval=bat->volt_max;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:  
    val->intval=bat->volt_now;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_AVG:  
    val->intval=bat->volt_avg;
    break;
    
  case POWER_SUPPLY_PROP_CURRENT_MAX:  
    val->intval=bat->current_max;
    break;
    
  case POWER_SUPPLY_PROP_CURRENT_NOW:  
    val->intval=bat->current_now;
    break;
    
  case POWER_SUPPLY_PROP_CAPACITY:  
    val->intval=bat->capacity;
    break;
    
  case POWER_SUPPLY_PROP_TEMP:  
    val->intval=bat->temp;
    break;
    
  default:
    pr_err("No such property(%d)\n",psp);
    return -EINVAL;
}
return 0;
}
    
//*****************************************************
//*  Выход из спячки
//*****************************************************
int battery_core_wakeup(void *self) {
  
struct battery_core_interface* bat=self;

if (!bat->ws.active) __pm_stay_awake(&bat->ws);
return 0;
}

//*****************************************************
//* Запись параметра батареи
//*****************************************************
int battery_core_set_property(struct power_supply *psy,enum power_supply_property psp, const union power_supply_propval *val) {
  
struct battery_core_interface* bat=container_of(psy, struct battery_core_interface, psy);  
int ret=0;

mutex_lock(&bat->lock);

switch (psp) {
  case POWER_SUPPLY_PROP_STATUS:
    bat->status=val->intval;
    break;
  
  case POWER_SUPPLY_PROP_PRESENT:
    if (bat->test_mode != 0) bat->present=val->intval;
    else ret=-EPERM;
    break;
  
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    if (bat->test_mode != 0) bat->volt_now=val->intval;
    else ret=-EPERM;
    break;
    
  case POWER_SUPPLY_PROP_TEMP:
    if (bat->test_mode != 0) bat->temp=val->intval;
    else ret=-EPERM;
    break;
    
  default:
    ret=-EINVAL;
}    
return ret;
}

//*****************************************************
//*   Установка зарядного тока батареи
//*****************************************************
int battery_core_set_ibat(struct battery_core_interface* bat, int mA) {

struct charger_info chg_info;
struct charger_interface* api;

chg_info.ichg_now=0;
chg_info.ada_connected=0;

if (bat->charger == 0) bat->charger=charger_core_get_charger_interface_by_name(bat->bname);
if (bat->charger != 0) {
  api=bat->charger->api;
  if (api->set_charging_current != 0) (*api->set_charging_current)(api,mA);
  if (api->get_charger_info != 0) (*api->get_charger_info)(api,&chg_info);
}

bat->current_now = chg_info.ichg_now*1000;

if (bat->current_now > 0) bat->status=POWER_SUPPLY_STATUS_CHARGING;
else {
  if (chg_info.ada_connected == 0) bat->status=POWER_SUPPLY_STATUS_DISCHARGING;
  else bat->status=POWER_SUPPLY_STATUS_NOT_CHARGING;
}

power_supply_changed(&bat->psy);
return 0;
}

//*****************************************************
//*  Смена состояния источника питания
//*****************************************************
void battery_core_external_power_changed(struct power_supply *psy) {
  
struct battery_core_interface* bat=container_of(psy, struct battery_core_interface, psy);  
int mA;

if (bat->disable_chg != 0) mA=0;
else mA=bat->current_max/1000;
battery_core_set_ibat(bat,mA);
}

//*****************************************************
//*  Вычисление среднего
//*****************************************************
int battery_core_calculate_average(int* data) {
  
int i;
int max,min,v;
int sum=0;
sum=max=min=data[0];

for(i=1;i<8;i++) {
  v=data[i];
  if (v>max) max=v;
  else if (v<min) min=v;
  sum+=v;
}
return(sum-max-min)/6;

}

//*****************************************************
//*  Монитор состояния батареи
//*****************************************************
void battery_core_monitor_work(struct work_struct *work) {
  

int data[8];  

struct battery_interface* api;
struct delayed_work* dw=container_of(work, struct delayed_work, work);
struct battery_core_interface* bat=container_of(dw, struct battery_core_interface, work);  
struct charger_interface* capi;
int i;
int rc;
int temp=0;
int health;
int volt;
int power_event;  // R6
int cap;
int current_max;
int integrated_volt, mvavg;
int dv,cm;
int bpr,offset,capupdate;
int hyst;
int monperiod;
int temp_event=0;
int capdelta;

// Хранилище для вычисления среднего напряжения между циклами монитора (интегратор напряжения)
static int sum,count,average,calculate_initialized;


if (!bat->ws.active) __pm_stay_awake(&bat->ws);
api=bat->api;
memset(data,0,32);
if (api-> get_vntc_proc != 0) {
//  r7=-22;
}
else {
  if (api->x40 == 0) goto donetemp;
//  else r7=-22;  
}
rc=1;
for (i=0;i<8;i++) {
  if (api-> get_vntc_proc != 0) rc= (*api-> get_vntc_proc)(api,&data[i]);
  else if (api-> x40 != 0) rc= (api-> x40)(api->thisptr,&data[i]);
  if (rc != 0) {
    pr_err("failed to measure battery temperature, rc=%d\n",rc);
    goto donetemp;
  }
  (*arm_delay_ops.const_udelay)(1073740);
}
if (api-> get_vntc_proc != 0) {
  // усредняем результат 8 выборок
   rc=battery_core_calculate_average(data);
  // проверяем наличие таблицы переода напряжения в температуру (NTC) 
   if (bat->ntc == 0) {
     pr_err("NTC convert table is NULL!\n");
     temp=25;
   }  
   else {
     // собственно сам алгоритм табличного преобразования
     // Пересчитываем напряжение по калибровочным коэффициетам
     if (bat->vref != bat-> vref_calib) rc=rc*bat->vref_calib/bat->vref;
       
     // поиск по таблице   
     for(i=0;i<bat->ntcsize-1;i++) {
       if ((rc<bat->ntc[i].tnvc) && (rc>bat->ntc[i+1].tnvc)) break;
     }
     // интерполяция таличного значения
     bpr=(rc-bat->ntc[i].tnvc)*(bat->ntc[i+1].tntc-bat->ntc[i].tntc)*10/(bat->ntc[i+1].tnvc-bat->ntc[i].tnvc);
     temp=bat->ntc[i].tntc+bpr/10;
     if ((bpr%10)>4) temp++;
     if (temp > bat->ntc[i+1].tntc) cap=bat->ntc[i+1].tntc;
    }
   
   // Выставляем флаги состояния в зависимости от температуры
   health=POWER_SUPPLY_HEALTH_GOOD;
   if ((temp>bat->temp_high_poweroff) || (temp<bat->temp_low_poweroff)) health=POWER_SUPPLY_HEALTH_DEAD;
   else {
     if (temp>bat->temp_high_disable_charge) health=POWER_SUPPLY_HEALTH_OVERHEAT;
     if (temp<bat->temp_low_disable_charge) health=POWER_SUPPLY_HEALTH_COLD;
   }
   
   mutex_lock(&bat->lock);
   bat->temp=temp;
   bat->health=health;
   
   mutex_unlock(&bat->lock);
}
donetemp:

//------------------------------------------------------------------------------------------------
// Температуру измерили, теперь измеряем напряжение

cap=99;
memset(data,0,32);

if (api-> get_vbat_proc == 0) {
  volt=0;
  goto no_vbat_proc;
}  

// приостанавливаем зарядку и ждем стабилизации напряжения на батарее
if (bat->charger == 0) bat->charger=charger_core_get_charger_interface_by_name(bat->bname);
if (bat->charger != 0) {
  capi=bat->charger->api;
  if (capi->suspend_charging != 0) {
    rc=(*capi->suspend_charging)(capi);
    if (rc == 0) {
      for(i=0;i<11;i++) {
	(*arm_delay_ops.const_udelay)(107374000);
      }
    }
  }
}  


// 8 выборок напряжения
for (i=0;i<8;i++) {
  rc= (*api-> get_vbat_proc)(api,&data[i]);
  if (rc != 0) {
    pr_err("failed to measure battery voltage, rc=%d\n",rc);
    volt=0;
    goto no_vbat_proc;
  }  
  (*arm_delay_ops.const_udelay)(1073740);  // задержка на время нового преобразования
}

// возобновляем зарядку
if (bat->charger != 0) {
  capi=bat->charger->api;
  if (capi->resume_charging != 0) (*capi->resume_charging)(capi);
}

// усредняем результат 8 выборок
volt=battery_core_calculate_average(data);
// в тестовом режиме берем установленное напряжение вместо измеренного
if (bat->test_mode != 0) volt=bat->volt_now;

// **** Интегратор напряжения аккумулятора *** (та самая математика, которой я так боялся)

if (bat-> present == 1) {
 // Если флаг present поднят - вычисляем интегральное значение за 32 выборки
 if (calculate_initialized == 0) {
   // начальный этап накопления данных интегратора - первые 7 выборок считаем просто среднее
   count++;
   sum+=volt;
   average=sum/count;
   if (count>7) calculate_initialized=1;
 }
 else {
   // Все последующий выборки, начиная от 8 - учитываем предыдущий хвост
   if (bat->status == POWER_SUPPLY_STATUS_CHARGING) dv=32;
   else  dv=((count<32) ? 32 : count);
   count++;
   cm=dv*volt+average*(32-dv);
   if (cm<0) cm+=31;
   sum+=cm/32;
   average=sum/count;
   if (count > 63) {
     // сделаны 63 выборки - усредняем результат, представляем его как первые 8 выборок новго цикла
     sum=average*8;
     count=8;
     average=sum/8;
   }
 }
 integrated_volt=average;
}
else {
 // Если флаг present опущен - делаем сброс переменных интегратора
 sum=0;
 count=0;
 average=0;
 calculate_initialized=0;
 integrated_volt=volt;
} 

// **** Определение заряда аккумулятора 

mvavg=integrated_volt/1000; // напряжение в mV

// если таблицы процентов не существует
if (bat-> cap == 0) {
  pr_err("Capacity convert table is NULL!\n");
  cap=-22;
  capupdate=1;
  goto nocap;
}  
// выход за границы таблицы

else if  (mvavg < bat->cap[0].vmin)              cap=0;
else if  (mvavg > bat->cap[bat->capsize-1].vmin) cap=100;
else {
 // напряжение попадает внутрь таблицы - ищем строку таблицы
 for (i=0;i<bat->capsize;i++) {
   if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING) offset=0;
   else offset=bat->cap[i].offset;
   if (((mvavg+offset) >= bat->cap[i].vmin) && ((mvavg+offset) <= bat->cap[i].vmax)) break;
 }
 //   смещение V от vmin        разность % между строками                       разность напряжений между строками
 bpr=(mvavg-bat->cap[i].vmin)*(bat->cap[i+1].percent-bat->cap[i].percent)*10/(bat->cap[i+1].vmin-bat->cap[i].vmin);
 cap=bat->cap[i].percent+bpr/10;
 if ((bpr%10)>4) cap++;
 if (cap > bat->cap[i+1].percent) cap=bat->cap[i+1].percent;
}

// Провряем, не попадает ли напряжение на границу перехода процентов

if (bat->test_mode != 0) capupdate=1;
else {
  capupdate=0;
  for (i=bat->capsize-1;i>0;i--) {
    if (bat->capacity > bat->cap[i-1].percent) break;
  }
  hyst=bat->cap[i].hysteresis;
  if ((mvavg < (bat->cap[i].vmin-hyst)) || (mvavg > (bat->cap[i].vmax+hyst))) capupdate=1;
}
if (cap != bat->capacity) capupdate=1;

nocap:

mutex_lock(&bat->lock);
bat->volt_now=volt;
bat->volt_avg=integrated_volt;
if (capupdate) bat->capacity=cap;
mutex_unlock(&bat->lock);

if (bat->debug_mode) {
  pr_info("vbat(meas/avg)=%dmV/%dmV, capacity(%d%%) has %s %s\n",bat->volt_now/1000,mvavg,
	  bat->capacity,capupdate?"changed":"not changed",bat->test_mode?"at test mode":"");
}  

no_vbat_proc:


volt/=1000;
//power_event=bat->status;

// Формирование статуса зарядки
if (api-> get_vbat_proc != 0) switch (bat->status) {
  //%%%%%%%%%%%%%%%%%%%%%
  case POWER_SUPPLY_STATUS_DISCHARGING:
    if (volt<bat->poweroff_volt) {
      // напряжение ниже критически низкого
      power_event=BATTERY_EVENT_CRITICAL_VOLTAGE;         
      bat->power_event=BATTERY_EVENT_CRITICAL_VOLTAGE;  
      break;
    }
    if (volt>=bat->low_volt) {
      // напряжение в норме
      power_event=0;   
      break;
    }
    // Если напряжение находится в пределах poweroff_volt < volt <= low_volt
    if (bat->power_event != BATTERY_EVENT_LOW_VOLTAGE) { 
      power_event=BATTERY_EVENT_LOW_VOLTAGE;  // второй проход - 2
      bat->power_event=BATTERY_EVENT_LOW_VOLTAGE;
      break;
    }  
    power_event=0;  // первый проход - 0
    break;
   
  //%%%%%%%%%%%%%%%%%%%%%
   case POWER_SUPPLY_STATUS_NOT_CHARGING:
    if (bat-> present == 0) {
      // батарейки нет
      power_event=0;
      break;
    }
    if (bat-> health != POWER_SUPPLY_HEALTH_GOOD) {
      // батарейка есть, но дохлая
      power_event=0;
      break;
    }
    if (volt<bat->recharge_volt) {
      // напряжение упало ниже напряжения повторного запуска зарядки
      bat->power_event=BATTERY_EVENT_RESUME_CHARGING;
      break;
    }
    power_event=0;
    break;

  //%%%%%%%%%%%%%%%%%%%%%
   case POWER_SUPPLY_STATUS_CHARGING:
    power_event=0;
    bat->power_event=0;
    break;
    
  //%%%%%%%%%%%%%%%%%%%%%
   default:
    power_event=0;
}

if (bat->debug_mode != 0) pr_info("Battery %s(%dC), last_event=%s\n",
    battery_health_strings[bat->health],
    temp,
    battery_event_strings[bat->event]);

// разбор и формирование event
if (bat->present != 0) {
  if (bat->event== BATTERY_EVENT_PLUGOUT) bat->event=BATTERY_EVENT_PLUGIN;
}
else switch(bat->health) {
   case POWER_SUPPLY_HEALTH_GOOD:
    if (bat->event == BATTERY_EVENT_LOW_TEMPERATURE) {
      bat->event=BATTERY_EVENT_RECOVER_FROM_LOW_TEMP;
      temp_event=BATTERY_EVENT_RECOVER_FROM_LOW_TEMP;
      break;
    }
    if (bat->event == BATTERY_EVENT_HIGH_TEMPERATURE) {
      bat->event=BATTERY_EVENT_RECOVER_FROM_HIGH_TEMP;
      temp_event=BATTERY_EVENT_RECOVER_FROM_HIGH_TEMP;
      break;
    }    
    temp_event=0;
    break;
    
   case POWER_SUPPLY_HEALTH_COLD:
    if (bat->event != BATTERY_EVENT_LOW_TEMPERATURE) {
      bat->event=BATTERY_EVENT_LOW_TEMPERATURE;
      temp_event=BATTERY_EVENT_LOW_TEMPERATURE;
      break;
    }
    temp_event=0;
    break;
    
   case POWER_SUPPLY_HEALTH_OVERHEAT:
    if (bat->event != BATTERY_EVENT_HIGH_TEMPERATURE) {
      bat->event=BATTERY_EVENT_HIGH_TEMPERATURE;
      temp_event=BATTERY_EVENT_HIGH_TEMPERATURE;
      break;
    }
    temp_event=0;
    break;

   case POWER_SUPPLY_HEALTH_DEAD:
     bat->event=BATTERY_EVENT_POWEROFF_TEMPERATURE;
     temp_event=BATTERY_EVENT_POWEROFF_TEMPERATURE;
     break;
     
    
   case POWER_SUPPLY_HEALTH_WARM:
   case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
   case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
   default:
    temp_event=0;
    break;
}   

if (bat->debug_mode != 0) pr_info("event[voltage]=%s, event[temp]=%s\n",
    battery_event_strings[power_event],
    battery_event_strings[temp_event]);

if ((power_event==BATTERY_EVENT_CRITICAL_VOLTAGE) || (power_event==BATTERY_EVENT_LOW_VOLTAGE)) { 
 //  напряжение на батарейке вызодит за безопасные пределы
 //  сообщаем заряднику о текущем ограничении зарядного тока
 if (bat->charger != 0) {
   capi=bat->charger->api;
   current_max=bat->current_max;
   if (capi->notify_event != 0) (*capi->notify_event)(capi,4,&current_max);
 }
 // сообщаем о смене режима питания
 battery_core_external_power_changed(&bat->psy);
}

switch(temp_event) {
  case BATTERY_EVENT_HIGH_TEMPERATURE:
  case BATTERY_EVENT_PLUGOUT:
    current_max=0;
    break;
  case BATTERY_EVENT_RECOVER_FROM_LOW_TEMP:
  case BATTERY_EVENT_RECOVER_FROM_HIGH_TEMP:
  case BATTERY_EVENT_PLUGIN:
    current_max=bat->current_max;
    break;
  default:
    goto notempevent;
}    
 //  сообщаем заряднику о текущем ограничении зарядного тока
 if (bat->charger != 0) {
   capi=bat->charger->api;
   if (capi->notify_event != 0) (*capi->notify_event)(capi,4,&current_max);
 }
 // сообщаем о смене режима питания
 battery_core_external_power_changed(&bat->psy);

notempevent:

// проверяем критерии приостановки зарядки
if (bat->status == POWER_SUPPLY_STATUS_CHARGING) {
  if ((bat->disable_chg =! 0)  || ((bat->volt_now/1000) >= bat->high_voltage)) {
    i=bat->disable_chg;
    bat->disable_chg=1;
    battery_core_external_power_changed(&bat->psy);
    bat->disable_chg=i;
  }
}  

// соощение о смене режиме питания при изменении зарядна на cap_changed_margin
capdelta=abs(bat->capacity - bat->prev_capacity);
if ((capdelta >= bat->cap_changed_margin) || ((capdelta != 0) && (bat->capacity == 100))) {
  bat->prev_capacity=bat->capacity;
  power_supply_changed(&bat->psy);
}

 
 
if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING) monperiod=bat->dischg_mon_period;
    else monperiod=bat->chg_mon_period;
queue_delayed_work_on(1,bat->mon_queue,&bat->work ,msecs_to_jiffies(monperiod));
if (bat->ws.active != 0) __pm_relax(&bat->ws);
}


//*****************************************************
//* Модификация таблицы емкостей аккумулятора
//*****************************************************
int battery_core_set_battery_capacity_tables(struct battery_core_interface* bat,const char* buf,size_t count) {
  // пока заглушка
  return 0;
}  

//*****************************************************
//* Модификация таблицы ткмператур
//*****************************************************
int battery_core_set_battery_ntc_tables(struct battery_core_interface* bat,const char* buf,size_t count) {
  // пока заглушка
  return 0;
}  

//*****************************************************
//*  Сохранение sysfs-параметра
//*****************************************************
ssize_t battery_store_property(struct device *dev,struct device_attribute *attr, const char *buf, size_t count) {

struct power_supply* psy;
struct battery_core_interface* bat;
long int res=0;
int off;
int rc;

psy=dev_get_drvdata(dev);
bat=container_of(psy, struct battery_core_interface, psy);  

off=((unsigned int)attr-(unsigned int)battery_dev_attrs)/16;

// Для всех атрибутов кроме 0 и 1 аргумент в буфере - число
if (off>1) {
  rc=kstrtol(buf,10,&res);
  if (rc != 0) return rc;
}

// ветки обработки отдельных атрибутов
switch(off) {
  case 0:
    // capacity
    rc=battery_core_set_battery_capacity_tables(bat,buf,count);
    if (rc == 0) return count;
    return rc;
   
  case 1:
    // ntc
    rc=battery_core_set_battery_ntc_tables(bat,buf,count);
    if (rc == 0) return count;
    return rc;
    
  case 2: 
    // precharge_voltage        
    bat->prechare_volt=res;
    break;
    
  case 3: 
    // poweroff_voltage     
    bat->poweroff_volt=res;
    break;
    
  case 4: 
    // low_voltage
    bat->low_volt=res;
    break;
    
  case 5: 
    // recharge_voltage
    bat->recharge_volt=res;
    break;
    
  case 6: 
    // charge_done_votage       
    bat->charge_done_volt=res;
    break;
    
  case 7: 
    // temp_low_poweroff        
    bat->temp_low_poweroff=res;
    break;
    
  case 8: 
    // temp_low_disable_charge  
    bat->temp_low_disable_charge=res;
    break;
    
  case 9: 
    // temp_high_disable_charge 
    bat->temp_high_disable_charge=res;
    break;
    
  case 10: 
    // temp_high_poweroff       
    bat->temp_high_poweroff=res;
    break;
    
  case 11: 
    // temp_error_margin        
    bat->temp_error_margin=res;
    break;
    
  case 12: 
    // charging_monitor_period  
    bat->chg_mon_period=res;
    break;
    
  case 13: 
    // discharging_monitor_period
    bat->dischg_mon_period=res;
    break;
    
  case 14:
    // vbat_max                 
    bat->volt_max=res*1000;
    break;
    
  case 15:
    // ibat_max                 
    bat->current_max=res*1000;
    break;
    
  case 16:
    // test_mode                
    bat->test_mode=res;
    break;
    
  case 17:
    // disable_charging         
    bat->disable_chg=res;
    break;
    
  case 18:
    // high_voltage             
    bat->high_voltage=res;
    break;
    
  case 19:
    // capacity_changed_margin  
    bat->cap_changed_margin=res;
    break;
    
  case 20:
    // debug_mode
    bat->debug_mode=res;
    break;
}    
return count;
}

//*****************************************************
//*  Чтение sysfs-параметра
//*****************************************************
ssize_t battery_show_property(struct device *dev,struct device_attribute *attr, char* buf) {
  
struct power_supply* psy;
struct battery_core_interface* bat;
int i,res,count;
unsigned int off;
char* head_c="percentage:min,max,offset,hysteresis\n";


psy=dev_get_drvdata(dev);
bat=container_of(psy, struct battery_core_interface, psy);  

off=((unsigned int)attr-(unsigned int)battery_dev_attrs)/16;
switch(off) {
  case 0:
    // capacity
    if (bat->cap == 0) {
      res=0;
      break;
    }
    strcpy(buf,head_c);
    count=strlen(head_c);
    for(i=0;i<bat->capsize;i++) {
      count+=sprintf(buf+count,"%d%%:%d,%d,%d,%d\n",
          bat->cap[i].percent,
          bat->cap[i].vmin,
          bat->cap[i].vmax,
          bat->cap[i].offset,
          bat->cap[i].hysteresis);
    }
    return count;
    
  case 1:
    // ntc
    if (bat->ntc == 0) {
      res=0;
      break;
    }
    count=sprintf(buf,"vref=%d, vref_calib=%d, ntc_table[%d]:\n",bat->vref,bat->vref_calib,bat->ntcsize);
    for(i=0;i<bat->ntcsize;i++) {
      count+=sprintf(buf+count,"%dC:%d\n",bat->ntc[i].tntc,bat->ntc[i].tnvc);
    }
    return count;
    
  case 2:
    // precharge_voltage        
    res=bat->prechare_volt;
    break;
     
  case 3: 
    // poweroff_voltage     
    res=bat->poweroff_volt;
    break;
    
  case 4: 
    // low_voltage
    res=bat->low_volt;
    break;
    
  case 5: 
    // recharge_voltage
    res=bat->recharge_volt;
    break;
    
  case 6: 
    // charge_done_votage       
    res=bat->charge_done_volt;
    break;
    
  case 7: 
    // temp_low_poweroff        
    res=bat->temp_low_poweroff;
    break;
    
  case 8: 
    // temp_low_disable_charge  
    res=bat->temp_low_disable_charge;
    break;
    
  case 9: 
    // temp_high_disable_charge 
    res=bat->temp_high_disable_charge;
    break;
    
  case 10: 
    // temp_high_poweroff       
    res=bat->temp_high_poweroff;
    break;
    
  case 11: 
    // temp_error_margin        
    res=bat->temp_error_margin;
    break;
    
  case 12: 
    // charging_monitor_period  
    res=bat->chg_mon_period;
    break;
    
  case 13: 
    // discharging_monitor_period
    res=bat->dischg_mon_period;
    break;
    
  case 14:
    // vbat_max                 
    res=bat->volt_max/1000;
    break;
    
  case 15:
    // ibat_max                 
    res=bat->current_max/1000;
    break;
    
  case 16:
    // test_mode                
    res=bat->test_mode;
    break;
    
  case 17:
    // disable_charging         
    res=bat->disable_chg;
    break;
    
  case 18:
    // high_voltage             
    res=bat->high_voltage;
    break;
    
  case 19:
    // capacity_changed_margin  
    res=bat->cap_changed_margin;
    break;
    
  case 20:
    // debug_mode
    res=bat->debug_mode;
    break;
    
  default:
    res=0;
    break;
}
    
return sprintf(buf,"%d\n",res);
}

//*****************************************************
//*  Проверка видимости атрибутов
//*****************************************************
umode_t battery_attr_is_visible(struct kobject *kobj, struct attribute *attr, int attrno) {
  
return 420;
}


//*****************************************************
//*  Регистрация ветки параметров в sysfs
//*****************************************************
int battery_core_add_sysfs_interface(struct device *dev) {

int rc;
  
if (dev == 0) return -EINVAL;

rc=sysfs_create_group(&dev->kobj,&battery_attr_group);
if (rc != 0) pr_err("failed to add battery attrs!");

return rc;
}

//*****************************************************
//*  Удаление ветки параметров в sysfs
//*****************************************************
int  battery_core_remove_sysfs_interface(struct device *dev) {

if (dev == 0) return -EINVAL;
sysfs_remove_group(&dev->kobj,&battery_attr_group);		      
return 0;
}

//*****************************************************
//*  Регистрация в системе батарейного дарйвера
//*****************************************************
int battery_core_register(struct device* dev, struct battery_interface* api) {
  
struct battery_core_interface* bat;  
struct workqueue_struct* swq;
int rc;

if ((dev==0) || (api==0)) return -EINVAL;
if (api->bname == 0) return -EINVAL;
if (api->bname[0] == 0) return -EINVAL;


bat=kmalloc(sizeof(struct battery_core_interface),__GFP_ZERO|GFP_KERNEL);
if (bat == 0) {
  pr_err("cannot get enough memory!\n");
  return -ENOMEM;
}

bat->dev=dev;
mutex_init(&bat->lock);
wakeup_source_prepare(&bat->ws, api->bname);
wakeup_source_add(&bat->ws);

bat->status=POWER_SUPPLY_STATUS_DISCHARGING;
bat->volt_max=4350000;
bat->bname=api->bname;
bat->current_max=1000000;
bat->current_now=0;
bat->prev_capacity=0;
bat->volt_now=0;
bat->volt_avg=0;
bat->capacity=80;
bat->x448=3;
bat->temp=25;
bat->cap_changed_margin=10;
bat->present=1;
bat->health=POWER_SUPPLY_HEALTH_UNKNOWN;
bat->vref=1800000;
bat->vref_calib=1800000;
bat->debug_mode=0;
bat->test_mode=0;

bat->ntc=ntc_tvm_tables;
bat->ntcsize=35;
bat->cap=battery_capacity_table;
bat->capsize=battery_capacity_table_size;

bat->prechare_volt=3000;
bat->disable_chg=0;
bat->poweroff_volt=3064;
bat->x568=0;
bat->x484=3264;
bat->x572=0;
bat->x576=0;
bat->x580=0;
bat->x584=0;
bat->x588=0;
bat->low_volt=3600;
bat->temp_error_margin=2;
bat->high_voltage=4450;
bat->recharge_volt=4200;
bat->charge_done_volt=4350;
bat->temp_low_poweroff=-20;
bat->temp_low_disable_charge=-5;
bat->x516=-3;
bat->x520=53;
bat->temp_high_disable_charge=55;
bat->temp_high_poweroff=65;

// кросс-ссылки структур друг на друга
bat->api=api;
api->bat=bat;

bat->charger=charger_core_get_charger_interface_by_name(api->bname);
api->x_timer_suspend_proc=0;
api->alarm_wakeup_proc=battery_core_wakeup;
api->timer_resume_proc=0;
api->timer_suspend_proc=0;

bat->chg_mon_period=20000;
bat->dischg_mon_period=25000;
bat->work.work.data.counter=-32;

bat->work.work.entry.next=&bat->work.work.entry;
bat->work.work.entry.prev=&bat->work.work.entry;

bat->power_event=0;
bat->event=0;
bat->work.work.func=battery_core_monitor_work;

init_timer_key(&bat->work.timer,2,0,0);

bat->work.timer.data=(unsigned int)(&bat->work);


bat->work.timer.function=delayed_work_timer_fn;
bat->mon_queue = alloc_workqueue("batt_monitor_wq", WQ_MEM_RECLAIM, 1);
swq=bat->mon_queue;
if (bat->mon_queue == 0) {
  pr_err("failed to create_workqueue batt_monitor_wq!");
  swq=system_wq;
}
queue_delayed_work_on(1,swq,&bat->work ,msecs_to_jiffies(250));

bat->psy.name=bat->bname;
bat->psy.type=1;
bat->psy.properties=battery_core_power_props;
bat->psy.num_properties=ARRAY_SIZE(battery_core_power_props);
bat->psy.get_property=battery_core_get_property;
bat->psy.set_property=battery_core_set_property;
bat->psy.property_is_writeable=battery_core_property_is_writeable;
bat->psy.external_power_changed=battery_core_external_power_changed;

rc=power_supply_register(bat->dev,&bat->psy);
if (rc<0) {
  pr_err("failed to register psy_%s\n",bat->psy.name);
  goto err_power_supply_register_bat;
}

rc=battery_core_add_sysfs_interface(bat->psy.dev);
if (rc < 0) {
  pr_err("failed to add sysfs interface!\n");
  power_supply_unregister(&bat->psy);
  goto err_power_supply_register_bat;
}

battery_core_external_power_changed(&bat->psy);
pr_err("Battery Core Version %s(Built at %s %s)!","4.1.5f",__DATE__,__TIME__);
return 0;

// Обработка ошибок
err_power_supply_register_bat:

cancel_delayed_work_sync(&bat->work);
if (bat->mon_queue != 0) destroy_workqueue(bat->mon_queue);
wakeup_source_remove(&bat->ws);
wakeup_source_drop(&bat->ws);
mutex_destroy(&bat->lock);
kfree(bat);
return rc;
}

//*****************************************************
//*  Отключение батарейного дарйвера
//*****************************************************
void battery_core_unregister(struct device *dev,struct battery_interface *api) {

struct battery_core_interface* bat;  
  
if ((api== 0) || (api->bat == 0)) return;
bat=api->bat;

battery_core_remove_sysfs_interface(dev);
power_supply_unregister(&bat->psy);
if (bat->mon_queue != 0) destroy_workqueue(bat->mon_queue);
wakeup_source_remove(&bat->ws);
wakeup_source_drop(&bat->ws);
mutex_destroy(&bat->lock);
kfree(bat);
}



		      
		      