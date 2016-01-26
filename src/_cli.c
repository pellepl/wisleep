/*
 * cli.c
 *
 *  Created on: Jul 24, 2012
 *      Author: petera
 */

#include "_cli.h"

#include "uart_driver.h"
#include "taskq.h"
#include "miniutils.h"
#include "system.h"

#include "app.h"

#include "gpio.h"

#ifdef CONFIG_I2C
#include "i2c_driver.h"
#ifdef CONFIG_I2C_DEVICE
#include "i2c_dev.h"
#include "hmc5883l_driver.h"
#include "adxl345_driver.h"
#endif
#endif

#include "ws2812b_spi_stm32f1.h"


#define CLI_PROMPT "> "
#define IS_STRING(s) ((u8_t*)(s) >= (u8_t*)in && (u8_t*)(s) < (u8_t*)in + sizeof(in))

typedef int (*func)(int a, ...);

typedef struct cmd_s {
  const char* name;
  const func fn;
  const char* help;
} cmd;

struct {
  uart_rx_callback prev_uart_rx_f;
  void *prev_uart_arg;
} cli_state;

static u8_t in[256];

static int _argc;
static void *_args[16];

static int f_uwrite(int uart, char* data);
static int f_uread(int uart, int numchars);
static int f_uconf(int uart, int speed);

static int f_rand();

static int f_reset();
static int f_time(int d, int h, int m, int s, int ms);
static int f_help(char *s);
static int f_dump();
static int f_dump_trace();
static int f_assert();
static int f_dbg();
static int f_build();

static int f_memfind(int hex);

#ifdef CONFIG_I2C
static int f_i2c_read(int addr, int reg);
static int f_i2c_write(int addr, int reg, int data);
static int f_i2c_scan(void);
static int f_i2c_reset(void);

static int f_hmc_open(void);
static int f_hmc_cfg(void);
static int f_hmc_check(void);
static int f_hmc_read(void);
static int f_hmc_drdy(void);

static int f_adxl_open(void);
static int f_adxl_check(void);
static int f_adxl_cfg(void);
static int f_adxl_read(void);
static int f_adxl_stat(void);
#endif

static int f_ws_test(int seq);

static void cli_print_app_name(void);

/////////////////////////////////////////////////////////////////////////////////////////////

