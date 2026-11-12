#pragma once
#include <stdint.h>

// Same values as SCE_CTRL_* (psp2common/ctrl.h), so pure code builds on the host.
// touchzones.c checks them with _Static_assert.
#define PAD_SELECT   0x00000001u
#define PAD_L3       0x00000002u
#define PAD_R3       0x00000004u
#define PAD_START    0x00000008u
#define PAD_UP       0x00000010u
#define PAD_RIGHT    0x00000020u
#define PAD_DOWN     0x00000040u
#define PAD_LEFT     0x00000080u
#define PAD_LTRIGGER 0x00000100u
#define PAD_RTRIGGER 0x00000200u
#define PAD_L1       0x00000400u
#define PAD_R1       0x00000800u
#define PAD_TRIANGLE 0x00001000u
#define PAD_CIRCLE   0x00002000u
#define PAD_CROSS    0x00004000u
#define PAD_SQUARE   0x00008000u
#define PAD_PS       0x00010000u

// Bits the client turns into DualShock 4 parts (Client/ViGEm.cpp:141-220); anything else is ignored by the mirror
#define PAD_MIRROR_MASK 0x0001FFFFu
// Bits a zone may produce
#define PAD_ZONE_MASK (PAD_L1 | PAD_R1 | PAD_L3 | PAD_R3)
