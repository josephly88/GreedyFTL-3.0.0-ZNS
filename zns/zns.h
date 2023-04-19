#ifndef __ZNS_H_
#define __ZNS_H_

#define MAXIMUM_OPEN_ZONE_COUNT             1
#define MAXIMUM_CLOSE_ZONE_COUNT            0

// Temporarily 
typedef struct _ZNS_ADDR
{
    union{
        unsigned int dword;
        struct{
            unsigned int BLOCK_OFFSET       :14;    // Page size: 16384KB -> 14 bits
            unsigned int CHIP_ID            :6;     // # way * # ch / 2^(# FCG - 1) : 8 * 8 / 2 ^ 0 = 64 -> 6 bits
            unsigned int STRIP_ID           :7;     // # pages in a block : 128 -> 7 bits
            //unsigned int FCG_ID             :0;   // # FCG - 1 : (1-1) = 0
            unsigned int FBG_ID             :5;     // 32 - the bits above
        };
    };
}ZNS_ADDR;

typedef struct _ZONE_REG
{
    unsigned int Zone_ID;
    unsigned int FBG;
    unsigned char Zone_State;
    unsigned int Write_Pointer;
} ZONE_REG, *P_ZONE_REG;

typedef struct _ZONE_MAP
{
    ZONE_REG zoneReg[MAXIMUM_OPEN_ZONE_COUNT];
} ZONE_MAP, *P_ZONE_MAP;

#endif