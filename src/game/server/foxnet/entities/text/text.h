#ifndef GAME_SERVER_FOXNET_ENTITY_TEXT_TEXT_H
#define GAME_SERVER_FOXNET_ENTITY_TEXT_TEXT_H

#include <base/log.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/server/entity.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>

#include <vector>

#define MAX_TEXT_LEN 32

constexpr int GlyphH = 8;
constexpr int GlyphW = 5;

#define EMPTY_GLYPH \
	{ \
		{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, { false, false, false, false, false } \
	}

static const bool asciiTable[256][GlyphH][GlyphW] = {
	EMPTY_GLYPH, // 0
	EMPTY_GLYPH, // 1
	EMPTY_GLYPH, // 2
	EMPTY_GLYPH, // 3
	EMPTY_GLYPH, // 4
	EMPTY_GLYPH, // 5
	EMPTY_GLYPH, // 6
	EMPTY_GLYPH, // 7
	EMPTY_GLYPH, // 8
	EMPTY_GLYPH, // 9
	EMPTY_GLYPH, // 10
	EMPTY_GLYPH, // 11
	EMPTY_GLYPH, // 12
	EMPTY_GLYPH, // 13
	EMPTY_GLYPH, // 14
	EMPTY_GLYPH, // 15
	EMPTY_GLYPH, // 16
	EMPTY_GLYPH, // 17
	EMPTY_GLYPH, // 18
	EMPTY_GLYPH, // 19
	EMPTY_GLYPH, // 20
	EMPTY_GLYPH, // 21
	EMPTY_GLYPH, // 22
	EMPTY_GLYPH, // 23
	EMPTY_GLYPH, // 24
	EMPTY_GLYPH, // 25
	EMPTY_GLYPH, // 26
	EMPTY_GLYPH, // 27
	EMPTY_GLYPH, // 28
	EMPTY_GLYPH, // 29
	EMPTY_GLYPH, // 30
	EMPTY_GLYPH, // 31
	EMPTY_GLYPH, // 32
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 33 !
	{{false, true, true, false, false}, {false, true, true, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 34 "
	{{false, true, false, true, false}, {true, true, true, true, true}, {false, true, false, true, false}, {true, true, true, true, true}, {false, true, false, true, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 35 #
	EMPTY_GLYPH, // 92, // 36 $
	EMPTY_GLYPH, // 92, // 37 %
	EMPTY_GLYPH, // 92, // 38 &
	{{false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 39 '
	{{false, false, true, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, false, true, false, false}}, // 40 (
	{{false, false, true, false, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, true, false, false}}, // 41 )
	{{false, true, false, true, false}, {false, false, true, false, false}, {false, true, false, true, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 42 *
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, true, true, true, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 43 +
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, true, false, false}}, // 44 ,
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 45 -
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 46 .

	{{false, false, false, true, false}, {false, false, false, true, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, false, false, false, false}}, // 47 /
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 48 0
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, true, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 49 1
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, true, false}, {false, true, false, false, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 50 2
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 51 3
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, false, false}}, // 52 4
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, true, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 53 5 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, true, false}, {false, true, false, false, false}, {false, true, true, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 54 6
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 55 7
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 56 8 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, true, false, true, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 57 9
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 58 :
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, true, false, false}}, // 59 ;
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, true, false}, {false, false, true, false, false}, {false, true, false, false, false}, {false, false, true, false, false}, {false, false, false, true, false}}, // 60 <
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 61 =
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, false, false, false}, {false, false, true, false, false}, {false, false, false, true, false}, {false, false, true, false, false}, {false, true, false, false, false}}, // 62 >
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, false, true, true, false}, {false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 63 ?
	{{false, false, false, false, false}, {false, true, true, true, true}, {true, false, false, false, true}, {true, false, true, true, true}, {true, false, true, false, true}, {true, false, true, true, true}, {true, false, false, false, false}, {false, true, true, true, false}}, // 64 @
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {true, false, false, true, false}, {true, true, true, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, false, false, false, false}}, // 65 A
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, true, true, false, false}, {false, false, false, false, false}}, // 66 B
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {false, true, true, false, false}, {false, false, false, false, false}}, // 67 C
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, true, true, false, false}, {false, false, false, false, false}}, // 68 D
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {false, false, false, false, false}}, // 69 E
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {false, false, false, false, false}}, // 70 F
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {true, false, false, false, false}, {true, false, true, true, false}, {true, false, false, true, false}, {true, true, true, true, false}, {false, false, false, false, false}}, // 71 G
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, true, true, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, false, false, false, false}}, // 72 H
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 73 I
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {false, false, false, true, false}, {false, false, false, true, false}, {true, false, false, true, false}, {false, true, true, false, false}, {false, false, false, false, false}}, // 74 J
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, true, false}, {true, false, true, false, false}, {true, true, false, false, false}, {true, false, true, false, false}, {true, false, false, true, false}, {false, false, false, false, false}}, // 75 K
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {false, false, false, false, false}}, // 76 L
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, true, false, true, true}, {true, false, true, false, true}, {true, false, false, false, true}, {true, false, false, false, true}, {false, false, false, false, false}}, // 77 M
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, true, false}, {true, true, false, true, false}, {true, false, true, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, false, false, false, false}}, // 78 N
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, true, true, false, false}, {false, false, false, false, false}}, // 79 O
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {false, false, false, false, false}}, // 80 P
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, true, true, false, false}, {false, false, false, true, false}}, // 81 Q
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, false, false, false, false}}, // 82 R
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, false, false}, {false, true, true, false, false}, {false, false, false, true, false}, {true, true, true, false, false}, {false, false, false, false, false}}, // 83 S
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, true}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 84 T
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, true, true, false, false}, {false, false, false, false, false}}, // 85 U
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, false, false, false, true}, {false, true, false, true, false}, {false, true, false, true, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 86 V
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, false, false, false, true}, {true, false, true, false, true}, {true, false, true, false, true}, {false, true, false, true, false}, {false, false, false, false, false}}, // 87 W
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, false, true, false, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, false, false, false, false}}, // 88 X
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, false, false, false, true}, {false, true, false, true, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, false, false}}, // 89 Y
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, false, true, false, false}, {false, true, false, false, false}, {false, true, true, true, false}, {false, false, false, false, false}}, // 90 Z
	{{true, true, true, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, true, true, false, false}}, // 91 [ 
	{{false, true, false, false, false}, {false, true, false, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, false, false}}, // 92 "\"
	{{false, false, true, true, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, true, true, true}}, // 93 ]
	{{false, false, true, false, false}, {false, true, false, true, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 94 ^
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}}, // 95 _
	{{false, true, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}}, // 96 `
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {false, false, false, false, true}, {false, true, true, true, true}, {true, false, false, false, true}, {false, true, true, true, true}}, // 97 a
	{{true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, true, true, false, false}}, // 98 b
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {false, true, true, true, false}}, // 99 c
	{{false, false, false, false, true}, {false, false, false, false, true}, {false, false, false, false, true}, {false, false, true, true, true}, {false, true, false, false, true}, {false, true, false, false, true}, {false, true, false, false, true}, {false, false, true, true, true}}, // 100 d
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, false, true}, {true, true, true, true, true}, {true, false, false, false, false}, {false, true, true, true, true}}, // 101 e 
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {true, false, false, false, false}, {true, true, true, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}}, // 102 f
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, true}, {true, false, false, false, true}, {true, false, false, false, true}, {false, true, true, true, true}, {false, false, false, false, true}, {true, true, true, true, false}}, // 103 g
	{{false, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, true, true, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {true, false, false, true, false}}, // 106 h
	{{false, false, false, false, false}, {false, false, true, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, true, true, false}}, // 105 i
	{{false, false, true, false, false}, {false, false, false, false, false}, {false, true, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, false, false, false}}, // 104 j
	{{true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, true, false}, {true, true, true, false, false}, {true, true, false, false, false}, {true, false, true, false, false}, {true, false, false, true, false}}, // 107 k
	{{true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}, {false, true, true, true, false}}, // 108 l
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, true, false, true, false}, {true, false, true, false, true}, {true, false, true, false, true}, {true, false, true, false, true}, {true, false, true, false, true}}, // 109 m
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {true, false, false, false, true}, {true, false, false, false, true}, {true, false, false, false, true}, {true, false, false, false, true}}, // 110 n
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, false, true}, {true, false, false, false, true}, {true, false, false, false, true}, {false, true, true, true, false}}, // 111 o 
	{{false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {true, false, false, false, true}, {true, false, false, false, true}, {true, true, true, true, false}, {true, false, false, false, false}, {true, false, false, false, false}}, // 112 p 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, true, true, true, false}, {false, false, false, true, false}, {false, false, false, true, false}}, // 113 q
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, false}, {true, false, false, false, true}, {true, false, false, false, false}, {true, false, false, false, false}, {true, false, false, false, false}}, // 114 r 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, true, true, true}, {true, false, false, false, false}, {false, true, true, true, false}, {false, false, false, false, true}, {true, true, true, true, false}}, // 115 s
	{{false, false, false, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, true, true, true, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, false, true, false}}, // 116 t 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, false, false, true}, {false, true, false, false, true}, {false, true, false, false, true}, {false, true, false, false, true}, {false, true, true, true, true}}, // 117 u 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, false, false, false, true}, {false, true, false, true, false}, {false, true, false, true, false}, {false, false, true, false, false}}, // 118 v 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, false, true}, {true, false, false, false, true}, {true, false, true, false, true}, {true, false, true, false, true}, {false, true, false, true, false}}, // 119 w 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {false, true, false, true, false}, {false, true, false, true, false}, {false, false, true, false, false}, {false, true, false, true, false}, {false, true, false, true, false}}, // 120 x
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, false, false, true, false}, {true, false, false, true, false}, {false, true, true, true, false}, {false, false, false, true, false}, {true, true, true, false, false}}, // 121 y 
	{{false, false, false, false, false}, {false, false, false, false, false}, {false, false, false, false, false}, {true, true, true, true, false}, {false, false, false, true, false}, {false, false, true, false, false}, {false, true, false, false, false}, {true, true, true, true, false}}, // 122 z 
	{{false, false, true, true, false}, {false, true, false, false, false}, {false, true, false, false, false}, {true, true, false, false, false}, {true, true, false, false, false}, {false, true, false, false, false}, {false, true, false, false, false}, {false, false, true, true, false}}, // 123 {
	{{false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}, {false, false, true, false, false}}, // 124 |
	{{false, true, true, false, false}, {false, false, false, true, false}, {false, false, false, true, false}, {false, false, false, true, true}, {false, false, false, true, true}, {false, false, false, true, false}, {false, false, false, true, false}, {false, true, true, false, false}}, // 125 }
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
	EMPTY_GLYPH,
};

