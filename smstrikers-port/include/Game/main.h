#ifndef _MAIN_H_
#define _MAIN_H_

#include "NL/nlLocalization.h"

class LoadingManager;

extern bool g_bProfiling;
extern bool g_bTweaking;
extern bool g_e3_Build;
extern bool g_Europe;
extern bool g_bFranticPausing;
extern nlLocalization::nlLanguage g_Language;
extern LoadingManager* g_pTheLoadingManagerTask;

const int* GetRegion();

#if defined(PORT_VITA)
// Shader compilation is permitted only in explicit loading windows. Interactive
// front-end/gameplay phases run cache-only in the SEALED profile.
void PortVitaShaderCacheBeginLoading();
void PortVitaShaderCacheEndLoading(bool enteringGameplay);
void PortVitaShaderCacheEnterGameplay();
void PortVitaShaderCacheLeaveGameplay();
#endif

#endif // _MAIN_H_
