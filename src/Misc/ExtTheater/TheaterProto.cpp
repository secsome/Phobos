#include "TheaterProto.h"

// These are the listed default values for vanilla theaters

const TheaterProto TheaterProto::Array[] = {
	TheaterProto /* TEMPERATE */ {
		"TEMPERATE",
		"T",
		"TEM",
		"TEMPERATMD.INI",
		"TEMPERAT.PAL",
		"UNITTEM.PAL",
		"ISOTEM.PAL",
		"TEMPERAT.MIX, TEM.MIX, ISOTEMMD.MIX, ISOTEMP.MIX",
		1.0
	},
	TheaterProto /* SNOW */ {
		"SNOW",
		"A",
		"SNO",
		"SNOWMD.INI",
		"SNOW.PAL",
		"UNITSNO.PAL",
		"ISOSNO.PAL",
		"SNOWMD.MIX, SNOW.MIX, SNO.MIX, ISOSNOMD.MIX, ISOSNOW.MIX",
		0.8
	},
	TheaterProto /* URBAN */ {
		"URBAN",
		"U",
		"URB",
		"URBANMD.INI",
		"URBAN.PAL",
		"UNITURB.PAL",
		"ISOURB.PAL",
		"URBAN.MIX, URB.MIX, ISOURBMD.MIX, ISOURB.MIX",
		1.0
	},
	TheaterProto /* DESERT */ {
		"DESERT",
		"D",
		"DES",
		"DESERTMD.INI",
		"DESERT.PAL",
		"UNITDES.PAL",
		"ISODES.PAL",
		"DESERT.MIX, DES.MIX, ISODESMD.MIX, ISODES.MIX",
		1.0
	},
	TheaterProto /* NEWURBAN */ {
		"NEWURBAN",
		"N",
		"UBN",
		"URBANNMD.INI",
		"URBANN.PAL",
		"UNITUBN.PAL",
		"ISOUBN.PAL",
		"URBANN.MIX, UBN.MIX, ISOUBNMD.MIX, ISOUBN.MIX",
		1.0
	},
	TheaterProto /* LUNAR */ {
		"LUNAR",
		"L",
		"LUN",
		"LUNARMD.INI",
		"LUNAR.PAL",
		"UNITLUN.PAL",
		"ISOLUN.PAL",
		"LUNAR.MIX, LUN.MIX, ISOLUNMD.MIX, ISOLUN.MIX",
		1.0
	}
};

constexpr int TheaterProto::Size = sizeof(TheaterProto::Array) / sizeof(*TheaterProto::Array);
