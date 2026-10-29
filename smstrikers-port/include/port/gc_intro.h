// The GameCube boot animation as the Vita splash screen, while the renderer prepares its shaders.

// Its files are packed into the VPK under app0:gcintro/ (see assets/vita/gcintro/README.md);
// vita_gc_intro = 0 turns it off. A button press skips the rest once the game is ready.

#ifndef PORT_GC_INTRO_H
#define PORT_GC_INTRO_H

#ifdef __cplusplus
extern "C" {
#endif

// Before vglInit*. Non-zero when the intro will run.
int PortGcIntroSetup(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_GC_INTRO_H