static cmd c_tbl[] = {
    { .name = "dump", .fn = (func) f_dump,
        .help = "Dumps state of all system\n"
    },
    { .name = "dump_trace", .fn = (func) f_dump_trace, .help = "Dumps system trace\n"
    },
    { .name = "time", .fn = (func) f_time,
        .help = "Prints or sets time\n"
        "time or time <day> <hour> <minute> <second> <millisecond>\n"
    },
    { .name = "uwrite", .fn = (func) f_uwrite,
        .help = "Writes to uart\n"
        "uwrite <uart> <string>\n"
            "ex: uwrite 2 \"foo\"\n"
    },
    { .name = "uread", .fn = (func) f_uread,
        .help = "Reads from uart\n"
        "uread <uart> (<numchars>)\n"
            "numchars - number of chars to read, if omitted uart is drained\n"
            "ex: uread 2 10\n"
    },
    { .name = "uconf", .fn = (func) f_uconf,
        .help = "Configure uart\n"
        "uconf <uart> <speed>\n"
            "ex: uconf 2 9600\n"
    },


#ifdef CONFIG_I2C
    { .name = "i2c_r", .fn = (func) f_i2c_read,
        .help = "i2c read reg\n"
    },
    { .name = "i2c_w", .fn = (func) f_i2c_write,
        .help = "i2c write reg\n"
    },
    { .name = "i2c_scan", .fn = (func) f_i2c_scan,
        .help = "scans i2c bus for all addresses\n"
    },
    { .name = "i2c_reset", .fn = (func) f_i2c_reset,
        .help = "reset i2c bus\n"
    },

    { .name = "hmc_open", .fn = (func) f_hmc_open,
        .help = "Open HMC device\n"
    },
    { .name = "hmc_cfg", .fn = (func) f_hmc_cfg,
        .help = "Configures HMC\n"
    },
    { .name = "hmc_check", .fn = (func) f_hmc_check,
        .help = "Check HMC ID\n"
    },
    { .name = "hmc_read", .fn = (func) f_hmc_read,
        .help = "Read HMC data\n"
    },
    { .name = "hmc_drdy", .fn = (func) f_hmc_drdy,
        .help = "Check HMC data ready\n"
    },

    { .name = "adxl_open", .fn = (func) f_adxl_open,
        .help = "Open ADXL device\n"
    },
    { .name = "adxl_cfg", .fn = (func) f_adxl_cfg,
        .help = "Open ADXL device\n"
    },
    { .name = "adxl_check", .fn = (func) f_adxl_check,
        .help = "Check ADXL ID\n"
    },
    { .name = "adxl_read", .fn = (func) f_adxl_read,
        .help = "Read ADXL Data\n"
    },
    { .name = "adxl_sr", .fn = (func) f_adxl_stat,
        .help = "Read ADXL status\n"
    },

#endif

    { .name = "ws_test", .fn = (func) f_ws_test,
        .help = "test ws2812b driver\nws_test <0-?> - arg is different sequences\n" },


    { .name = "dbg", .fn = (func) f_dbg,
        .help = "Set debug filter and level\n"
        "dbg (level <dbg|info|warn|fatal>) (enable [x]*) (disable [x]*)\n"
        "x - <task|heap|comm|cnc|cli|nvs|spi|all>\n"
        "ex: dbg level info disable all enable cnc comm\n"
    },
    {.name = "memfind", .fn = (func) f_memfind,
        .help = "Searches for hex in memory\n"
            "memfind 0xnnnnnnnn\n"
    },
    { .name = "assert", .fn = (func) f_assert,
        .help = "Asserts system\n"
            "NOTE system will need to be rebooted\n"
    },
    { .name = "rand", .fn = (func) f_rand,
        .help = "Generates pseudo random sequence\n"
    },
    { .name = "reset", .fn = (func) f_reset,
        .help = "Resets system\n"
    },
    { .name = "build", .fn = (func) f_build,
        .help = "Outputs build info\n"
    },
    { .name = "help", .fn = (func) f_help,
        .help = "Prints help\n"
        "help or help <command>\n"
    },
    { .name = "?", .fn = (func) f_help,
        .help = "Prints help\n"
        "help or help <command>\n" },

    // menu end marker
    { .name = NULL, .fn = (func) 0, .help = NULL },
  };

/////////////////////////////////////////////////////////////////////////////////////////////


static int f_rand() {
  print("%08x\n", rand_next());
  return 0;
}

static int f_reset() {
  SYS_reboot(REBOOT_USER);
  return 0;
}

static int f_time(int ad, int ah, int am, int as, int ams) {
  if (_argc == 0) {
    u16_t d, ms;
    u8_t h, m, s;
    SYS_get_time(&d, &h, &m, &s, &ms);
    print("day:%i time:%02i:%02i:%02i.%03i\n", d, h, m, s, ms);
  } else if (_argc == 5) {
    SYS_set_time(ad, ah, am, as, ams);
  } else {
    return -1;
  }
  return 0;
}

#ifdef DBG_OFF
static int f_dbg() {
  print("Debug disabled compile-time\n");
  return 0;
}
#else
const char* DBG_BIT_NAME[] = _DBG_BIT_NAMES;

static void print_debug_setting() {
  print("DBG level: %i\n", SYS_dbg_get_level());
  int d;
  for (d = 0; d < sizeof(DBG_BIT_NAME) / sizeof(const char*); d++) {
    print("DBG mask %s: %s\n", DBG_BIT_NAME[d],
        SYS_dbg_get_mask() & (1 << d) ? "ON" : "OFF");
  }
}

