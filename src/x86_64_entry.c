// NOTE(Torin)
// X86_64 Kernel Entry Point
// This x86_64_kernel_entry is called after
// the computer has been booted with a multiboot2 compliant
// bootloader such as GRUB2 and the bootstrap assembly has put the
// CPU into longmode with a longmode GDT; however, an longmode
// IDT still must be initalized

typedef struct {
	uint8_t *framebuffer;
	uint16_t resolutionX;
	uint16_t resolutionY;
	uint16_t depth;
} VideoState;

typedef struct {
	uint16_t offset_0_15;
	uint16_t code_segment_selector;
	uint8_t ist;
	uint8_t type_and_attributes;
	uint16_t offset_16_31;
	uint32_t offset_32_63;
	uint32_t null_uint32;
} x86_64_IDT_Entry;

typedef struct {
  Circular_Log log;
  VGA_Text_Terminal vga_text_term;
  bool is_logging_disabled;
  uintptr_t lapic_address;
  Framebuffer framebuffer;
} Kernel_Globals;

static Kernel_Globals globals;

global_variable x86_64_IDT_Entry _idt[256];
global_variable uintptr_t _interrupt_handlers[256];

#include "kernel_graphics.c"
#include "kernel_acpi.c"
#include "kernel_apic.c"


struct IOAPIC_IRQR_LOW_STRUCT {
  union {
    struct {
      uint8_t vector; // 0-7
      uint8_t delivery_mode : 2; //8-10
      uint8_t destination_mode : 1; //11
      uint8_t delivery_status : 1; // 12
      uint8_t pin_polarity : 1; // 13
      uint8_t remote_irr : 1; //14
      uint8_t trigger_mode : 1; //15
      uint8_t mask : 1; //16
      uint16_t reserved : 15; 
    };
    uint32_t packed;
  };
} __attribute__((packed));

struct IOAPIC_IRQR_HIGH_STRUCT {
  union {
    struct {
      uint32_t reserved    : 24;
      uint32_t destination :  8;
    };
    uint32_t packed;
  };
} __attribute__((packed));

typedef struct IOAPIC_IRQR_LOW_STRUCT IOAPIC_IRQR_LOW;
typedef struct IOAPIC_IRQR_HIGH_STRUCT IOAPIC_IRQR_HIGH;

static void
ioapic_initalize(uintptr_t ioapic_register_base) {
   static const uint8_t DELIVERY_MODE_FIXED = 0x00;

  { //Keyboard
    IOAPIC_IRQR_LOW low = {};
    IOAPIC_IRQR_HIGH high = {};
    low.vector = 0x21;
    ioapic_write_register(ioapic_register_base, 0x12, low.packed);
    ioapic_write_register(ioapic_register_base, 0x13, high.packed);
  }


#if 1
  #endif
}

static void
ioapic_print_irq_map(uintptr_t ioapic_base){
  for(size_t i = 0; i < 8; i++){
    IOAPIC_IRQR_LOW irqr_low = {};
    IOAPIC_IRQR_HIGH irqr_high = {};
    irqr_low.packed = ioapic_read_register(ioapic_base, 0x10 + (i * 2));
    irqr_high.packed = ioapic_read_register(ioapic_base, 0x10 + (i * 2) + 1);
    klog_debug("IRQ %u", (uint32_t)i);
    klog_debug("vector: %u", (uint32_t)irqr_low.vector);
    klog_debug("mask %u", (uint32_t)irqr_low.mask);
  }
}

static void
lapic_initalize(uintptr_t apic_register_base) {
	asm volatile ("cli");
	
  { //Disable the legacy PIC first
		static const uint8_t PIC1_DATA_PORT = 0x21;
		static const uint8_t PIC2_DATA_PORT = 0xA1;
		write_port(PIC1_DATA_PORT, 0x20);
		write_port(PIC2_DATA_PORT, 0x20);
		write_port(PIC1_DATA_PORT, 0b11111111);
		write_port(PIC2_DATA_PORT, 0b11111111);
	}

	{ 
    //APIC Spuritous interput vector 
		static const uint64_t APIC_SIVR_OFFSET = 0xF0;
    static const uint32_t SIVR_ENABLE = 1 << 8;
    static const uint32_t SIVR_FOCUS_CHECKING = 1 << 9;
		uint32_t *sivr = (uint32_t *)(apic_register_base + APIC_SIVR_OFFSET);
		*sivr = (31 | SIVR_ENABLE); 
	}

  klog_debug("lapic initalized for cpu0 is initalized");
	asm volatile("sti");
  klog_debug("now receiving lapic interrupts!");
}

