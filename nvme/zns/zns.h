#ifndef __ZNS_H_
#define __ZNS_H_

/*Opcodes for ZNS IO Commands */
#define IO_ZNS_MANAGEMENT_SEND								0x79
#define IO_ZNS_MANAGEMENT_RECEIVE							0x7A

#define ZONE_CAP                            32768 // 32768 * 4096 = 128MB

#define MAXIMUM_ZONE_COUNT                  8   // 8 * 128MB = 1TB

/* Zone Descriptor - Zone State (ZS) */
#define EMPTY												0x1
#define IMPLICITLY_OPENED									0x2
#define EXPLICITLY_OPENED									0x3
#define CLOSED												0x4
#define READ_ONLY											0xD
#define FULL												0xE
#define OFFLINE												0xF

/* Zone Mangaement Send Zone Send Action (ZSA) */
#define CLOSE_ZONE											0x1
#define FINISH_ZONE											0x2
#define OPEN_ZONE											0x3
#define RESET_ZONE											0x4
#define OFFLINE_ZONE										0x5
#define SET_ZONE_DESCRIPTOR_EXTENSION						0x10

// Temporarily Zone Size: 128MB (4KB * (1 << (2+6+7)))
typedef struct _ZNS_ADDR
{
    union{
        unsigned int dword;
        struct{
            unsigned int PAGE_OFFSET        :2;    // Page size: 16384KB -> 14 bits
            unsigned int PAGE_COLUMN_ID     :6;     // # way * # ch / 2^(# FCG - 1) : 8 * 8 / 2 ^ 0 = 64 -> 6 bits
            unsigned int PAGE_ROW_ID        :7;     // # pages in a block : 128 -> 7 bits
            //unsigned int FCG_ID             :0;   // # FCG - 1 : (1-1) = 0
            unsigned int FBG_ID             :17;     // 32 - the bits above
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
    unsigned int Num_Open_Zone;
    unsigned int Num_Close_Zone;
    unsigned int Num_Full_Zone;
    unsigned int Num_Empty_Zone;
    unsigned int Num_Read_Zone;
    unsigned int Num_Off_Zone;
    ZONE_REG zoneReg[MAXIMUM_ZONE_COUNT];
} ZONE_MAP, *P_ZONE_MAP;

/* ZNS Identify Namespace Data Structure */
typedef struct _ADMIN_IDENTIFY_ZNS_COMMAND_SET
{
	struct
	{
		unsigned char variableZoneCapacity					:1;
		unsigned char zoneActiveExcursions					:1;
		unsigned int reserved0								:14;
	} ZOC;

	struct
	{
		unsigned char readAcrossZoneBoundaries				:1;
		unsigned int reserved0								:15;
	}OZCS;

	unsigned int MAR;
	unsigned int MOR;
	unsigned int RRL;
	unsigned int FRL;
	unsigned int RRL1;
	unsigned int RRL2;
	unsigned int RRL3;
	unsigned int FRL1;
	unsigned int FRL2;
	unsigned int FRL3;

	unsigned char reserved0[2772];

	struct
	{
		unsigned long long ZSZE;
		unsigned char ZDES;
		unsigned int reserve0;
	}LBAFE[64];

	unsigned char reserved1[256];

}ADMIN_IDENTIFY_ZNS_COMMAND_SET;

typedef struct _IO_ZNS_ZONE_MANGAEMENT_SEND_DW13
{
	union{
		unsigned int dword;
		struct{
			unsigned int ZSA					:8;
			unsigned int SELECT_ALL				:1;
			unsigned int reserved0				:23;
		};
	};
} IO_ZNS_ZONE_MANGAEMENT_SEND_DW13;

/* IO ZNS Zone Management Receive*/
typedef struct _IO_ZNS_ZONE_MANAGEMENT_RECEIVE_DW13
{
	union{
		unsigned int dword;
		struct {
			unsigned int ZRA					:8;
			unsigned int ZRA_specific_field 	:8;
			unsigned int ZRA_specific_feature	:1;
			unsigned int reserved0				:15;
		};
	};
} IO_ZNS_ZONE_MANAGEMENT_RECEIVE_DW13;



typedef struct _ZONE_DESCRIPTOR
{
	unsigned char ZT						:4;
	unsigned char reserved0					:4;
	unsigned char reserved1					:4;
	unsigned char ZS						:4;
	struct
	{
		unsigned char ZFC					:1;
		unsigned char FZR					:1;
		unsigned char RZR					:1;
		unsigned char reserved0				:4;
		unsigned char ZDEV					:1;
	} ZA;
	struct
	{
		unsigned char FZRTL					:2;
		unsigned char RZRTL					:2;
		unsigned char reserved0				:4;
	} ZAI;
	unsigned int reserved2;
	unsigned long long ZCAP;
	unsigned long long ZSLBA;
	unsigned long long WP;
	unsigned long long reserved3[4];
} ZONE_DESCRIPTOR;

typedef struct _IO_ZNS_MANAGEMENT_RECEIVE_ZONE_REPORT
{
	unsigned long long num_zone;
	unsigned long long reserved0[7];
	ZONE_DESCRIPTOR zone_descriptor[MAXIMUM_ZONE_COUNT];
} IO_ZNS_MANAGEMENT_RECEIVE_ZONE_REPORT;

#endif