static int f_dbg() {
  enum state {
    NONE, LEVEL, ENABLE, DISABLE
  } st = NONE;
  int a;
  if (_argc == 0) {
    print_debug_setting();
    return 0;
  }
  for (a = 0; a < _argc; a++) {
    u32_t f = 0;
    char *s = (char*) _args[a];
    if (!IS_STRING(s)) {
      return -1;
    }
    if (strcmp("level", s) == 0) {
      st = LEVEL;
    } else if (strcmp("enable", s) == 0) {
      st = ENABLE;
    } else if (strcmp("disable", s) == 0) {
      st = DISABLE;
    } else {
      switch (st) {
      case LEVEL:
        if (strcmp("dbg", s) == 0) {
          SYS_dbg_level(D_DEBUG);
        } else if (strcmp("info", s) == 0) {
          SYS_dbg_level(D_INFO);
        } else if (strcmp("warn", s) == 0) {
          SYS_dbg_level(D_WARN);
        } else if (strcmp("fatal", s) == 0) {
          SYS_dbg_level(D_FATAL);
        } else {
          return -1;
        }
        break;
      case ENABLE:
      case DISABLE: {
        int d;
        for (d = 0; f == 0 && d < sizeof(DBG_BIT_NAME) / sizeof(const char*);
            d++) {
          if (strcmp(DBG_BIT_NAME[d], s) == 0) {
            f = (1 << d);
          }
        }
        if (strcmp("all", s) == 0) {
          f = D_ANY;
        }
        if (f == 0) {
          return -1;
        }
        if (st == ENABLE) {
          SYS_dbg_mask_enable(f);
        } else {
          SYS_dbg_mask_disable(f);
        }
        break;
      }
      default:
        return -1;
      }
    }
  }
  print_debug_setting();
  return 0;
}
#endif

static int f_assert() {
  ASSERT(FALSE);
  return 0;
}

static int f_build(void) {
  cli_print_app_name();
  print("\n");
  print("SYS_MAIN_TIMER_FREQ %i\n", SYS_MAIN_TIMER_FREQ);
  print("SYS_TIMER_TICK_FREQ %i\n", SYS_TIMER_TICK_FREQ);
  print("UART2_SPEED %i\n", UART2_SPEED);
  print("CONFIG_TASK_POOL %i\n", CONFIG_TASK_POOL);

  return 0;
}

static int f_uwrite(int uart, char* data) {
  if (_argc != 2 || !IS_STRING(data)) {
    return -1;
  }
  if (uart < 0 || uart >= CONFIG_UART_CNT) {
    return -1;
  }
  char c;
  while ((c = *data++) != 0) {
    UART_put_char(_UART(uart), c);
  }
  return 0;
}

static int f_uread(int uart, int numchars) {
  if (_argc < 1 || _argc > 2) {
    return -1;
  }
  if (uart < 0 || uart >= CONFIG_UART_CNT) {
    return -1;
  }
  if (_argc == 1) {
    numchars = 0x7fffffff;
  }
  int l = UART_rx_available(_UART(uart));
  l = MIN(l, numchars);
  int ix = 0;
  while (ix++ < l) {
    print("%c", UART_get_char(_UART(uart)));
  }
  print("\n%i bytes read\n", l);
  return 0;
}

static int f_uconf(int uart, int speed) {
  if (_argc != 2) {
    return -1;
  }
  if (IS_STRING(uart) || IS_STRING(speed) || uart < 0 || uart >= CONFIG_UART_CNT) {
    return -1;
  }
  UART_config(_UART(uart), speed,
      UART_DATABITS_8, UART_STOPBITS_1, UART_PARITY_NONE, UART_FLOWCONTROL_NONE, TRUE);

  return 0;
}


#ifdef CONFIG_I2C

static u8_t i2c_dev_reg = 0;
static u8_t i2c_dev_val = 0;
static i2c_dev i2c_device;
static u8_t i2c_wr_data[2];
static i2c_dev_sequence i2c_r_seqs[] = {
    I2C_SEQ_TX(&i2c_dev_reg, 1),
    I2C_SEQ_RX_STOP(&i2c_dev_val, 1)
};
static i2c_dev_sequence i2c_w_seqs[] = {
    I2C_SEQ_TX_STOP(i2c_wr_data, 2) ,
};

static void i2c_test_cb(i2c_dev *dev, int result) {
  print("i2c_dev_cb: reg:0x%02x val:0x%02x 0b%08b res:%i\n", i2c_dev_reg, i2c_dev_val, i2c_dev_val,
      result);
  I2C_DEV_close(&i2c_device);
}

static int f_i2c_read(int addr, int reg) {
  I2C_DEV_init(&i2c_device, 100000, _I2C_BUS(0), addr);
  I2C_DEV_set_callback(&i2c_device, i2c_test_cb);
  I2C_DEV_open(&i2c_device);
  i2c_dev_reg = reg;
  int res = I2C_DEV_sequence(&i2c_device, i2c_r_seqs, 2);
  if (res != I2C_OK) print("err: %i\n", res);
  return 0;
}

