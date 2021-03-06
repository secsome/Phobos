#pragma once
#include "../../Phobos.h"
#include <CCINIClass.h>

class MixFileClass;

class TheaterExt
{
    static void LoadFromINI();

    static void LoadSpecificMixs();
    static void UnloadSpecificMixs();

public:
    static inline CCINIClass* OpenConfig(const char* str) {
        char configFileName[0x80];
        _snprintf_s(configFileName, sizeof(configFileName), sizeof(configFileName) - 1, "%s.theater", str);
        return Phobos::OpenConfig(configFileName);
    }

    static void Init();

    static inline bool SameTheater() {
        return (_stricmp(TheaterExt::name, TheaterExt::nameLast) == 0);
    }

    // properties:
    static inline char ID[0x80];
    static inline char nameLast[0x80];

    static inline int  slot = 0;
    static inline char name[0x80];

    static inline char controlFileName[0x80];

    static inline char palOverlay[0x80];
    static inline char palUnit[0x80];
    static inline char palISO[0x80];

    static inline char suffix[4];
    static inline char letter[2];
    static inline float radarBrightness = 1.0f;

    static inline DynamicVectorClass<char*> specificMixNames;
    static inline DynamicVectorClass<MixFileClass*> specificMixFiles;

private:
    static inline char specificMixNames_buff[1024];

    static inline DWORD protectFlags = 0x0;
};