#define bochs_magic_breakpoint asm volatile("xchgw %bx, %bx");

static const uint8_t TRAMPOLINE_BINARY[] = {
#include "trampoline.txt"
};

static const uint8_t TEST_PROGRAM_ELF[] = { 
#include "test_program.txt" 
};

static void
lapic_enable_timer(uintptr_t lapic_base){
  static const uint32_t TIMER_IRQ_REGISTER = 0x320;
  
  static const uint32_t TIMER_INITAL_COUNT_REGISTER  = 0x380;
  static const uint32_t TIMER_CURRENT_COUNT_REGISTER = 0x390;
  static const uint32_t TIMER_DIVIDE_CONFIG_REGISTER = 0x3E0;

  static const uint32_t TIMER_DIVIDE_BY_2 = 0b00;
  static const uint32_t TIMER_DIVIDE_BY_4 = 0b01;
  static const uint32_t TIMER_DIVIDE_BY_8 = 0b10;
  static const uint32_t TIMER_DIVIDE_BY_16 = 0b11;

  //NOTE(Torin) Without this flag the timer is in one-shot mode 
  static const uint32_t TIMER_PERIODIC_MODE = 0x20000;

  lapic_write_register(lapic_base, TIMER_CURRENT_COUNT_REGISTER, 0x00);
  lapic_write_register(lapic_base, TIMER_INITAL_COUNT_REGISTER, 0xFFFFFF);
  lapic_write_register(lapic_base, TIMER_DIVIDE_CONFIG_REGISTER, TIMER_DIVIDE_BY_16);
  lapic_write_register(lapic_base, TIMER_IRQ_REGISTER, 0x20 | TIMER_PERIODIC_MODE);
}

static void
initalize_cpu(uintptr_t apic_register_base, uint8_t target_apic_id, uint8_t target_page_number){
  static const uintptr_t APIC_ICR1_OFFSET = 0x300;
  static const uintptr_t APIC_ICR2_OFFSET = 0x310;

  static const uint8_t DELIVERY_MODE_FIXED = 0b000;
  static const uint8_t DELIVERY_MODE_LOWEST_PRIORITY = 0b001;
  static const uint8_t DELIVERY_MODE_SMI = 0b010;
  static const uint8_t DELIVERY_MODE_NMI = 0b100;
  static const uint8_t DELIVERY_MODE_INIT = 0b101;
  static const uint8_t DELIVERY_MODE_SIPI = 0b110;

  static const uint8_t DESTINATION_MODE_PHYSICAL = 0;
  static const uint8_t DESTINATION_MODE_LOGICAL = 1;

  static const uint8_t DEILVERY_STATUS_IDLE = 0;
  static const uint8_t DELIVERY_STATUS_PENDING = 1;

  struct ICR1_STRUCT {
    uint8_t vector;                     // 0 - 7
    uint8_t delivery_mode : 3;          // 8 - 10 
    uint8_t destination_mode : 1;       // 11 
    uint8_t delivery_status : 1;        // 12 
    uint8_t reserved0 : 1;              // 13
    uint8_t level : 1;                  // 14
    uint8_t trigger_mode : 1;           //15
    uint8_t reserved1 : 2;              //16 - 17
    uint8_t destination_shorthand : 2;  // 18 -19
    uint16_t reserved2 : 11;            // 20 - 31
  } __attribute((packed));

  struct ICR2_STRUCT {
    uint32_t reserved_0_ : 24;
    uint8_t destination_field;
  } __attribute((packed));

  typedef struct ICR1_STRUCT ICR1_Register;
  typedef struct ICR2_STRUCT ICR2_Register;

  ICR1_Register icr1 = {};
  icr1.delivery_mode = DELIVERY_MODE_INIT;
  ICR2_Register icr2 = {};
  icr2.destination_field = target_apic_id;

  //NOTE(Torin) Writing to the ICR1 causues a IPI to be generated automaticly so ICR2 is written first
  //Send INIT IPI To set processor to wait for SIPI

  uint32_t icr2_value = *(uint32_t *)&icr2;
  uint32_t icr1_value = *(uint32_t *)&icr1;
  klog_debug("icr2_value: %u", icr2_value);
  klog_debug("icr1_value: %u", icr1_value);

  lapic_write_register(apic_register_base, APIC_ICR2_OFFSET, *(uint32_t *)&icr2);
  lapic_write_register(apic_register_base, APIC_ICR1_OFFSET, *(uint32_t *)&icr1);
  while(icr1.delivery_status == 1){
    klog_debug("waiting for icr1 to get delivered!");
  }

  for(size_t i = 0; i < 0xFFFFF; i++) { asm volatile ( "nop" ); }

  //Setup and send SIPI
  icr1.vector = target_page_number; 
  icr1.delivery_mode = DELIVERY_MODE_SIPI;
  lapic_write_register(apic_register_base, APIC_ICR2_OFFSET, *(uint32_t *)&icr2);
  lapic_write_register(apic_register_base, APIC_ICR1_OFFSET, *(uint32_t *)&icr1);
  for(size_t i = 0; i < 0xFFFFF; i++) { asm volatile ( "nop" ); }
  lapic_write_register(apic_register_base, APIC_ICR2_OFFSET, *(uint32_t *)&icr2);
  lapic_write_register(apic_register_base, APIC_ICR1_OFFSET, *(uint32_t *)&icr1);
}