static int f_i2c_write(int addr, int reg, int data) {
  I2C_DEV_init(&i2c_device, 100000, _I2C_BUS(0), addr);
  I2C_DEV_set_callback(&i2c_device, i2c_test_cb);
  I2C_DEV_open(&i2c_device);
  i2c_wr_data[0] = reg;
  i2c_wr_data[1] = data;
  i2c_dev_reg = reg;
  i2c_dev_val = data;
  int res = I2C_DEV_sequence(&i2c_device, i2c_w_seqs, 1);
  if (res != I2C_OK) print("err: %i\n", res);
  return 0;
}

static u8_t i2c_scan_addr;
extern task_mutex i2c_mutex;

void i2c_scan_report_task(u32_t addr, void *res) {
  if (addr == 0) {
    print("\n    0  2  4  6  8  a  c  e");
  }
  if ((addr & 0x0f) == 0) {
    print("\n%02x ", addr & 0xf0);
  }

  print("%s", (char *) res);

  if (i2c_scan_addr < 0xee) {
    i2c_scan_addr += 2;
    I2C_query(_I2C_BUS(0), i2c_scan_addr);
  } else {
    print("\n");
    TASK_mutex_unlock(&i2c_mutex);
  }
}

static void i2c_scan_cb_irq(i2c_bus *bus, int res) {
  task *report_scan_task = TASK_create(i2c_scan_report_task, 0);

  TASK_run(report_scan_task, bus->addr & 0xfe, res == I2C_OK ? "UP " : ".. ");
}

static int f_i2c_scan(void) {
  if (!TASK_mutex_try_lock(&i2c_mutex)) {
    print("i2c busy\n");
    return 0;
  }
  i2c_scan_addr = 0;
  int res = I2C_config(_I2C_BUS(0), 10000);
  if (res != I2C_OK) print("i2c config err %i\n", res);
  res = I2C_set_callback(_I2C_BUS(0), i2c_scan_cb_irq);
  if (res != I2C_OK) print("i2c cb err %i\n", res);
  res = I2C_query(_I2C_BUS(0), i2c_scan_addr);
  if (res != I2C_OK) print("i2c query err %i\n", res);
  return 0;
}

static int f_i2c_reset(void) {
  I2C_reset(_I2C_BUS(0));
  // force release of i2c mutex
  TASK_mutex_try_lock(&i2c_mutex);
  TASK_mutex_unlock(&i2c_mutex);
  return 0;
}

static hmc5883l_dev hmc_dev;
static hmc_reading hmc_data;
static bool hmc_bool;

static void hmc_cb(hmc5883l_dev *dev, hmc_state state, int res) {
  if (res < 0) print("hmc_cb err %i\n", res);
  switch (state) {
  case HMC5883L_STATE_CONFIG:
    print("hmc cfg ok\n");
    break;
  case HMC5883L_STATE_READ:
    print("hmc x:%i  y:%i  z:%i\n", hmc_data.x, hmc_data.y, hmc_data.z);
    break;
  case HMC5883L_STATE_READ_DRDY:
    print("hmc drdy: %s\n", hmc_bool ? "TRUE":"FALSE");
    break;
  case HMC5883L_STATE_ID:
    print("hmc id ok: %s\n", hmc_bool ? "TRUE":"FALSE");
    break;
  default:
    print("hmc_cb unknown state %02x\n", state);
    break;
  }
}

static int f_hmc_open(void) {
  hmc_open(&hmc_dev, _I2C_BUS(0), 100000, hmc_cb);
  return 0;
}
static int f_hmc_cfg(void) {
  int res = hmc_config(&hmc_dev,
      hmc5883l_mode_continuous,
      hmc5883l_i2c_speed_normal,
      hmc5883l_gain_1_3,
      hmc5883l_measurement_mode_normal,
      hmc5883l_data_output_3,
      hmc5883l_samples_avg_4);
  if (res != 0) print("err:%i\n", res);
  return 0;
}
static int f_hmc_check(void) {
  int res = hmc_check_id(&hmc_dev, &hmc_bool);
  if (res != 0) print("err:%i\n", res);
  return 0;
}
static int f_hmc_read(void) {
  int res = hmc_read(&hmc_dev, &hmc_data);
  if (res != 0) print("err:%i\n", res);
  return 0;
}
static int f_hmc_drdy(void) {
  int res = hmc_drdy(&hmc_dev, &hmc_bool);
  if (res != 0) print("err:%i\n", res);
  return 0;
}

