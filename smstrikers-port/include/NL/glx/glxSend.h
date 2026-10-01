#ifndef _GLXSEND_H_
#define _GLXSEND_H_

#include "dolphin/gx/GXEnum.h"
#include "NL/gl/glView.h"

struct GLLightUserData;

void glx_SendFrame_cb(eGLView view, unsigned long flags, const glModelPacket* p);
void glx_SendEnd();
void glx_SendReset();

#if defined(PORT_VITA)
#include <stdio.h>
class GLRenderList;
void glx_PrepareSkinPackets(eGLView view, GLRenderList* list);
void glx_FinishSkinPackets();
void glx_ReportSkinPackets(FILE* out);
bool glx_PacketProfileSampledView(eGLView view);
void glx_ReportPacketProfile(FILE* out);
#endif

#endif // _GLXSEND_H_