static void
legacy_pic8259_initalize(void) {
	static const uint8_t PIC1_COMMAND_PORT = 0x20;
	static const uint8_t PIC2_COMMAND_PORT = 0xA0;
	static const uint8_t PIC1_DATA_PORT = 0x21;
	static const uint8_t PIC2_DATA_PORT = 0xA1;

	//Initalization Command Words (ICW)
	static const uint8_t ICW1_INIT_CASCADED = 0x11;
	static const uint8_t ICW2_PIC1_IRQ_NUMBER_BEGIN = 0x20;
	static const uint8_t ICW2_PIC2_IRQ_NUMBER_BEGIN = 0x28;
	static const uint8_t ICW3_PIC1_IRQ_LINE_2 = 0x4;
	static const uint8_t ICW3_PIC2_IRQ_LINE_2 = 0x2;
	static const uint8_t ICW4_8068 = 0x01;

	//ICW1 Tells PIC to wait for 3 more words
	write_port(PIC1_COMMAND_PORT, ICW1_INIT_CASCADED);
	write_port(PIC2_COMMAND_PORT, ICW1_INIT_CASCADED);
	//ICW2 Set PIC Offset Values
	write_port(PIC1_DATA_PORT, ICW2_PIC1_IRQ_NUMBER_BEGIN);
	write_port(PIC2_DATA_PORT, ICW2_PIC2_IRQ_NUMBER_BEGIN);
	//ICW3 PIC Cascading Info
	write_port(PIC1_DATA_PORT, ICW3_PIC1_IRQ_LINE_2);
	write_port(PIC2_DATA_PORT, ICW3_PIC2_IRQ_LINE_2);
	//ICW4 Additional Enviroment Info
	//NOTE(Torin) Currently set to 80x86
	write_port(PIC1_DATA_PORT, ICW4_8068);
	write_port(PIC2_DATA_PORT, ICW4_8068);

  //Write EndOfInterupt and set interrupt enabled mask 
	write_port(PIC1_DATA_PORT, 0x20);
	write_port(PIC2_DATA_PORT, 0x20);
	write_port(PIC1_DATA_PORT, 0b11111101);
	write_port(PIC2_DATA_PORT, 0b11111111);
	klog_info("PIC8259 Initialized");
}

#if 0
static void
legacy_pit_initialize(void){ 
	//TODO(Torin) This frequency thing is probably bogus and needs to be
	//fixed so that specific times can be programmed
	static const uint32_t PIT_COMMAND_REPEATING_MODE = 0x36;
	static const uint32_t PIT_DIVISOR = 1193180 / 100;

	static const uint8_t PIT_DATA_PORT0 = 0x40;
	static const uint8_t PIT_DATA_PORT1 = 0x41;
	static const uint8_t PIT_DATA_PORT2 = 0x42;
	static const uint8_t PIT_COMMAND_PORT = 0x43;

	const uint8_t divisor_low = (uint8_t)(PIT_DIVISOR & 0xFF);
	const uint8_t divisor_high = (uint8_t)((PIT_DIVISOR >> 8) & 0xFF);

	write_port(PIT_COMMAND_PORT, PIT_COMMAND_REPEATING_MODE);
	write_port(PIT_DATA_PORT0, divisor_low);
	write_port(PIT_DATA_PORT0, divisor_high);
	klog_info("PIT initialized!");
}
#endif

//TODO(Torin) Remove IDT Global variable
static void
idt_install_interrupt(const uint32_t irq_number, const uint64_t irq_handler_addr) {
	static const uint64_t PRIVILEGE_LEVEL_0 = 0b00000000;
	static const uint64_t PRIVILEGE_LEVEL_3 = 0b01100000;
	static const uint64_t PRESENT_BIT = (1 << 7); 
	
	static const uint64_t TYPE_TASK_GATE_64 = 0x5;
	static const uint64_t TYPE_INTERRUPT_GATE_64 = 0xE;
	static const uint64_t TYPE_TRAP_GATE_64 = 0xF;

	static const uint8_t GDT_CODE_SEGMENT_OFFSET = 0x08;
	
	_idt[irq_number].offset_0_15 = (uint16_t)(irq_handler_addr & 0xFFFF);
	_idt[irq_number].offset_16_31 = (uint16_t)((irq_handler_addr >> 16) & 0xFFFF);
	_idt[irq_number].offset_32_63 = (uint32_t)((irq_handler_addr >> 32) & 0xFFFFFFFF);
	_idt[irq_number].type_and_attributes = PRESENT_BIT | TYPE_INTERRUPT_GATE_64 | PRIVILEGE_LEVEL_0; 
	_idt[irq_number].code_segment_selector = GDT_CODE_SEGMENT_OFFSET;
	_idt[irq_number].ist = 0;
}

static void
x86_64_idt_initalize(){
	//NOTE(Torin) This is a debug mechanisim to insure that 
	//if a handler is called from the asm stub and the registered interrupt
	//handler is 0xFFFFFFFF then the interrupt handler was never registered
  
  extern void asm_double_fault_handler();
  extern void asm_debug_handler();
  
	for (uint32_t i = 0; i < 256; i++) {
		idt_install_interrupt(i, (uintptr_t)asm_debug_handler);
		_interrupt_handlers[i] = 0xFFFFFFFF;
	}
	

	{ //Software Exceptions
		extern void asm_isr0(void);
		extern void asm_isr1(void);
		extern void asm_isr2(void);
		extern void asm_isr3(void);
		extern void asm_isr4(void);
		extern void asm_isr5(void);
		extern void asm_isr6(void);
		extern void asm_isr7(void);
		extern void asm_isr9(void);
		extern void asm_isr10(void);
		extern void asm_isr11(void);
		extern void asm_isr12(void);
		extern void asm_isr13(void);
		extern void asm_isr14(void);
		extern void asm_isr15(void);
		extern void asm_isr16(void);
		extern void asm_isr17(void);
		extern void asm_isr18(void);
		extern void asm_isr19(void);
		extern void asm_isr20(void);
		extern void asm_isr21(void);
		extern void asm_isr22(void);
		extern void asm_isr23(void);
		extern void asm_isr24(void);
		extern void asm_isr25(void);
		extern void asm_isr26(void);
		extern void asm_isr27(void);
		extern void asm_isr28(void);
		extern void asm_isr29(void);
		extern void asm_isr30(void);
		extern void asm_isr31(void);

		idt_install_interrupt(0, (uintptr_t)asm_isr0);
		idt_install_interrupt(1, (uintptr_t)asm_isr1);
		idt_install_interrupt(2, (uintptr_t)asm_isr2);
		idt_install_interrupt(3, (uintptr_t)asm_isr3);
		idt_install_interrupt(4, (uintptr_t)asm_isr4);
		idt_install_interrupt(5, (uintptr_t)asm_isr5);
		idt_install_interrupt(6, (uintptr_t)asm_isr6);
		idt_install_interrupt(7, (uintptr_t)asm_isr7);
		idt_install_interrupt(8, (uintptr_t)asm_double_fault_handler);
		idt_install_interrupt(9, (uintptr_t)asm_isr9);
		idt_install_interrupt(10, (uintptr_t)asm_isr10);
		idt_install_interrupt(11, (uintptr_t)asm_isr11);
		idt_install_interrupt(12, (uintptr_t)asm_isr12);
		idt_install_interrupt(13, (uintptr_t)asm_isr13);
		idt_install_interrupt(14, (uintptr_t)asm_isr14);
		idt_install_interrupt(15, (uintptr_t)asm_isr15);
		idt_install_interrupt(16, (uintptr_t)asm_isr16);
		idt_install_interrupt(17, (uintptr_t)asm_isr17);
		idt_install_interrupt(18, (uintptr_t)asm_isr18);
		idt_install_interrupt(19, (uintptr_t)asm_isr19);
		idt_install_interrupt(20, (uintptr_t)asm_isr20);
		idt_install_interrupt(21, (uintptr_t)asm_isr21);
		idt_install_interrupt(22, (uintptr_t)asm_isr22);
		idt_install_interrupt(23, (uintptr_t)asm_isr23);
		idt_install_interrupt(24, (uintptr_t)asm_isr24);
		idt_install_interrupt(25, (uintptr_t)asm_isr25);
		idt_install_interrupt(26, (uintptr_t)asm_isr26);
		idt_install_interrupt(27, (uintptr_t)asm_isr27);
		idt_install_interrupt(28, (uintptr_t)asm_isr28);
		idt_install_interrupt(29, (uintptr_t)asm_isr29);
		idt_install_interrupt(30, (uintptr_t)asm_isr30);
		idt_install_interrupt(31, (uintptr_t)asm_isr31);
	}

#if 1
	{ //Hardware Interrupts
		static const uint32_t IRQ_PIT = 0x20; 
		static const uint32_t IRQ_KEYBOARD = 0x21;
		extern void asm_irq0(void);
		extern void asm_irq1(void);

		_interrupt_handlers[0] = (uintptr_t)irq_handler_pit;
		_interrupt_handlers[1] = (uintptr_t)irq_handler_keyboard;
		idt_install_interrupt(IRQ_PIT, (uintptr_t)asm_irq0);
		idt_install_interrupt(IRQ_KEYBOARD, (uintptr_t)asm_irq1);
	}
#endif

	struct {
			uint16_t limit;
			uintptr_t address;
	} __attribute__((packed)) idtr = { sizeof(_idt) - 1, (uintptr_t)_idt };
	asm volatile ("lidt %0" : : "m"(idtr));
	asm volatile ("sti");
  klog_info("IDT initialized");
}

