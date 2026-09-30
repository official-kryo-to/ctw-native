#pragma once
#include <string>
bool Viewer_Init(const std::string& dataDir);
bool Viewer_Update();   // returns false when the user asked to quit
void Viewer_Render();
void Viewer_ShowText(const char* gxtName, int stringIndex, int language);  // 0=en 1=fr 2=de 3=it 4=es 5=ja
void Viewer_ShowModel(int resourceId, float yawDeg, float pitchDeg);
void Viewer_ShowAudio(int track);   // selects and starts playing a track
void Viewer_ShowWorld(float x, float y, float z, float yawDeg, float pitchDeg);   // city fly-camera, fully streamed in
void Viewer_Shutdown();
void Viewer_Key(char letter);   // simulate a letter key (A-Z) in the current mode
void Viewer_Select(int index);                  // jump to an item (for --shot)
bool Viewer_SaveScreenshot(const char* bmpPath); // reads back the framebuffer
