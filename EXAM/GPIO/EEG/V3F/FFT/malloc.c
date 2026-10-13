#include "malloc.h"

// 4 字节对齐的内存池（解决地址对齐问题）
__attribute__((aligned(4))) uint8_t g_MemoryPool[MEMORY_MAX_SIZE];
// 内存管理表
uint16_t g_AllocMap[MEMORY_ALLOC_TABLE_SIZE];

const uint32_t g_AllocTableSize = MEMORY_ALLOC_TABLE_SIZE;
const uint32_t g_BlockSize      = MEMORY_BLOCK_SIZE;
const uint32_t g_MemorySize     = MEMORY_MAX_SIZE;

struct MemoryController g_MemoryController =
        {
                MemoryInit,
                MemoryGetUsagePercent,
                g_MemoryPool,
                g_AllocMap,
                0,          // 未就绪
        };

void MemoryCopy(void *dest, void *src, uint32_t n)
{
    uint8_t *pDest = dest;
    uint8_t *pSrc = src;
    while (n--) *pDest++ = *pSrc++;
}

void MemorySet(void *s, uint8_t c, uint32_t count)
{
    uint8_t *p = s;
    while (count--) *p++ = c;
}

void MemoryInit(void)
{
    MemorySet(g_MemoryController.AllocMap, 0, g_AllocTableSize * 2);
    MemorySet(g_MemoryController.MemoryBase, 0, g_MemorySize);
    g_MemoryController.IsReady = 1;
}

uint8_t MemoryGetUsagePercent(void)
{
    uint32_t used = 0;
    uint32_t i;
    for (i = 0; i < g_AllocTableSize; i++)
        if (g_MemoryController.AllocMap[i]) used++;
    return (used * 100) / g_AllocTableSize;
}

uint32_t MemoryAllocBlock(uint32_t size)
{
    signed long offset = 0;
    uint16_t neededBlocks;
    uint16_t consecutiveBlocks = 0;
    uint32_t i;

    if (!g_MemoryController.IsReady) g_MemoryController.Init();
    if (size == 0) return 0xFFFFFFFF;

    neededBlocks = size / g_BlockSize;
    if (size % g_BlockSize) neededBlocks++;

    for (offset = g_AllocTableSize - 1; offset >= 0; offset--)
    {
        if (!g_MemoryController.AllocMap[offset]) consecutiveBlocks++;
        else consecutiveBlocks = 0;
        if (consecutiveBlocks == neededBlocks)
        {
            for (i = 0; i < neededBlocks; i++)
                g_MemoryController.AllocMap[offset + i] = neededBlocks;
            return (offset * g_BlockSize);
        }
    }
    return 0xFFFFFFFF;
}

uint8_t MemoryFreeBlock(uint32_t offset)
{
    int i;
    if (!g_MemoryController.IsReady)
    {
        g_MemoryController.Init();
        return 1;
    }
    if (offset < g_MemorySize)
    {
        int index = offset / g_BlockSize;
        int nmemb = g_MemoryController.AllocMap[index];
        for (i = 0; i < nmemb; i++)
            g_MemoryController.AllocMap[index + i] = 0;
        return 0;
    }
    return 2;
}

void FreeMemory(void *ptr)
{
    uint32_t offset;
    if (ptr == NULL) return;
    offset = (uint32_t)ptr - (uint32_t)g_MemoryController.MemoryBase;
    MemoryFreeBlock(offset);
}

void *AllocateMemory(uint32_t size)
{
    uint32_t offset;
    offset = MemoryAllocBlock(size);
    if (offset == 0xFFFFFFFF) return NULL;
    return (void *)((uint32_t)g_MemoryController.MemoryBase + offset);
}

void *ReallocateMemory(void *ptr, uint32_t size)
{
    uint32_t offset;
    offset = MemoryAllocBlock(size);
    if (offset == 0xFFFFFFFF) return NULL;
    MemoryCopy((void *)((uint32_t)g_MemoryController.MemoryBase + offset), ptr, size);
    FreeMemory(ptr);
    return (void *)((uint32_t)g_MemoryController.MemoryBase + offset);
}