#include "port/morphwatch.h"

void PortMorphWatchRegister(const void* obj, const void* fieldAddr, const char* tag)
{
    (void)obj;
    (void)fieldAddr;
    (void)tag;
}

void PortMorphWatchPoll(unsigned long frame)
{
    (void)frame;
}
