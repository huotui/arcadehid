/*
 * cli.c
 *
 *  Created on: Jul 24, 2012
 *      Author: petera
 */

#include "cli.h"
#include "io.h"
#include "taskq.h"
#include "miniutils.h"
#include "system.h"

#include "app.h"

#include "gpio.h"

#include "usb/usb_arcade.h"
#include "usb/usb_hw_config.h"

#include "linker_symaccess.h"

#include "def_config_parser.h"
#include "usb/usb_arc_codes.h"

#include "niffs_impl.h"

#ifdef CONFIG_ANNOYATRON
#include "app_annoyatron.h"
#endif // CONFIG_ANNOYATRON

#define CLI_PROMPT "> "
#define IS_STRING(s) ((u8_t*)(s) >= (u8_t*)in && (u8_t*)(s) < (u8_t*)in + sizeof(in))

typedef int (*func)(int a, ...);

typedef struct cmd_s {
  const char* name;
  const func fn;
  bool dbg;
  const char* help;
} cmd;

struct {
  uart_rx_callback prev_uart_rx_f;
  void *prev_uart_arg;
} cli_state;

static u8_t in[256];

static int _argc;
static void *_args[16];

static int f_sym(void);

static int f_cfg(void);

static int f_cfg_pin_debounce(u8_t cycles);
static int f_cfg_mouse_delta(u8_t ms);
static int f_cfg_acc_pos_speed(u16_t speed);
static int f_cfg_acc_whe_speed(u16_t speed);
static int f_cfg_joy_delta(u8_t ms);
static int f_cfg_joy_acc_speed(u16_t speed);

static int f_usb_enable(int ena);
static int f_usb_keyboard_test(void);

static int f_fs_mount(void);
static int f_fs_dump(void);
static int f_fs_ls(void);
static int f_fs_save(char *name);
static int f_fs_load(char *name);
static int f_fs_rm(char *name);
static int f_fs_format(void);
static int f_fs_chk(void);
static int f_fs_create(char *name);
static int f_fs_rename(char *oldname, char *newname);
static int f_fs_append(char *name, char *line);
static int f_fs_less(char *name);

static int f_uwrite(int uart, char* data);
static int f_uread(int uart, int numchars);
static int f_uconf(int uart, int speed);

static int f_rand(void);

static int f_reset(void);
static int f_time(int d, int h, int m, int s, int ms);
static int f_help(char *s);
static int f_dump(void);
static int f_dump_trace(void);
static int f_assert(void);
static int f_dbg(void);
static int f_build(void);

static int f_memfind(int p);
static int f_memdump(int a, int l);

#ifdef CONFIG_ANNOYATRON

static int f_an_stop(void);
static int f_an_cycle(u32_t t);

static int f_an_wait(char *s, char* s2);
static int f_an_loop(int loops);
static int f_an_endloop(void);

static int f_usb_kb(char *k1, char *k2, char *k3, char *k4);
static int f_usb_mouse(int dx, int dy, int dw, int buttons);

#endif // CONFIG_ANNOYATRON

static int f_def_dummy(void) {return 0;};

static void cli_print_app_name(void);


/////////////////////////////////////////////////////////////////////////////////////////////

