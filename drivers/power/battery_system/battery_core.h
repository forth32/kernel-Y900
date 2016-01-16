int32_t jrd_qpnp_vadc_read(enum qpnp_vadc_channels channel,struct qpnp_vadc_result *result);

//*************************************************
//* интерфейсная структура драйвера батарейки
//*************************************************
// все смещения вычислены по исходному дизассемблированному тексту

struct battery_interface {
  struct battery_core_interface* bat;  //0    0 battery_core_interface*
  int (*timer_resume_proc)(struct battery_core_interface*);  //4
  int (*timer_suspend_proc)(struct battery_core_interface*);  //8
  int (*x_timer_suspend_proc)(struct battery_core_interface*,int*);  //12
  int (*alarm_wakeup_proc)(void*);  //16
  char* bname;  //20
  struct battery_interface* thisptr;    //24
  int (*get_vbat_proc)(struct battery_interface*, int*);  //28
  int bx32;
  int (*get_vntc_proc)(struct battery_interface*, int*);  //36
  int (*x40)(struct battery_interface*, int*);
  int bx44;
  int bx48;
  int vbat;   // 52
  int tbat;   // 56
  char* name_rtcdev; //60
  int bx64;
  int bx68;
  struct rtc_timer rtctimer; //72
  struct rtc_device* rtcfd; //120
  struct device* parent; //124
};


//*****************************************************
//*  Главная интерфейсная структура battery_core
//*****************************************************

// 592 байта
struct battery_core_interface {
   struct battery_interface* api;   // 0
   struct charger_core_interface* charger; // 4
   struct device* dev;  // 8
   struct mutex lock; //12, 40 байт
   int x52;
   struct wakeup_source ws;  // 56, размер 152   
   struct power_supply psy;  // 208, размер 148

   struct workqueue_struct * mon_queue; // 316

//---------------------------------  
   struct delayed_work work; // 320, размер 76: 320-392
   // struct work_struct work
   //             atomic_long_t data;      320
   //             struct list_head entry;  324-328 
   //			  work_func_t func;		   332
   // struct timer_list timer; // 336, размер 52: 336-384
   //			  struct list_head entry; 336-340
   //			  unsigned long expires; 344
   //			  struct tvec_base *base; 348
   //             void (*function)(unsigned long); 352
   //             unsigned long data; 356
   //             int slack; 360
   //             int start_pid; 364
   //             void *start_site; 368
   //             char start_comm[16]; 372-388
   // struct workqueue_struct *wq; 392
   // int cpu; 396
//-----------------------------------
//---void (*battery_core_monitor_work)(work_struct *); //332
//---void (*function)(unsigned int); //352
//   struct delayed_work* pwork; // 356
   
// int x392;
   int chg_mon_period; // 396
   int dischg_mon_period;
   int new_status; // 404
   int x408;
   char* bname;  //412
   int status;        //416
   int current_now;   //420
   int current_max;   //424
   int volt_now;      //428
   int volt_avg;      //432
   int volt_max;      //436
   int capacity;      //440
   int x444;        
   int x448;
   int present;       //452
   int temp;          //456
   int health;        //460
   int debug_mode;    //464
   int test_mode;     //468
   int disable_chg;      //472
   int cap_changed_margin; //476
   int prechare_volt;  //480
   int x484;
   int poweroff_volt;  //488
   int low_volt;  //492
   int high_voltage;  //496
   int recharge_volt;    // 500
   int charge_done_volt; //504
   int temp_low_poweroff;   // 508
   int temp_low_disable_charge;   // 512
   int x516;
   int x520;
   int temp_high_disable_charge; //524
   int temp_high_poweroff;    //528
   int temp_error_margin;   //532  
   int vref;              // 536
   int vref_calib;        // 540
   struct ntc_tvm* ntc;  // 544
   int ntcsize;          // 548
   struct capacity* cap; //552
   int capsize;  //556	
   int x560;
   int x564;
   int x568;
   int x572;
   int x576;
   int x580;
   int x584;
   int x588;
};   


// Прототипы внешних подпрограмм

int battery_core_register(struct device* dev, struct battery_interface* api);
void battery_core_unregister(struct device *dev, struct battery_interface *api);
