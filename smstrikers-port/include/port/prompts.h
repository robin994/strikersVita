#ifndef PORT_PROMPTS_H
#define PORT_PROMPTS_H

#ifdef __cplusplus
class nlFont;
union SDL_Event;

extern "C" {

void PortPromptsFontLoaded(nlFont* font);
void PortPromptsFontUnloading(nlFont* font);
void PortPromptsEvent(const SDL_Event* event);
#endif

void PortPromptsFrame(void);

void PortPromptsLegendsLoaded(void);
void PortPromptsLegendsUnloading(void);

int PortPromptsSetFamily(const char* family);

#ifdef __cplusplus
}
#endif

#endif // PORT_PROMPTS_H
