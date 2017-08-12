#define ARRAY_COUNT(array) (sizeof(array)/sizeof(*array))

void memory_copy(void *dest, const void *src, size_t size);
void memory_set(void *dest, uint8_t value, size_t size);
uint64_t memory_align(uint64_t address, uint64_t align);