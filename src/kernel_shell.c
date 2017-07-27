


#define COMMAND_UNKNOWN_COMMAND 1
#define COMMAND_INVALID_ARG_COUNT 2

#define KShell_Command_Metalist \
_(help, kshell_help) \
_(hardware_info, kshell_hardware_info)\
_(time, kshell_unimplemented_command)\
_(ioapic_irq_map, kshell_ioapic_irq_map)\
_(run, kshell_run)\
_(test, kshell_test)

typedef enum {
  KShell_Command_Invalid,
  #define _(name, proc) KShell_Command_##name,
  KShell_Command_Metalist
  #undef _
  KShell_Command_Count
} KShell_Command;

static const char *KShell_Command_Name[] = {
#define _(name, proc) #name,
KShell_Command_Metalist
#undef _
};

static void kshell_help(const char *text, size_t length);
static void kshell_hardware_info(const char *text, size_t length);
static void kshell_unimplemented_command(const char *text, size_t length);
static void kshell_ioapic_irq_map(const char *text, size_t length);
static void kshell_run(const char *text, size_t length);
static void kshell_test(const char *text, size_t length);

typedef void(*KShell_Command_Proc)(const char *, size_t);
static const KShell_Command_Proc KShell_Command_Handler[] = {
#define _(name, proc) proc,
KShell_Command_Metalist
#undef _
};

#define string_and_length(string) string, (sizeof(string) - 1)

static void
kshell_help(const char *input, size_t length){
  klog_info("kshell command list:");
  #define _(name, proc) \
  klog_info("  %s", #name);
  KShell_Command_Metalist
  #undef _
}

static void
kshell_run(const char *text, size_t length){
  ktask_run_program("/programs/test_program", strlen("/programs/test_program"));
}

static void
kshell_test(const char *text, size_t length){
  File_Handle f = {};
  if(fs_obtain_file_handle(string_and_length("/user/short_story.txt"), &f) == 0){
    klog_error("failed to open test file");
    return;
  } 

#if 0
  uint8_t buffer[4096] = {};
  if(fs_read_file(&f, 0, f.file_size, (uintptr_t)buffer) == 0){
    klog_error("failed to read test file");
    return;
  }
#endif


  //klog_debug("%s", buffer);
}

static void
kshell_ioapic_irq_map(const char *text, size_t length){
  kdebug_ioapic_log_irq_map(globals.system_info.ioapic_virtual_address);
}

static void
kshell_hardware_info(const char *text, size_t length) {

}

static void
kshell_unimplemented_command(const char *text, size_t length){
  klog_error("%s is an unimplemented command!", text);
}

void kshell_process_command(const char *input, size_t length){
  #define _(name, proc) \
  if(string_matches_string(#name, sizeof(#name)-1, input)){\
    proc(input, length);\
    return;\
  }
  KShell_Command_Metalist
  #undef _

  klog_error("%s is not a shell command", input);
}

void process_shell_keyboard_input(Keyboard_State *keyboard, Circular_Log *log) {
  for (size_t i = 0; i < sizeof(keyboard->is_key_pressed); i++) {
    if (keyboard->is_key_pressed[i]) {
      uint8_t scancode = i;

      if (scancode == KEYBOARD_SCANCODE1_BACKSPACE_PRESSED) {
        klog_remove_last_input_character(log);
      } else if (scancode == KEYBOARD_SCANCODE1_ENTER_PRESSED) {
        klog_submit_input_to_shell(log);
      } else if (scancode == KEYBOARD_SCANCODE1_UP_PRESSED) {
        if(log->scroll_offset < log->current_entry_count - 1){
          log->scroll_offset += 1;
        }


        log->is_dirty = true;
      } else if (scancode == KEYBOARD_SCANCODE1_DOWN_PRESSED){
        if(log->scroll_offset > 0){
          log->scroll_offset -= 1;
          log->is_dirty = true;
        }
      }

      if (scancode < (int)sizeof(SCANCODE_TO_LOWERCASE_ACII)) {
        char ascii_character = 0;
        if(keyboard->is_key_down[KEYBOARD_SCANCODE1_LSHIFT] ||
          keyboard->is_key_down[KEYBOARD_SCANCODE1_RSHIFT]){
          ascii_character = SCANCODE_TO_UPERCASE_ACII[scancode];
        } else { ascii_character = SCANCODE_TO_LOWERCASE_ACII[scancode]; }
        if(ascii_character == 0) return;
        klog_add_input_character(log, ascii_character);
      }
    }
  }
}