static cmd c_tbl[] = {
    { .name = "def", .fn = (func) f_def_dummy, .dbg = FALSE,
        .help = "Define a pins function\n"
            "Syntax: def pin<x> = [(def)* | pin<y> ? (def)* : (def)*]\n"
            "ex: define pin 1 to send keyboard character A\n"
            "    def pin1 = a\n"
            "ex: define pin 2 to move mouse up\n"
            "    def pin2 = mouse_y(-1)\n"
            "ex: define pin 3 to move mouse right if pin 4 is pressed else left\n"
            "    def pin3 = pin4 ? mouse_x(1) : mouse_x(-1)\n\n"
            "ex: define pin 5 to send keyboard sequence CTRL+ALT+DEL\n"
            "    def pin5 = LEFT_CTRL LEFT_ALT DELETE\n"
            "To see all possible definitions, use command sym\n"
    },

    { .name = "sym", .fn = (func) f_sym, .dbg = FALSE,
        .help = "List all possible definitions in def command\n"
    },

    { .name = "cfg", .fn = (func) f_cfg, .dbg = FALSE,
        .help = "Display current configuration\n"
    },
    { .name = "set_pin_debounce", .fn = (func) f_cfg_pin_debounce, .dbg = FALSE,
        .help = "Set number of required debounce cycles required for a pin state change <0-255>\n"
    },
    { .name = "set_mouse_delta", .fn = (func) f_cfg_mouse_delta, .dbg = FALSE,
        .help = "Set number of milliseconds between mouse reports <0-255>\n"
    },
    { .name = "set_mouse_pos_acc", .fn = (func) f_cfg_acc_pos_speed, .dbg = FALSE,
        .help = "Set mouse position accelerator speed (0-65535)\n"
    },
    { .name = "set_mouse_wheel_acc", .fn = (func) f_cfg_acc_whe_speed, .dbg = FALSE,
        .help = "Set mouse wheel accelerator speed (0-65535)\n"
    },
    { .name = "set_joy_delta", .fn = (func) f_cfg_joy_delta, .dbg = FALSE,
        .help = "Set number of milliseconds between joystick reports <0-255>\n"
    },
    { .name = "set_joy_acc", .fn = (func) f_cfg_joy_acc_speed, .dbg = FALSE,
        .help = "Set joystick direction accelerator speed (0-65535)\n"
    },

    { .name = "usb_enable", .fn = (func) f_usb_enable, .dbg = FALSE,
        .help = "Enables or disables usb\n"
    },

    { .name = "usb_test_keyboard", .fn = (func) f_usb_keyboard_test,.dbg = FALSE,
        .help = "Test keys on keyboard\n"
            "Before running test, open some textpad, e.g. gedit.\n"
            "Run the test, and focus the textpad before countdown\n"
            "reaches zero.\nsh "
    },


    { .name = "fs_mount", .fn = (func) f_fs_mount, .dbg = TRUE,
        .help = "Mounts file system\n"
    },
    { .name = "fs_dump", .fn = (func) f_fs_dump, .dbg = TRUE,
        .help = "Dumps file system\n"
    },
    { .name = "ls", .fn = (func) f_fs_ls, .dbg = FALSE,
        .help = "Lists files\n"
    },
    { .name = "save", .fn = (func) f_fs_save, .dbg = FALSE,
        .help = "Saves current config\n"
    },
    { .name = "load", .fn = (func) f_fs_load, .dbg = FALSE,
        .help = "Loads config\n"
    },
    { .name = "rm", .fn = (func) f_fs_rm, .dbg = FALSE,
        .help = "Removes a config\n"
    },
    { .name = "chk", .fn = (func) f_fs_chk, .dbg = FALSE,
        .help = "Checks file system\n"
    },
    { .name = "format", .fn = (func) f_fs_format, .dbg = FALSE,
        .help = "Formats file system\n"
    },
    { .name = "creat", .fn = (func) f_fs_create, .dbg = FALSE,
        .help = "Creates empty file\n"
    },
    { .name = "append", .fn = (func) f_fs_append, .dbg = FALSE,
        .help = "Appends line to file\n"
    },
    { .name = "rename", .fn = (func) f_fs_rename, .dbg = FALSE,
        .help = "Renames a file\n"
    },
    { .name = "less", .fn = (func) f_fs_less, .dbg = FALSE,
        .help = "Show file contents\n"
    },

#ifdef CONFIG_ANNOYATRON

    { .name = "wait", .fn = (func) f_an_wait, .dbg = FALSE,
        .help = "annoyatron: wait [rand <ms>|<ms>]\n"
    },
    { .name = "cycle", .fn = (func) f_an_cycle, .dbg = FALSE,
        .help = "annoyatron: script cycle <ms>\n"
    },

    { .name = "loop", .fn = (func) f_an_loop, .dbg = FALSE,
        .help = "annoyatron: loop <times>\n"
    },

    { .name = "endloop", .fn = (func) f_an_endloop, .dbg = FALSE,
        .help = "annoyatron: endloop\n"
    },
    { .name = "kb", .fn = (func) f_usb_kb, .dbg = FALSE,
        .help = "annoyatron: kb (<key1> (<key2> (<key3> (<key4>))))\n"
    },
    { .name = "mouse", .fn = (func) f_usb_mouse, .dbg = FALSE,
        .help = "annoyatron: mouse (<dx> (<dy> (<dw> (<buttmask>))))\n"
    },
    { .name = "stop", .fn = (func) f_an_stop, .dbg = FALSE,
        .help = "annoyatron: stop annoying\n"
    },

#endif // CONFIG_ANNOYATRON


    { .name = "dump", .fn = (func) f_dump, .dbg = TRUE,
        .help = "Dumps state of all system\n"
    },
    { .name = "dump_trace", .fn = (func) f_dump_trace, .dbg = TRUE,
        .help = "Dumps system trace\n"
    },
    { .name = "time", .fn = (func) f_time, .dbg = TRUE,
        .help = "Prints or sets time\n"
        "time or time <day> <hour> <minute> <second> <millisecond>\n"
    },
    { .name = "uwrite", .fn = (func) f_uwrite, .dbg = TRUE,
        .help = "Writes to uart\n"
        "uwrite <uart> <string>\n"
            "ex: uwrite 2 \"foo\"\n"
    },
    { .name = "uread", .fn = (func) f_uread, .dbg = TRUE,
        .help = "Reads from uart\n"
        "uread <uart> (<numchars>)\n"
            "numchars - number of chars to read, if omitted uart is drained\n"
            "ex: uread 2 10\n"
    },
    { .name = "uconf", .fn = (func) f_uconf, .dbg = TRUE,
        .help = "Configure uart\n"
        "uconf <uart> <speed>\n"
            "ex: uconf 2 9600\n"
    },


    { .name = "dbg", .fn = (func) f_dbg, .dbg = FALSE,
        .help = "Set debug filter and level\n"
        "dbg (level <dbg|info|warn|fatal>) (enable [x]*) (disable [x]*)\n"
        "x - <task|heap|comm|cnc|cli|nvs|spi|all>\n"
        "ex: dbg level info disable all enable cnc comm\n"
    },
    {.name = "memfind", .fn = (func) f_memfind, .dbg = TRUE,
        .help = "Searches for hex in memory\n"
            "memfind 0xnnnnnnnn\n"
    },
    {.name = "memdump", .fn = (func) f_memdump, .dbg = TRUE,
        .help = "Dumps memory\n"
            "memdump <addr> <len>\n"
    },
    { .name = "assert", .fn = (func) f_assert, .dbg = TRUE,
        .help = "Asserts system\n"
            "NOTE system will need to be rebooted\n"
    },
    { .name = "rand", .fn = (func) f_rand, .dbg = TRUE,
        .help = "Generates pseudo random sequence\n"
    },
    { .name = "reset", .fn = (func) f_reset, .dbg = TRUE,
        .help = "Resets system\n"
    },
    { .name = "build", .fn = (func) f_build, .dbg = TRUE,
        .help = "Outputs build info\n"
    },
    { .name = "help", .fn = (func) f_help, .dbg = FALSE,
        .help = "Prints help\n"
        "help or help <command>\n"
    },
    { .name = "?", .fn = (func) f_help, .dbg = FALSE,
        .help = "Prints help\n"
        "help or help <command>\n" },

    // menu end marker
    { .name = NULL, .fn = (func) 0, .help = NULL },
  };

static int usb_kb_type(int mod, int code) {
  usb_kb_report r;
  memset(&r, 0, sizeof(r));
  r.modifiers = mod;
  r.keymap[0] = code;
  USB_ARC_KB_tx(&r);
  r.modifiers = 0;
  r.keymap[0] = 0;
  USB_ARC_KB_tx(&r);
  return 0;
}

static int usb_kb_type_char(char c) {
  enum kb_hid_code code = 0;
  for (code = 0; code <= 0xbc; code++) {
    const keymap *km = USB_ARC_get_keymap(code);
    if (km != NULL && km->keys != NULL) {
      if (strchr(km->keys, c)) {
        if (c >= 'A' && c <= 'Z') {
          usb_kb_type(KB_MOD_LEFT_SHIFT, code);
        } else {
          usb_kb_type(KB_MOD_NONE, code);
        }
        return 1;
      }
    }
  }

  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////

static int f_sym(void) {
  print("KEYBOARD SYMBOLS:\n  ");
  int cx = 0;
  enum kb_hid_code kb;
  for (kb = 0; kb < _KB_HID_CODE_MAX; kb++) {
    const keymap *kmap = USB_ARC_get_keymap(kb);
    if (kmap->name) {
      print("%s%s", kmap->name, kmap->numerator ? "(<num>)" : "");
      cx += strlen(kmap->name) + (kmap->numerator ? 7 : 0);
      int delta = 20 - (cx % 20);
      cx += delta;
      while (delta > 0) {
        print(" ");
        delta--;
      }
      if (cx > 70) {
        print("\n  ");
        cx = 0;
      }
    }
  }
  print("\n\nMOUSE SYMBOLS:\n  ");
  cx = 0;
  enum mouse_code m;
  for (m = 0; m < _MOUSE_CODE_MAX; m++) {
    const keymap *kmap = USB_ARC_get_mousemap(m);
    if (kmap->name) {
      print("%s%s", kmap->name, kmap->numerator ? "(<num>)" : "");
      cx += strlen(kmap->name) + (kmap->numerator ? 7 : 0);
      int delta = 20 - (cx % 20);
      cx += delta;
      while (delta > 0) {
        print(" ");
        delta--;
      }
      if (cx > 70) {
        print("\n  ");
        cx = 0;
      }
    }
  }
  print("\n\nJOYSTICK SYMBOLS:\n  ");
  cx = 0;
  enum joystick_code j;
  for (j = 0; j < _JOYSTICK_CODE_MAX; j++) {
    const keymap *kmap = USB_ARC_get_joystickmap(j);
    if (kmap->name) {
      print("%s%s", kmap->name, kmap->numerator ? "(<num>)" : "");
      cx += strlen(kmap->name) + (kmap->numerator ? 7 : 0);
      int delta = 20 - (cx % 20);
      cx += delta;
      while (delta > 0) {
        print(" ");
        delta--;
      }
      if (cx > 70) {
        print("\n  ");
        cx = 0;
      }
    }
  }

  print("\n\nNUMERATORS:\n<num> is defined as (ACC)[(+)|-][1..127].\n");
  print("Valid number are -127 to 127, excluding 0.\n");
  print("ex. to move mouse 10 steps right on x axis, use MOUSE_X(10).\n");
  print("ex. to move mouse 10 steps left on x axis, use MOUSE_X(-10).\n");
  print("ex. to move mouse accelerating from 1 to 10 steps right on x axis, use MOUSE_X(ACC10).\n");
  print("ex. to move mouse accelerating from 1 to 10 steps left on x axis, use MOUSE_X(ACC-10).\n");
  print("\n");
  return 0;
}

static int f_cfg(void) {
  print("pin debounce cycles:                  %i\n", APP_cfg_get_debounce_cycles());
  print("mouse report delta:                   %i ms\n", APP_cfg_get_mouse_delta_ms());
  print("mouse position accelerator speed:     %i\n", APP_cfg_get_acc_pos_speed());
  print("mouse wheel accelerator speed:        %i\n", APP_cfg_get_acc_wheel_speed());
  print("joystick report delta:                %i ms\n", APP_cfg_get_joystick_delta_ms());
  print("joystick direction accelerator speed: %i\n", APP_cfg_get_joystick_acc_speed());

#ifndef CONFIG_ANNOYATRON
  int pin;
  for (pin = 0; pin < APP_CONFIG_PINS; pin++) {
    def_config *c = APP_cfg_get_pin(pin);
    if (c->pin) def_config_print(c);
  }
#endif

  return 0;
}

static int f_cfg_pin_debounce(u8_t cycles) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_debounce_cycles(cycles);
  return 0;
}
static int f_cfg_mouse_delta(u8_t ms) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_mouse_delta_ms(ms);
  return 0;
}
static int f_cfg_acc_pos_speed(u16_t speed) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_acc_pos_speed(speed);
  return 0;
}
static int f_cfg_acc_whe_speed(u16_t speed) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_acc_wheel_speed(speed);
  return 0;
}

