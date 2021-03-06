#pragma once
#include <string.h>

struct TheaterProto
{
	static inline const int FindIndexOrDefault(const char* name)
	{
		for (int i = 0; i < TheaterProto::Size; i++) {
			if (_stricmp(Array[i].name, name) == 0) {
				return i;
			}
		}
		return 0;
	};

	static inline const TheaterProto* GetOrDefault(int index)
	{
		if (index < 0 || index >= TheaterProto::Size) {
			index = 0;
		}
		return &TheaterProto::Array[index];
	};

	static inline const TheaterProto* GetOrDefault(char* name)
	{
		const int index = TheaterProto::FindIndexOrDefault(name);
		return &TheaterProto::Array[index];
	};

	static const TheaterProto Array[];
	static const int Size;

// properties:
    const char* name;
    const char* letter;
    const char* suffix;

    const char* controlFileName;

    const char* palOverlay;
    const char* palUnit;
    const char* palISO;

    const char* specificMix;

    const double radarBrightness;
};
