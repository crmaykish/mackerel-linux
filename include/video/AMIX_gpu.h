#ifndef _AMIX_GPU_H
#define _AMIX_GPU_H

#define GPU_BASE 0xD0000000
#define GPU_MEM_SIZE (GPU_VRAM_SIZE + GPU_REG_SIZE)

#define GPU_VRAM_BASE GPU_BASE
#define GPU_VRAM_SIZE 0x50000
#define GPU_REG_OFF GPU_VRAM_SIZE
#define GPU_REG_BASE (GPU_VRAM_BASE + GPU_VRAM_SIZE)
#define GPU_REG_SIZE 0x20 //only palette for now


#define GPU_PALETTE_REG_OFF 0x00 //From regs

#endif