static int f_cfg_joy_delta(u8_t ms) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_joystick_delta_ms(ms);
  return 0;
}
static int f_cfg_joy_acc_speed(u16_t speed) {
  if (_argc != 1) {
    return -1;
  }
  APP_cfg_set_joystick_acc_speed(speed);
  return 0;
}

static int f_usb_enable(int ena) {
  if (_argc != 1) {
    return -1;
  }
  if (ena) {
    print("Enable usb\n");
    USB_Cable_Config(ENABLE);
    USB_ARC_init();
  } else {
    print("Disable usb\n");
    USB_Cable_Config(DISABLE);
  }
  return 0;
}

static int f_usb_keyboard_test(void) {
  print("This will generate a lot of keypresses.\n");
  print("Please focus some window safe for garbled input.\n");
  print("Pressing keys in...\n");
  int i;
  for (i = 7; i > 0; i--) {
    print("  %i", i);
    SYS_hardsleep_ms(1000);
  }
  print("\n");
  enum kb_hid_code code = 0;
  for (code = 0; code <= 0xbc; code++) {
    const keymap *km = USB_ARC_get_keymap(code);
    if (km != NULL && km->keys != NULL) {

      char hex_num[3];
      sprint(hex_num, "%02x", code);
      usb_kb_type_char(hex_num[0]);
      usb_kb_type_char(hex_num[1]);
      usb_kb_type_char(' ');
      for (i = 0; i < strlen(km->name); i++) {
        usb_kb_type_char(km->name[i]);
      }
      usb_kb_type_char(' ');
      usb_kb_type(KB_MOD_NONE, code);
      usb_kb_type_char(' ');
      usb_kb_type(KB_MOD_NONE, KC_ENTER);
    }

  }
  usb_kb_type_char('O');
  usb_kb_type_char('K');
  usb_kb_type(KB_MOD_NONE, KC_ENTER);
  return 0;
}

