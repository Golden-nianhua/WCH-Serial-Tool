#ifndef CH572_MODE_MEMORY_H
#define CH572_MODE_MEMORY_H

#include <stdint.h>

#ifndef MODE_MEMORY_SIZE
#define MODE_MEMORY_SIZE (512U * 7U)
#endif

extern uint32_t mode_memory[MODE_MEMORY_SIZE / sizeof(uint32_t)];

#endif
