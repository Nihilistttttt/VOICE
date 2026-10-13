#ifndef WATCH_MALLOC_H
#define WATCH_MALLOC_H
#include "ch32h417.h"
#ifndef NULL
#define NULL 0
#endif

// 内存参数设定
#define MEMORY_BLOCK_SIZE       8             // 内存块大小 8 字节
#define MEMORY_MAX_SIZE         (32*1024)      // 最大管理内存 8 KB（根据 FFT 需求调整）
#define MEMORY_ALLOC_TABLE_SIZE (MEMORY_MAX_SIZE / MEMORY_BLOCK_SIZE)

// 内存管理控制器
struct MemoryController
{
    void (*Init)(void);                 // 初始化
    uint8_t   (*GetUsagePercent)(void); // 内存使用率
    uint8_t   *MemoryBase;              // 内存池
    uint16_t  *AllocMap;                // 内存管理状态表
    uint8_t    IsReady;                 // 内存管理是否就绪
};
extern struct MemoryController g_MemoryController;

void  MemorySet(void *s, uint8_t c, uint32_t count);
void  MemoryCopy(void *dest, void *src, uint32_t n);
void  MemoryInit(void);
uint32_t   MemoryAllocBlock(uint32_t size);
uint8_t    MemoryFreeBlock(uint32_t offset);
uint8_t    MemoryGetUsagePercent(void);

void *AllocateMemory(uint32_t size);
void  FreeMemory(void *ptr);
void *ReallocateMemory(void *ptr, uint32_t size);
#endif //WATCH_MALLOC_H