// The gcintro scene renderer; used only by gc_intro.c.
#ifndef STRIKERS_GC_INTRO_SCENE_H
#define STRIKERS_GC_INTRO_SCENE_H

#include <vitaGL.h>

#define GC_INTRO_DIR "app0:gcintro/"

int gcSceneInit(const vglSplashDetails *details);
float gcSceneDuration(void);
void gcSceneDraw(const vglSplashDetails *details, float fade);
void gcSceneTerm(void);

#endif
