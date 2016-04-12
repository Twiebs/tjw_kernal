//Legacy serial port driver for debugging purposes via bochs or qemu

#define HW_COM1_PORT 0x3F8
#define HW_COM2_PORT 0x2F8
#define HW_COM3_PORT 0x3E8
#define HW_COM4_PORT 0x2E8

#define SERIAL_DATA_REGISTER(port) (port + 0)
#define SERIAL_INTERUPT_ENABLE_REGISTER(port) (port + 1)
#define SERIAL_INTERUPT_IDENTIFICATION_AND_FIFIO_CONTROL_REGISTER(port) (port + 2)
#define SERIAL_LINE_CONTROL_REGISTER(port) (port + 3)
#define SERIAL_LINE_STATUS_PORT(port) (port + 5)

#define HW_SERIAL_DIVISOR_LATCH_ACCESS_BIT (1 << 7)

#define PORT 0x3F8
void serial_debug_init() 
{
   write_port(PORT + 1, 0x00);    // Disable all interrupts
   write_port(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   write_port(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   write_port(PORT + 1, 0x00);    //                  (hi byte)
   write_port(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   write_port(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   write_port(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int is_transmit_empty() {
   return read_port(PORT + 5) & 0x20;
}
 
void write_serial(char a) {
   while (is_transmit_empty() == 0);
   write_port(PORT,a);
}

internal void
serial_enable_port(uint16_t port) 
{
	write_port(SERIAL_INTERUPT_ENABLE_REGISTER(port), 0x00);
	write_port(SERIAL_LINE_CONTROL_REGISTER(port), 
			HW_SERIAL_DIVISOR_LATCH_ACCESS_BIT);
	write_port(SERIAL_DATA_REGISTER(port) + 0, 0x03);
	write_port(SERIAL_DATA_REGISTER(port) + 1, 0x00); 
	write_port(SERIAL_LINE_CONTROL_REGISTER(port), 0x03);
	write_port(SERIAL_INTERUPT_IDENTIFICATION_AND_FIFIO_CONTROL_REGISTER(port), 0xC7);
}

internal void 
serial_write_char(uint16_t port, char c) {
	bool is_fifo_empty = false;
	while(is_fifo_empty == false) {
		uint8_t status = read_port(SERIAL_LINE_STATUS_PORT(port));
		is_fifo_empty = (status & 0x20);
	}

	write_port(port, c);
}