#include "multiboot2.h"
#include "hardware_serial.cpp"
#include "kernel_memory.c"

extern void
ap_entry_procedure(void){
  klog_info("CPU1 booted and initalized");

  klog_debug("Grettings CPU0!  CPU1 here. I'm having a fantastic day!  How are you?");
  asm volatile("hlt");
  klog_debug("wtf mate? who dares awaken me from my slumber?");
}

#include "kernel_process.c"



extern void 
kernel_longmode_entry(uint64_t multiboot2_magic, uint64_t multiboot2_address) 
{
  //klog_disable();
	serial_debug_init();
	legacy_pic8259_initalize();
	//legacy_pit_initialize();
	x86_64_idt_initalize();
  kmem_initalize();

  klog_debug("ap_entry_procedure: %lu", ap_entry_procedure);

  System_Info sys = {};

	if (multiboot2_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		klog_error("the kernel was not booted with a multiboot2 compliant bootloader!");
		kpanic();
	}

	if (multiboot2_address & 7) {
		klog_error("unaligned multiboot_info!");
		kpanic();
	}

	uint32_t multiboot_info_size = *(uint32_t *)multiboot2_address;
	klog_debug("multiboot_info_size is %u", multiboot_info_size);
	
	struct multiboot_tag *tag = (struct multiboot_tag *)(multiboot2_address + 8);
	while (tag->type != MULTIBOOT_TAG_TYPE_END) {
		klog_debug("found mbtag: %u", tag->type);
		switch (tag->type) {

			case MULTIBOOT_TAG_TYPE_ACPI_OLD: {
				struct multiboot_tag_old_acpi *acpi_info = (struct multiboot_tag_old_acpi *)(tag);
				klog_debug("found acpi old data.  rsp address is %lu", acpi_info->rsdp);
				parse_root_system_descriptor((RSDP_Descriptor_1*)acpi_info->rsdp, &sys);
			} break;

			case MULTIBOOT_TAG_TYPE_ACPI_NEW: {
			  klog_debug("found acpi new data");
				struct multiboot_tag_new_acpi *acpi_info = (struct multiboot_tag_new_acpi *)(tag);
				RSDP_Descriptor_2 *rsdp = (RSDP_Descriptor_2 *)acpi_info->rsdp;
				parse_root_system_descriptor(&rsdp->first_part, &sys);
			} break;

      case MULTIBOOT_TAG_TYPE_FRAMEBUFFER: {
        struct multiboot_tag_framebuffer *fb = (struct multiboot_tag_framebuffer *)(tag);

        if(fb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB){
          klog_info("this is a text buffer");
        } else {
          sys.framebuffer_address = fb->common.framebuffer_addr;
          sys.framebuffer_width = fb->common.framebuffer_width;
          sys.framebuffer_height = fb->common.framebuffer_height;
          sys.framebuffer_depth = fb->common.framebuffer_bpp / 8;
          sys.framebuffer_pitch = fb->common.framebuffer_pitch;        
        }
      } break;

      case MULTIBOOT_TAG_TYPE_MMAP: {
        #if 0
        klog_debug("found mmap multiboot tag");
        struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap *)(tag);
        multiboot_memory_map_t *mmap_entry = (multiboot_memory_map_t *)(mmap_tag->entries);
        while((uintptr_t)mmap_entry < (uintptr_t)mmap_tag + mmap_tag->size){
          const char *MB_MMAP_TYPE_STRINGS[] = {
            "INVALID_TYPE",
            "MEMORY_AVAILABLE",
            "MEMORY_ACPI_RECLAIMABLE",
            "MEMORY_NON_VOLITALE_STORAGE",
            "MEMORY_BAD_RAM"
          };

          klog_debug("addr: %lu, size: %lu, type: %s", mmap_entry->addr, mmap_entry->len, MB_MMAP_TYPE_STRINGS[mmap_entry->type]);
          mmap_entry = (multiboot_memory_map_t *)((uintptr_t)mmap_entry + mmap_tag->entry_size);
        }
        #endif
      };

		}

		//kterm_redraw_if_required(&_kterm, &_iostate);
		tag = (struct multiboot_tag *)(((uint8_t *)tag) + ((tag->size + 7) & ~7));
	}

  klog_debug("copying trampoline code to 0x1000");
  memcpy(0x1000, TRAMPOLINE_BINARY, sizeof(TRAMPOLINE_BINARY));
  uintptr_t *p4_table_address = (uintptr_t *)0x2000;
  uintptr_t *trampoline_exit = (uintptr_t *)0x2008;
  *p4_table_address = (uintptr_t)&g_p4_table;
  *trampoline_exit = (uintptr_t)ap_entry_procedure;
  
  //Maping the APIC physical address to 2M for now
	uintptr_t lapic_physical_page, lapic_page_offset = 0;
	uintptr_t lapic_virtual_page = silly_page_map(sys.lapic_register_base, true, &lapic_physical_page, &lapic_page_offset);
  uintptr_t lapic_register_base_address = lapic_virtual_page + lapic_page_offset;
  lapic_initalize(lapic_register_base_address);
  globals.lapic_address = lapic_register_base_address;

  uintptr_t ioapic_physical_page, ioapic_page_offset;
  uintptr_t ioapic_base_address = silly_page_map(sys.ioapic_register_base, true, &ioapic_physical_page, &ioapic_page_offset);
  ioapic_initalize(ioapic_base_address);

  klog_info("ioapic: physical = %lu, virtual = %lu", sys.ioapic_register_base, ioapic_base_address);
  klog_info("lapic: physical = %lu, virtual = %lu", sys.lapic_register_base, lapic_register_base_address);

  //initalize_cpu(lapic_register_base_address, 1, 0x01);
  lapic_enable_timer(lapic_register_base_address);

  if(sys.framebuffer_address != 0){
    uintptr_t framebuffer_physical_page, framebuffer_page_offset;
    uintptr_t framebuffer_virtual_address = silly_page_map(sys.framebuffer_address, true, &framebuffer_physical_page, &framebuffer_page_offset);
    silly_page_map(sys.framebuffer_address + 0x200000, true, &framebuffer_physical_page, &framebuffer_page_offset);
    uint8_t *framebuffer = (uint8_t *)framebuffer_virtual_address;

    Framebuffer *fb = &globals.framebuffer;
    fb->width = sys.framebuffer_width;
    fb->height = sys.framebuffer_height;
    fb->buffer = framebuffer;
    fb->depth = sys.framebuffer_depth;
    fb->pitch = sys.framebuffer_pitch;

    klog_debug("framebuffer: width: %u, height: %u", fb->width, fb->height);

    for(size_t y = 0; y < sys.framebuffer_height; y++){
      for(size_t x = 0; x < sys.framebuffer_width; x++){
        size_t index = x*sys.framebuffer_depth + y*sys.framebuffer_pitch;
        framebuffer[index + 0] = 0x10;
        framebuffer[index + 1] = 0x10;
        framebuffer[index + 2] = 0x10;
      }
    }
  }
  
  kprocess_load_elf_executable((uintptr_t)TEST_PROGRAM_ELF);

	while(1) { asm volatile("hlt"); };
}