static adxl345_dev adxl_dev;
static bool adxl_bool;
static adxl_reading adxl_data;
static adxl_status adxl_sr;
static volatile bool adxl_busy;

static void adxl_cb(adxl345_dev *dev, adxl_state state, int res) {
  if (res < 0) print("adxl_cb err %i\n", res);
  switch (state) {
  case ADXL345_STATE_ID:
    print("adxl id ok: %s\n", adxl_bool ? "TRUE":"FALSE");
    break;
  case ADXL345_STATE_READ:
    print("adxl data: %04x %04x %04x\n", adxl_data.x, adxl_data.y, adxl_data.z);
    break;
  case ADXL345_STATE_READ_ALL_STATUS:
    print("adxl state:\n"
        "  int raw       : %08b\n"
        "  int dataready : %i\n"
        "  int activity  : %i\n"
        "  int inactivity: %i\n"
        "  int sgl tap   : %i\n"
        "  int dbl tap   : %i\n"
        "  int freefall  : %i\n"
        "  int overrun   : %i\n"
        "  int watermark : %i\n"
        "  acttapsleep   : %08b\n"
        "  act x y z     : %i %i %i\n"
        "  tap x y z     : %i %i %i\n"
        "  sleep         : %i\n"
        "  fifo trigger  : %i\n"
        "  entries       : %i\n"
        ,
        adxl_sr.int_src,
        (adxl_sr.int_src & ADXL345_INT_DATA_READY) != 0,
        (adxl_sr.int_src & ADXL345_INT_ACTIVITY) != 0,
        (adxl_sr.int_src & ADXL345_INT_INACTIVITY) != 0,
        (adxl_sr.int_src & ADXL345_INT_SINGLE_TAP) != 0,
        (adxl_sr.int_src & ADXL345_INT_DOUBLE_TAP) != 0,
        (adxl_sr.int_src & ADXL345_INT_FREE_FALL) != 0,
        (adxl_sr.int_src & ADXL345_INT_OVERRUN) != 0,
        (adxl_sr.int_src & ADXL345_INT_WATERMARK) != 0,
        adxl_sr.act_tap_status,
        adxl_sr.act_tap_status.act_x,
        adxl_sr.act_tap_status.act_y,
        adxl_sr.act_tap_status.act_z,
        adxl_sr.act_tap_status.tap_x,
        adxl_sr.act_tap_status.tap_y,
        adxl_sr.act_tap_status.tap_z,
        adxl_sr.act_tap_status.asleep,
        adxl_sr.fifo_status.fifo_trig,
        adxl_sr.fifo_status.entries

        );
    break;
  case ADXL345_STATE_CONFIG_ACTIVITY:
  case ADXL345_STATE_CONFIG_FIFO:
  case ADXL345_STATE_CONFIG_FORMAT:
  case ADXL345_STATE_CONFIG_FREEFALL:
  case ADXL345_STATE_CONFIG_INTERRUPTS:
  case ADXL345_STATE_CONFIG_POWER:
  case ADXL345_STATE_CONFIG_TAP:
    print("adxl cfg ok: %02x\n", state);
    break;
  default:
    print("adxl_cb unknown state %02x\n", state);
    break;
  }
  adxl_busy = FALSE;
}

static int f_adxl_open(void) {
  adxl_open(&adxl_dev, _I2C_BUS(0), 100000, adxl_cb);
  return 0;
}
static int f_adxl_check(void) {
  int res = adxl_check_id(&adxl_dev, &adxl_bool);
  if (res != 0) print("err:%i\n", res);
  return 0;
}
static int f_adxl_cfg(void) {
  int res;

  adxl_busy = TRUE;
  res = adxl_config_power(&adxl_dev, FALSE, ADXL345_RATE_12_5_LP, TRUE, FALSE, ADXL345_MODE_MEASURE, ADXL345_SLEEP_OFF);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_fifo(&adxl_dev, ADXL345_FIFO_BYPASS, ADXL345_PIN_INT1, 0);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_format(&adxl_dev, FALSE, TRUE, FALSE, ADXL345_RANGE_2G);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_freefall(&adxl_dev, 0, 0);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_interrupts(&adxl_dev, ADXL345_INT_SINGLE_TAP | ADXL345_INT_INACTIVITY | ADXL345_INT_ACTIVITY, 0);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_tap(&adxl_dev, ADXL345_XYZ, 0x40, 0x30, 0x40, 0xff , FALSE);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  adxl_busy = TRUE;
  res = adxl_config_activity(&adxl_dev, ADXL345_AC, ADXL345_XYZ, ADXL345_XYZ, 12, 24, 5);
  if (res != 0) {print("err:%i\n", res); return 0;}
  while (adxl_busy);

  return 0;
}
static int f_adxl_read(void) {
  int res = adxl_read_data(&adxl_dev, &adxl_data);
  if (res != 0) print("err:%i\n", res);
  return 0;
}
static int f_adxl_stat(void) {
  int res = adxl_read_status(&adxl_dev, &adxl_sr);
  if (res != 0) print("err:%i\n", res);
  return 0;
}

