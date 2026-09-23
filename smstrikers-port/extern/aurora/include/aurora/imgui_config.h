#pragma once

#define IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS 1

#ifdef __SWITCH__
// smstrikers-port: the default URL opener needs fork and execvp, which Horizon lacks.
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS 1
#endif

#ifdef __SWITCH__
// smstrikers-port: sdl3on2 defines SDL_IOStream as a macro for its own stream type.
typedef struct S3_IOStream* ImFileHandle;
#else
typedef struct SDL_IOStream SDL_IOStream;
typedef SDL_IOStream* ImFileHandle;
#endif
typedef unsigned long long ImU64;

ImFileHandle ImFileOpen(const char* filename, const char* mode);
bool ImFileClose(ImFileHandle file);
ImU64 ImFileGetSize(ImFileHandle file);
ImU64 ImFileRead(void* data, ImU64 size, ImU64 count, ImFileHandle file);
ImU64 ImFileWrite(const void* data, ImU64 size, ImU64 count, ImFileHandle file);