struct FTextData
{
	int m_Id;
	vec2 m_Pos;
};

class CText : public CEntity
{
public:
	int m_AliveTicks;
	int m_CurTicks;
	int m_StartTick;

	int m_Owner;

	CClientMask m_Mask;

	std::vector<FTextData *> m_pData;

	float m_CenterX;

	char m_aText[MAX_TEXT_LEN];

	void SetData(float Cell);

	CText(CGameWorld *pGameWorld, vec2 Pos, int Owner, int AliveTicks, const char *pText, int EntType);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	virtual void DoSnap(int SnappingClient) {}
};

inline void CText::Reset()
{
	if(g_Config.m_SvExtraLogging >= 2)
		log_info("text", "Reset");

	Server()->SnapFreeId(GetId());

	for(auto *pData : m_pData)
		Server()->SnapFreeId(pData->m_Id);

	GameWorld()->RemoveEntity(this);
}

inline void CText::Tick()
{
	if(++m_CurTicks - m_StartTick > m_AliveTicks)
		Reset();
}

inline void CText::Snap(int SnappingClient)
{
	DoSnap(SnappingClient);
}

inline float GlyphYOffset(unsigned char ch)
{
	switch(ch)
	{
	case 'g':
	case 'p':
	case 'q':
	case 'j':
	case 'f':
		return 0.85f;
	default:
		return 0.0f;
	}
}

