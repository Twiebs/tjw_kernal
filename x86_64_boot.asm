bits 32

global start

extern asm_longmode_entry
extern kernel_end

%include "multiboot2_header.asm"

section .gdt
GDT64:                         
.null:
	dq 0
.code:
	dq 0x0020980000000000 
.data:
  dq 0x0000900000000000 
.Pointer:                    ; The GDT-pointer.
	dw $ - GDT64 - 1             ; Limit.
	dd GDT64                     ; Base.

section .text

start:
	cli
	mov esp, stack_top
	mov edi, ebx

	call is_multiboot2_bootloader
	call is_cpuid_supported
	call is_longmode_supported
	
	;call fill_temporary_paging_tables
	call setup_paging_tables
	call enable_paging

	lgdt [GDT64.Pointer]
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	jmp 0x8:asm_longmode_entry

fill_temporary_paging_tables:
  mov eax, kernel_end
	add eax, (0x1000 - 1)
	and eax, ~(0x1000 - 1)

	mov cr3, eax 
	mov edi, eax
	xor eax, eax
	mov ecx, 0x1000
	rep stosd
	mov edi, cr3

	mov eax, edi
	add eax, 0x1000
	or eax, 0b11
	mov DWORD [edi], eax
	add edi, 0x1000
	mov eax, edi
	add eax, 0x1000
	or eax, 0b11
	mov DWORD [edi], eax
	add edi, 0x1000
	mov eax, edi
	add eax, 0x1000
	or eax, 0b11
	mov DWORD [edi], eax
	add edi, 0x1000

	mov ebx, 0b11
	mov ecx, 512
.set_page_entry:
  mov DWORD [edi], ebx
	add ebx, 0x1000
	add edi, 8
	loop .set_page_entry

	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax
	ret

setup_paging_tables:
	%define PAGING_PRESENT_BIT 			(1 << 0)
	%define PAGING_WRITEABLE_BIT 	 	(1 << 1)
	%define PAGING_USER_ACCESSIBLE 	(1 << 2)
	%define PAGING_WRITE_THFROUGH_CACHING (1 << 3)
	%define PAGING_DISABLE_CACHE (1 << 4)
	%define PAGING_HUGE_BIT 				(1 << 7)

	mov eax, p3_table
	or eax, (PAGING_PRESENT_BIT | PAGING_WRITEABLE_BIT) 
	mov [p4_table], eax

	mov eax, p2_table
	or eax, (PAGING_PRESENT_BIT | PAGING_WRITEABLE_BIT)
	mov [p3_table], eax

	;mov eax, 0x200000
	;or eax, (PAGING_PRESENT_BIT | PAGING_WRITABLE_BIT | PAGING_HUGE_BIT)
	;mov [p2_table]

	mov ecx, 0
.fill_p2_table:
  mov eax, 0x200000
	mul ecx
	or eax, (PAGING_PRESENT_BIT | PAGING_WRITEABLE_BIT | PAGING_HUGE_BIT)
	mov [p2_table + ecx * 8], eax
	inc ecx
	cmp ecx, 512
	jne .fill_p2_table
	ret
 
enable_paging:
	%define CR0_PAGING_ENABLED_BIT (1 << 31)
	%define CR4_PHYSICAL_ADDRESS_EXTENTION_BIT (1 << 5)
	%define MSR_LONG_MODE_BIT (1 << 8)
	%define EFLAGS_ID_BIT (1 << 21)
	%define EFER_MSR (0xC0000080)

	mov eax, p4_table
	mov cr3, eax

	mov eax, cr4                 
	or eax, CR4_PHYSICAL_ADDRESS_EXTENTION_BIT 
	mov cr4, eax 

	mov ecx, EFER_MSR
	rdmsr           
	or eax, MSR_LONG_MODE_BIT 
	wrmsr   

	mov eax, cr0                 
	or eax, CR0_PAGING_ENABLED_BIT 
	mov cr0, eax                 
	ret
	
; System Feature Checks
;============================================

%define ERROR_CODE_NO_MULTIBOOT "0"
%define ERROR_CODE_NO_CPUID "7"
%define ERROR_CODE_NO_LONGMODE "2"

is_multiboot2_bootloader:
  cmp eax, 0x36D76289
	jne .no_multiboot
	ret
.no_multiboot:
  mov al, ERROR_CODE_NO_MULTIBOOT 
	jmp error_handler

;CPUID Support
is_cpuid_supported: 
    ; Check if CPUID is supported by attempting to flip the ID bit (bit 21) in
    ; the FLAGS register. If we can flip it, CPUID is available.
    ; Copy FLAGS in to EAX via stack
    pushfd
    pop eax
 
    ; Copy to ECX as well for comparing later on
    mov ecx, eax
 
    ; Flip the ID bit
    xor eax, 1 << 21
 
    ; Copy EAX to FLAGS via the stack
    push eax
    popfd
 
    ; Copy FLAGS back to EAX (with the flipped bit if CPUID is supported)
    pushfd
    pop eax
 
    ; Restore FLAGS from the old version stored in ECX (i.e. flipping the ID bit
    ; back if it was ever flipped).
    push ecx
    popfd
 
    ; Compare EAX and ECX. If they are equal then that means the bit wasn't
    ; flipped, and CPUID isn't supported.
    xor eax, ecx
    jz .no_cpuid 
    ret
.no_cpuid:
  mov al, ERROR_CODE_NO_CPUID
	jmp error_handler

is_longmode_supported:
  mov eax, 0x80000000
	cpuid
	cmp eax, 0x80000001
	jb .no_longmode
	mov eax, 0x80000001
	cpuid
	test edx, 1 << 29
	jz .no_longmode
	ret
.no_longmode:
  mov al, ERROR_CODE_NO_LONGMODE
	jmp error_handler

error_handler:
	mov dword [0xb8000], 0x4f524f45
	mov dword [0xb8004], 0x4f3a4f52
	mov dword [0xb8008], 0x4f204f20
	mov byte  [0xb800a], al
	hlt

global p4_table
global p3_table
global p2_table

;===============================================
%define GDT_WRITE_ENABLED_BIT (1 << 41)
%define GDT_IS_CODE_SEGMENT_BIT (1 << 43)
%define GDT_TYPE_CODE_OR_DATA_BIT (1 << 44)
%define GDT_PRESENT_BIT (1 << 47)
%define GDT_IS_64_BIT (1 << 53)
section .bss
align 4096
p4_table:
  resb 4096
align 4096
p3_table:
  resb 4096
align 4096
p2_table:
  resb 4096

stack_bottom:
  resb 8192
stack_top:
