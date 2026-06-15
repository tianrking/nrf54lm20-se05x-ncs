#include <stdlib.h>

void *pvPortMalloc(size_t xWantedSize)
{
    return malloc(xWantedSize);
}

void vPortFree(void *pv)
{
    free(pv);
}
