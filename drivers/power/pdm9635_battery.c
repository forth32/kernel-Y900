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
// все смещения вычислены по исходному дизассемблированному тексту

struct battery_interface {
  int alarm_wakeup_arg;  //0    0 battery_core_interface*
  int (*timer_resume_proc)(int);  //4
  int (*timer_suspend_proc)(int);  //8
  int (*x_timer_suspend_proc)(int,int*);  //12
  void (*alarm_wakeup_proc)(int);  //16
  char* bname;  //20
  struct battery_interface* thisptr;    //24
  int (*get_vbat_proc)(struct battery_interface*, int*);  //28
  int x32;
  int (*get_vntc_proc)(struct battery_interface*, int*);  //36
  int x40;
  int x44;
  int x48;
  int vbat;   // 52
  int tbat;   // 56
  char* name_rtcdev; //60
  int x64;
  int x68;
  struct rtc_timer rtctimer; //72
  struct rtc_device* rtcfd; //120
  struct device* parent; //124
};

//*****************************************************
//*  Функции-заглушки для проверки корректности сборки
//*****************************************************

int battery_core_register(struct device *dev, struct battery_interface *api) {
  return 0;
} 

int battery_core_unregister(struct device *dev, struct battery_interface *api) {
  return 0;
}   

//***********************************
//*  Обработчик события от таймера
//***********************************
void pmd9635_battery_alarm_wakeup(void* private) {

struct battery_interface* b9635data=private;
  
if (b9635data->alarm_wakeup_proc != 0) b9635data->alarm_wakeup_proc(b9635data->alarm_wakeup_arg);
}

//**************************************
//* Возобновление работы модуля
//**************************************
int pmd9635_battery_resume(struct platform_device* pdev) {

struct device* dev;
struct battery_interface* b9635data;

dev=&pdev->dev;
b9635data=dev_get_drvdata(dev);
if (b9635data->rtcfd != 0) rtc_timer_cancel(b9635data->rtcfd,&b9635data->rtctimer);
if (b9635data->timer_resume_proc != 0) b9635data->timer_resume_proc(b9635data->alarm_wakeup_arg);
return 0;
}

//**************************************
//* Приостановка модуля
//**************************************
int pmd9635_battery_suspend(struct platform_device* pdev, pm_message_t state) {
  
struct battery_interface* b9635data;
int ret = 0;
int var1;
struct rtc_time tm;
union {
  ktime_t kt;
  s64 longtime;
} xtime;
struct rtc_device* rd;

b9635data=dev_get_drvdata(&pdev->dev);

if (b9635data->timer_suspend_proc != 0) b9635data->timer_suspend_proc(b9635data->alarm_wakeup_arg);
if (b9635data->rtcfd == 0) {
  rd=rtc_class_open(b9635data->name_rtcdev);
  if (rd == 0) {
    pr_err("pmd9635_battery_set_wakeup_alarm: can't open rtc device(%s)!\n",b9635data->name_rtcdev);
    return 0;
  }  
  b9635data->rtcfd=rd;
}

if (b9635data->x_timer_suspend_proc != 0) ret=b9635data->x_timer_suspend_proc(b9635data->alarm_wakeup_arg,&var1);
if (ret == 0) ret=var1/1000;
  else ret=600;

rtc_timer_cancel(b9635data->rtcfd, &b9635data->rtctimer);
rtc_read_time(b9635data->rtcfd, &tm);

xtime.kt=rtc_tm_to_ktime(tm);
xtime.longtime+=(s64)ret*(s64)1000000000;
rtc_timer_start(b9635data->rtcfd, &b9635data->rtctimer, xtime.kt, (ktime_t)((s64)0) );
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
int pmd9635_battery_get_vbat(struct battery_interface* b9635data, int* val) {

int vbat_channel;  
int ret;

if ((b9635data == 0) || (val == 0)) return -EINVAL;

vbat_channel=b9635data->vbat;
ret=pmd9635_get_adc_value(vbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vbat: can't get battery voltage, rc=%d\n",ret);
return ret;
}

//**************************************
//*  Чтение температуры аккумулятора
//**************************************
int pmd9635_battery_get_vntc(struct battery_interface* b9635data, int* val) {

int tbat_channel;  
int ret;

if ((b9635data == 0) || (val == 0)) return -EINVAL;

tbat_channel=b9635data->tbat;
ret=pmd9635_get_adc_value(tbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vntc: can't get battery temperature, rc=%d\n",ret);
return ret;
}


  
//***********************************************
//*  Конструктор модуля
//***********************************************
static int pmd9635_battery_probe(struct platform_device *pdev) {


const char* procname="pmd9635_battery_probe"; 
static char* bname="battery";
char* rtcdevname="rtc0";
int ret;
struct rtc_device* rd;

struct battery_interface* b9635data;
int vbat_channel, tbat_channel;
struct device* dparent;

if ((pdev == 0) || (pdev->dev.of_node == 0)) return -EINVAL;

b9635data=kmalloc(sizeof(struct battery_interface),__GFP_ZERO|GFP_KERNEL);
if (b9635data == 0) {
  pr_err("%s: Can't allocate memory!",procname);
  return -ENOMEM;
}

dparent=pdev->dev.parent;
b9635data->parent=dparent;

b9635data->bname=bname;
b9635data->thisptr=b9635data;

dev_set_drvdata(&pdev->dev,b9635data);

if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,vbat-channel", &vbat_channel, 1) != 0) {
  pr_err("%s: failed to get vbat channel!\n",procname);
  return -EPERM;
}
b9635data->vbat=vbat_channel;
  
if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,tbat-channel", &tbat_channel, 1) != 0) {
  pr_err("%s: failed to get tbat channel!\n",procname);
  return -EPERM;
}
b9635data->tbat=tbat_channel;

if ((vbat_channel<0) && (tbat_channel<0)) {
  dev_set_drvdata(&pdev->dev,0);
  kfree(b9635data);
  return 0;
}
 
if (vbat_channel>=0)  b9635data->get_vbat_proc=&pmd9635_battery_get_vbat;
 else b9635data->get_vbat_proc=0;

if (tbat_channel>=0)  b9635data->get_vntc_proc=&pmd9635_battery_get_vntc;
 else b9635data->get_vntc_proc=0;
 
ret=battery_core_register(dparent,b9635data);
if (ret != 0) {
  pr_err("%s: fail to register battery core, rc=%d!\n",procname,ret);
  dev_set_drvdata(dparent,0);
  kfree(b9635data);
  return ret;
}
 
b9635data->name_rtcdev=rtcdevname;
rd=rtc_class_open(rtcdevname);
b9635data->rtcfd=rd;
rtc_timer_init(&b9635data->rtctimer,&pmd9635_battery_alarm_wakeup,(void*)b9635data);

printk(KERN_INFO "%s: vbat_channel=%d, tbat_channel=%d\n",procname,vbat_channel,tbat_channel);
return 0;
}


//**************************************
//* Деструктор модуля
//**************************************
static int pmd9635_battery_remove(struct platform_device *pdev) {
struct device* dparent;
struct battery_interface* b9635data;

dparent=&pdev->dev;
b9635data=dev_get_drvdata(dparent);

if (b9635data->rtcfd != 0) rtc_class_close(b9635data->rtcfd);
battery_core_unregister(b9635data->parent,b9635data);
kfree(b9635data);
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
		   .name = "pmd9635_battery",
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