// Horizontal gap per glyph (in columns)
inline float GlyphXGap(unsigned char ch)
{
	switch(ch)
	{
	case ' ':
		return 1.65f; // bigger gap for space
	default:
		return 0.75f; // normal positive gap for letters
	}
}

inline void CText::SetData(float Cell)
{
	vec2 Base = m_Pos;
	int Len = str_length(m_aText);

	// Horizontal cursor measured in columns
	float xCursorCols = 0;

	for(int ci = 0; ci < Len && ci < MAX_TEXT_LEN; ++ci)
	{
		unsigned char ch = (unsigned char)m_aText[ci];

		// Find tight horizontal bounds for the glyph: [minC, maxC]
		int minC = GlyphW; // first column with a pixel
		int maxC = -1; // last column with a pixel

		for(int r = 0; r < GlyphH; ++r)
		{
			for(int c = 0; c < GlyphW; ++c)
			{
				if(asciiTable[ch][r][c])
				{
					if(c < minC)
						minC = c;
					if(c > maxC)
						maxC = c;
				}
			}
		}

		// Handle empty glyphs (no pixels): width 0, but still add gap
		float TightWidth = (maxC >= minC) ? (float)(maxC - minC + 1) : 0.0f;

		// Compute vertical offset in cells for this glyph
		const float yOffCells = GlyphYOffset(ch) - 2.0f;

		// Render only the tight columns
		if(TightWidth > 0.0f)
		{
			for(int r = 0; r < GlyphH; ++r)
			{
				for(int c = minC; c <= maxC; ++c)
				{
					if(!asciiTable[ch][r][c])
						continue;

					// Use cell offsets first, then scale
					const float heightCells = (float)r;
					const float widthCells = (float)(c - minC);

					vec2 P = Base + vec2((xCursorCols + widthCells) * Cell * 0.70f,
								(heightCells + yOffCells) * Cell * 0.70f);

					int Id = Server()->SnapNewId();

					FTextData *pD = new FTextData();
					pD->m_Id = Id;
					pD->m_Pos = P;
					m_pData.push_back(pD);
				}
			}
		}

		// Advance cursor by tight glyph width + per-glyph gap (in columns), then scale uniformly
		xCursorCols += TightWidth + GlyphXGap(ch);
	}
	m_CenterX = (xCursorCols * Cell * 0.70f) / 2.0f;
}

inline CText::CText(CGameWorld *pGameWorld, vec2 Pos, int Owner, int AliveTicks, const char *pText, int EntType) :
	CEntity(pGameWorld, EntType, Pos)
{
	m_AliveTicks = AliveTicks;
	m_CurTicks = 0;
	m_Owner = Owner;
	m_StartTick = Server()->Tick();
	str_copy(m_aText, pText ? pText : "", sizeof(m_aText));
}

class CProjectileText : public CText
{
	int m_Type;

public:
	CProjectileText(CGameWorld *pGameWorld, vec2 Pos, int Owner, int AliveTicks, const char *pText, int Type = WEAPON_HAMMER);

	void Snap(int SnappingClient) override;
};

class CLaserText : public CText
{
public:
	CLaserText(CGameWorld *pGameWorld, vec2 Pos, int Owner, int AliveTicks, const char *pText);

	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_FOXNET_ENTITY_TEXT_TEXT_H
