#ifndef __ZNS_H_
#define __ZNS_H_

#include "../../ftl_config.h"

#define ZNS_IO_COMMAND_SET									1

/* NVME_BLOCKS_PER_ZONE: Zone Capacity */
#define MB_PER_ZONE											(2*1024)		// 2GB
#define NVME_BLOCKS_PER_ZONE								((MB_PER_ZONE * 1024) / (BYTES_PER_NVME_BLOCK / 1024))		// 2GB / 4KB (NVMe Block Size)

/* Zone NVMe LBA Range */
#define MAXIMUM_ZONE_COUNT                  				1 				// 1073741824 / ZONE_SIZE		// 1TB / Zone size 
#define ZNS_LBA_START										0x8000000	// 0.5 TB / 4KB (NVMe Block Size)
#define ZNS_LBA_END											(ZNS_LBA_START + (MAXIMUM_ZONE_COUNT * NVME_BLOCKS_PER_ZONE))

/* Zone Group: # of Flash Block */
#define TOTAL_GROUP_OF_LAYER								512			// 1TB / 2GB = 512
#define BLOCK_LAYER_PER_GROUP								(USER_BLOCKS_PER_DIE / TOTAL_GROUP_OF_LAYER)
#define BTYE_PER_BLOCK_LAYER								((unsigned int)USER_DIES * BYTES_PER_DATA_REGION_OF_PAGE * USER_PAGES_PER_BLOCK * BLOCK_LAYER_PER_GROUP)

#define ZONE_GROUP_START									256
#define ZONE_GROUP_END										(ZONE_GROUP_START + MAXIMUM_ZONE_COUNT)



/*Opcodes for ZNS IO Commands */
#define IO_ZNS_MANAGEMENT_SEND								0x79
#define IO_ZNS_MANAGEMENT_RECEIVE							0x7A

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

// Temporarily Zone Size: 2GB
typedef struct _ZNS_ADDR
{
    union{
        unsigned int dword;
        struct{
			// A Block Layer is 128 MB if spaning all the channels x die (15-bits)
			unsigned int PAGE_COLUMN_ID     			:6;     	// # way * # ch / 2^(# FCG - 1) : 8 * 8 / 2 ^ 0 = 64 -> 6 bits
            unsigned int PAGE_ROW_ID        			:7;     	// # pages in a block : 128 -> 7 bits
            //unsigned int FCG_ID             :0;   	// # FCG - 1 : (1-1) = 0
			unsigned int INNER_BLOCK_GROUP_ROW_ID		:4;			// 2GB / 128MB = 16 -> 4 bits
            unsigned int OUTER_BLOCK_GROUP_ROW_ID 		:15;     	// 32 - the bits above (Actually 1TB/2GB = 512 -> 9 bits is really using)
        };
    };
}ZNS_ADDR;

typedef struct _ZONE_REG
{
    unsigned int Zone_ID;
    unsigned int OUTER_BLOCK_GROUP_ROW_ID;
    unsigned char Zone_State;
	unsigned int SLBA;
    unsigned int Write_Pointer;
} ZONE_REG, *P_ZONE_REG;

// Zone ID Extractor, similar to ZNS_ADDR
typedef struct _ZONE_ID_EXTRACTOR_SLBA
{
    union{
        unsigned int dword;
        struct{
			// A Block Layer is 128 MB if spaning all the channels x die (15-bits)
            unsigned int reserved0	        			:19;
            unsigned int ZONE_ID			 			:13;     	// 32 - the bits above (Actually 1TB/2GB = 512 -> 9 bits is really using)
        };
    };
} ZONE_ID_EXTRACTOR_SLBA;

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
