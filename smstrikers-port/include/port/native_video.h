#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int PortNativeVideoEnabled(void);
int PortNativeVideoDecode(const void*, size_t, void*, void*, void*);
void PortNativeVideoReset(void);
#ifdef __cplusplus
}
class nlFile;
nlFile* PortOpenNativeMovie(const char* path);
#endif