static int f_fs_mount(void) {
  FS_mount();
  return 0;
}

static int f_fs_dump(void) {
  FS_dump();
  return 0;
}

static int f_fs_ls(void) {
  FS_ls();
  return 0;
}

static int f_fs_save(char *name) {
  if (_argc != 1) return -1;
  int res = FS_save_config(name);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_load(char *name) {
  if (_argc != 1) return -1;
  int res = FS_load_config(name);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_rm(char *name) {
  if (_argc != 1) return -1;
  int res = FS_rm_config(name);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_format(void) {
  int res = FS_format();
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_chk(void) {
  int res = FS_chk();
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_create(char *name) {
  int res = FS_create(name);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_rename(char *oldname, char *newname){
  int res = FS_rename(oldname, newname);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_append(char *name, char *line) {
  int res = FS_append(name, line);
  if (res != 0) print("err: %i\n", res);
  return 0;
}

static int f_fs_less(char *name) {
  int res = FS_less(name);
  if (res != 0) print("err: %i\n", res);
  return 0;
}



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
  print("\ndate:%i build:%i\n\n", SYS_build_date(), SYS_build_number());

  print("SYS_MAIN_TIMER_FREQ %i\n", SYS_MAIN_TIMER_FREQ);
  print("SYS_TIMER_TICK_FREQ %i\n", SYS_TIMER_TICK_FREQ);
  print("UART2_SPEED %i\n", UART2_SPEED);
  print("CONFIG_TASK_POOL %i\n", CONFIG_TASK_POOL);
  print("APP_CONFIG_PINS %i\n", APP_CONFIG_PINS);
  print("APP_CONFIG_DEFS_PER_PIN %i\n", APP_CONFIG_DEFS_PER_PIN);
  print("USB_KB_REPORT_KEYMAP_SIZE %i\n", USB_KB_REPORT_KEYMAP_SIZE);

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
      UART_CFG_DATABITS_8, UART_CFG_STOPBITS_1, UART_CFG_PARITY_NONE, UART_CFG_FLOWCONTROL_NONE, TRUE);

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
      if ((c_tbl[i].dbg && __dbg_level < 1) || (!c_tbl[i].dbg)) {
        int len = strpbrk(c_tbl[i].help, "\n") - c_tbl[i].help;
        char tmp[64];
        strncpy(tmp, c_tbl[i].help, len + 1);
        tmp[len + 1] = 0;
        char fill[24];
        int fill_len = sizeof(fill) - strlen(c_tbl[i].name);
        memset(fill, ' ', sizeof(fill));
        fill[fill_len] = 0;
        print("  %s%s%s", c_tbl[i].name, fill, tmp);
      }
      i++;
    }
  }
  return 0;
}

static int f_dump() {
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);

  print("\nCLOCKS\n------\n");
  print("HCLK:        %i\n", clocks.HCLK_Frequency);
  print("PCLK1:       %i\n", clocks.PCLK1_Frequency);
  print("PCKL2:       %i\n", clocks.PCLK2_Frequency);
  print("SYSCLK:      %i\n", clocks.SYSCLK_Frequency);
  print("ADCCLK:      %i\n", clocks.ADCCLK_Frequency);

  print("\nMEMORY\n------\n");
  print("ram begin:   %08x\n", RAM_BEGIN);
  print("ram end:     %08x\n", RAM_END);
  print("flash begin: %08x\n", FLASH_BEGIN);
  print("flash end:   %08x\n", FLASH_END);
  print("shmem begin: %08x\n", SHARED_MEMORY_ADDRESS);
  print("shmem end:   %08x\n", SHARED_MEMORY_END);
  print("stack begin: %08x\n", STACK_START);
  print("stack end:   %08x\n", STACK_END);
  print("\n");

  TASK_dump(IOSTD);
  TASK_dump_pool(IOSTD);
  return 0;
}

static int f_dump_trace() {
#ifdef DBG_TRACE_MON
  SYS_dump_trace(get_print_output());
#else
  print("trace not enabled\n");
#endif
  return 0;
}

static int f_memfind(int hex) {
  if (_argc < 1) return -1;
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

static int f_memdump(int addr, int len) {
  if (_argc < 2) return -1;
  print("dumping 0x%08x, %i bytes\n", addr, len);
  printbuf(IOSTD, (u8_t*)addr, len);
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_ANNOYATRON

static int f_an_stop(void) {
  annoy_stop();
  return 0;
}

static int f_an_wait(char *s, char *s2) {
  u32_t time;
  if (s >= 0x20000000 && strcmp("rand", s) == 0) {
    if (_argc < 2) {
      time = rand_next() % 1000;
    } else {
      time = rand_next() % (u32_t)s2;
    }
  } else {
    time = (u32_t)s;
  }
  if (time > 20000) time = rand_next() % 20000;
  if (time == 0) time = 1;
  print("wait %i ms\n", time);
  annoy_wait(time);
  return 0;
}

static int f_an_cycle(u32_t cycle) {
  annoy_set_cycle(cycle);
  return 0;
}

static int f_an_loop(int loops) {
  annoy_loop_enter(loops);
  return 0;
}
static int f_an_endloop(void) {
  annoy_loop_exit();
  return 0;
}

static enum kb_hid_code get_kb_code(char *s) {
  enum kb_hid_code kb_code;
  for (kb_code = 0; kb_code < _KB_HID_CODE_MAX; kb_code++) {
    const keymap *kb_map = USB_ARC_get_keymap(kb_code);
    if (kb_map->name == NULL) continue;
    if (strcmp(kb_map->name, s) == 0) {
      return kb_code;
      break;
    }
  }
  return _KB_HID_CODE_MAX;
}

static void fill_kb_report(char *sym, usb_kb_report *r) {
  enum kb_hid_code kb_code = get_kb_code(sym);
  if (kb_code >= _KB_HID_CODE_MAX) {
    print("keyboard symbol %s not found\n", sym);
    return;
  }

  if (kb_code >= MOD_LCTRL) {
    r->modifiers |= MOD_BIT(kb_code);
  } else {
    int i = 0;
    while (r->keymap[i] != 0) i++;
    r->keymap[i] = kb_code;
  }
}

static int f_usb_kb(char *k1, char *k2, char *k3, char *k4) {
  usb_kb_report r;
  memset(&r, 0, sizeof(r));
  if (_argc >= 1) fill_kb_report(k1, &r);
  if (_argc >= 2) fill_kb_report(k2, &r);
  if (_argc >= 3) fill_kb_report(k3, &r);
  if (_argc >= 4) fill_kb_report(k4, &r);
  USB_ARC_KB_tx(&r);
  return 0;
}

static int f_usb_mouse(int dx, int dy, int dw, int buttons) {
  usb_mouse_report r;
  memset(&r, 0, sizeof(r));
  if (_argc >= 1) r.dx = dx & 0xff;
  if (_argc >= 2) r.dy = dy & 0xff;
  if (_argc >= 3) r.wheel = dw & 0xff;
  if (_argc >= 4) r.modifiers = buttons & 0xff;
  USB_ARC_MOUSE_tx(&r);
  return 0;
}

#endif // CONFIG_ANNOYATRON


/////////////////////////////////////////////////////////////////////////////////////////////

void CLI_parse(u32_t len, u8_t *buf) {
#ifndef CONFIG_ANNOYATRON
  if (strcmpbegin("def", (char*)buf)==0) {
    if (buf[len-1] == '\r' || buf[len-1] == '\n') {
      len--;
    }
    memset(&buf[len], 0, sizeof(in)-len);
    def_config pindef;
    bool ok = def_config_parse(&pindef, (char*)&buf[4], len-4);
    if (ok) {
      def_config_print(&pindef);
      print("OK\n");
      APP_cfg_set_pin(&pindef);
    }
    print(CLI_PROMPT);
    return;
  }
#endif // CONFIG_ANNOYATRON

  cursor cursor;
  strarg_init(&cursor, (char*) buf, len);
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

void CLI_TASK_on_uart_input(u32_t len, void *p) {
  set_print_output(IOSTD);
  if (len > sizeof(in)) {
    DBG(D_CLI, D_WARN, "CONS input overflow\n");
    print(CLI_PROMPT);
    return;
  }
  u32_t rlen = IO_get_buf(IOSTD, in, MIN(len, sizeof(in)));
  if (rlen != len) {
    DBG(D_CLI, D_WARN, "CONS length mismatch\n");
    print(CLI_PROMPT);
    return;
  }
  CLI_parse(rlen, in);
}

#ifdef CONFIG_ARCHID_VCD
void CLI_TASK_on_usb_input(u32_t len, void *p) {
  set_print_output(IOUSB);
  if (len > sizeof(in)) {
    DBG(D_CLI, D_WARN, "CONS input overflow\n");
    print(CLI_PROMPT);
    return;
  }
  CLI_parse(len, in);
  set_print_output(IOSTD);
}
#endif

void CLI_timer() {
}

void CLI_uart_check_char(void *a, u8_t c) {
  if (c == '\n' || c == '\r') {
    task *t = TASK_create(CLI_TASK_on_uart_input, 0);
    TASK_run(t, IO_rx_available(IOSTD), NULL);
  }
}

#ifdef CONFIG_ARCHID_VCD
static void usb_rx_cb(u16_t avail, void *arg) {
  int in_ix = 0;
  while ((avail = IO_rx_available(IOUSB)) > 0) {
    int len = MIN(avail, sizeof(in) - in_ix);
    IO_get_buf(IOUSB, &in[in_ix], len);
    in_ix += len;
    if (in_ix >= sizeof(in)) break;
  }
  int i;
  for (i = 0; i < in_ix; i++) {
    if (in[i] == '\r' || in[i] == '\n') {
      task *t = TASK_create(CLI_TASK_on_usb_input, 0);
      TASK_run(t, i, NULL);
    }
  }
}
#endif

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
#ifdef CONFIG_ARCHID_VCD
  USB_SER_set_rx_callback(usb_rx_cb, NULL);
#endif

  print ("\n");
  cli_print_app_name();
  print("\n\n");
  print("build     : %i\n", SYS_build_number());
  print("build date: %i\n", SYS_build_date());
  print("\ntype '?' or 'help' for list of commands\n\n");
  print(CLI_PROMPT);
}

static void cli_print_app_name(void) {
  print (APP_NAME);
}