#endif // CONFIG_I2C

static int f_ws_test(int seq) {
  u32_t rgb = 0;
  if (seq == 100) {
    for (rgb = 0; rgb < 1000; rgb++) {
      WS2812B_STM32F1_set(rand_next());
      if (rgb != 0 && (rgb % WS2812B_NBR_OF_LEDS) == 0) {
        WS2812B_STM32F1_output();
        SYS_hardsleep_ms(75);
      }
    }
    return 0;
  }
  if (seq == 200) {
    for (rgb = 0; rgb < 0x200; rgb++) {
      int i;
      for (i = 0; i < WS2812B_NBR_OF_LEDS; i++) {
        WS2812B_STM32F1_set(0x010101 * (rgb & 0xff));
      }
      WS2812B_STM32F1_output();
      SYS_hardsleep_ms(50);
    }
    return 0;
  }
  if (seq == 1000) {
    WS2812B_STM32F1_output_test_pattern();
    return 0;
  }
  switch (seq % 10) {
  case 1:
    rgb = 0x404040; break;
  case 2:
    rgb = 0x000040; break;
  case 3:
    rgb = 0x004000; break;
  case 4:
    rgb = 0x400000; break;
  case 5:
    rgb = 0x004040; break;
  case 6:
    rgb = 0x400040; break;
  case 7:
    rgb = 0x404000; break;
  }
  if (seq < 10) {
    int i;
    for (i = 0; i < WS2812B_NBR_OF_LEDS; i++) {
      WS2812B_STM32F1_set(rgb);
    }
    WS2812B_STM32F1_output();
  } else {
    int j;
    for (j = 0; j < WS2812B_NBR_OF_LEDS; j++) {
      int i;
      for (i = 0; i < WS2812B_NBR_OF_LEDS; i++) {
        WS2812B_STM32F1_set(i == j ? rgb : 0);
      }
      WS2812B_STM32F1_output();
      SYS_hardsleep_ms(100);
    }

  }

  return 0;
}



static int f_help(char *s) {
  if (IS_STRING(s)) {
    int i = 0;
    while (c_tbl[i].name != NULL ) {
      if (strcmp(s, c_tbl[i].name) == 0) {
        print("%s\t%s", c_tbl[i].name, c_tbl[i].help);
        return 0;
      }
      i++;
    }
    print("%s\tno such command\n", s);
  } else {
    print ("  ");
    cli_print_app_name();
    print("\n");
    int i = 0;
    while (c_tbl[i].name != NULL ) {
      int len = strpbrk(c_tbl[i].help, "\n") - c_tbl[i].help;
      char tmp[64];
      strncpy(tmp, c_tbl[i].help, len + 1);
      tmp[len + 1] = 0;
      char fill[24];
      int fill_len = sizeof(fill) - strlen(c_tbl[i].name);
      memset(fill, ' ', sizeof(fill));
      fill[fill_len] = 0;
      print("  %s%s%s", c_tbl[i].name, fill, tmp);
      i++;
    }
  }
  return 0;
}

static int f_dump() {
  print("FULL DUMP\n=========\n");
  TASK_dump(IOSTD);
  print("\n");
  print("APP specifics\n-------------\n");
  print("I2C mutex\n");
  print("  taken: %s\n", i2c_mutex.taken ? "YES":"NO");
  if (i2c_mutex.taken) {
    print("  entries: %i\n", i2c_mutex.entries);
    print("  owner:   0x%08x  [func:0x%08x]\n", i2c_mutex.owner, i2c_mutex.owner->f);
    print("  reentr.: %s\n", i2c_mutex.reentrant ? "YES":"NO");
  }
  print("\n");

  return 0;
}

static int f_dump_trace() {
#ifdef DBG_TRACE_MON
  SYS_dump_trace(IOSTD);
#else
  print("trace not enabled\n");
#endif
  return 0;
}

static int f_memfind(int hex) {
  u8_t *addr = (u8_t*)SRAM_BASE;
  int i;
  print("finding 0x%08x...\n", hex);
  for (i = 0; i < 20*1024 - 4; i++) {
    u32_t m =
        (addr[i]) |
        (addr[i+1]<<8) |
        (addr[i+2]<<16) |
        (addr[i+3]<<24);
    u32_t rm =
        (addr[i+3]) |
        (addr[i+2]<<8) |
        (addr[i+1]<<16) |
        (addr[i]<<24);
    if (m == hex) {
      print("match found @ 0x%08x\n", i + addr);
    }
    if (rm == hex) {
      print("reverse match found @ 0x%08x\n", i + addr);
    }
  }
  print("finished\n");
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void CLI_TASK_on_input(u32_t len, void *p) {
  if (len > sizeof(in)) {
    DBG(D_CLI, D_WARN, "CONS input overflow\n");
    print(CLI_PROMPT);
    return;
  }
  u32_t rlen = UART_get_buf(_UART(UARTSTDIN), in, MIN(len, sizeof(in)));
  if (rlen != len) {
    DBG(D_CLI, D_WARN, "CONS length mismatch\n");
    print(CLI_PROMPT);
    return;
  }
  cursor cursor;
  strarg_init(&cursor, (char*) in, rlen);
  strarg arg;
  _argc = 0;
  func fn = NULL;
  int ix = 0;

  // parse command and args
  while (strarg_next(&cursor, &arg)) {
    if (arg.type == INT) {
      //DBG(D_CLI, D_DEBUG, "CONS arg %i:\tlen:%i\tint:%i\n",arg_c, arg.len, arg.val);
    } else if (arg.type == STR) {
      //DBG(D_CLI, D_DEBUG, "CONS arg %i:\tlen:%i\tstr:\"%s\"\n", arg_c, arg.len, arg.str);
    }
    if (_argc == 0) {
      // first argument, look for command function
      if (arg.type != STR) {
        break;
      } else {
        while (c_tbl[ix].name != NULL ) {
          if (strcmp(arg.str, c_tbl[ix].name) == 0) {
            fn = c_tbl[ix].fn;
            break;
          }
          ix++;
        }
        if (fn == NULL ) {
          break;
        }
      }
    } else {
      // succeeding arguments¸ store them in global vector
      if (_argc - 1 >= 16) {
        DBG(D_CLI, D_WARN, "CONS too many args\n");
        fn = NULL;
        break;
      }
      _args[_argc - 1] = (void*) arg.val;
    }
    _argc++;
  }

  // execute command
  if (fn) {
    _argc--;
    DBG(D_CLI, D_DEBUG, "CONS calling [%p] with %i args\n", fn, _argc);
    int res = (int) _variadic_call(fn, _argc, _args);
    if (res == -1) {
      print("%s", c_tbl[ix].help);
    } else {
      print("OK\n");
    }
  } else {
    print("unknown command - try help\n");
  }
  print(CLI_PROMPT);
}

void CLI_timer() {
}

void CLI_uart_check_char(void *a, u8_t c) {
  if (c == '\n') {
    task *t = TASK_create(CLI_TASK_on_input, 0);
    TASK_run(t, UART_rx_available(_UART(UARTSTDIN)), NULL);
  }
}

DBG_ATTRIBUTE static u32_t __dbg_magic;

void CLI_init() {
  if (__dbg_magic != 0x43215678) {
    __dbg_magic = 0x43215678;
    SYS_dbg_level(D_WARN);
    SYS_dbg_mask_set(0);
  }
  memset(&cli_state, 0, sizeof(cli_state));
  DBG(D_CLI, D_DEBUG, "CLI init\n");
  UART_set_callback(_UART(UARTSTDIN), CLI_uart_check_char, NULL);
  print ("\n");
  print(APP_NAME);
  print("\n\n");
  print("build     : %i\n", SYS_build_number());
  print("build date: %i\n", SYS_build_date());
  print("\ntype '?' or 'help' for list of commands\n\n");
  print(CLI_PROMPT);
}

static void cli_print_app_name(void) {
  print (APP_NAME);
}
