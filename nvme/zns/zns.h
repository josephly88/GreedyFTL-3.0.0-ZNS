#ifndef __ZNS_H_
#define __ZNS_H_

#include "math.h"
#include "../../ftl_config.h"

/*-------------------------------------------------------------
		Section : Input Parameters
-------------------------------------------------------------*/
#define ZNS_IO_COMMAND_SET									1			// 0-Normal, 1-ZNS

// Row Group: 1-8192 (Must be a factor of 8192 (USER_BLOCKS_PER_DIE). E.g., 8192 / 1024 = 8 that has no remainder)
#define NUM_OF_BLOCK_PER_ZONE								16			// 1 Block : 2MB
// Column Group: 1-64 (Must be a factor of 64 (USER_DIES). E.g., 64 / 4 = 16 that has no remainder)
#define NUM_OF_DIE_PER_ZONE									32

#define SLICE_PER_STRIPE									(NUM_OF_DIE_PER_ZONE)

#define MAXIMUM_OPEN_ZONE_COUNT                  			64
#define MAXIMUM_ACTIVE_ZONE_COUNT                  			64
#define ZNS_LBA_START_NVME_BLOCK							0			// zone 0 starts at host LBA 0

// 0-Channel Oriented, 1-Way Oriented
#define CHANNEL_WAY_ORIENTED								0
// 0-Disable, 1-Enable
#define BLOCK_SHUFFLE_ENABLE								0

// Each Stripe is NUM_OF_DIE_PER_ZONE

// 0: Per-zone, 1: Unified
#define BUFFER_MODE											(0)		
#define ADDITION_DATA_BUFFER_PER_OPEN_ZONE					(0)	// 2-stripe would be (SLICE_PER_STRIPE)
// Options: -1 (Last), N/2 (N/2 Ping-Pong)
#define EVICTION_OFFSET										(-1)    
// Only for BUFFER_MODE 0, MOD 1 is Weight Balance Evictoin, MOD 2 is Sizing, MOD 3 is Weight Balance Eviction + Sizing
#define NON_SHARE_MOD										(0)
#define EVICTION_THRESHOLD									(16)

// Each Entry is a slice (16KB)
#define ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT					(64)

/*-------------------------------------------------------------
		Section : Output Parameters
-------------------------------------------------------------*/

/* NVME_BLOCKS_PER_ZONE: Zone Capacity */
#define MB_PER_ZONE											(MB_PER_BLOCK * NUM_OF_BLOCK_PER_ZONE * NUM_OF_DIE_PER_ZONE)
#define NVME_BLOCKS_PER_ZONE								((MB_PER_ZONE * 1024) / (BYTES_PER_NVME_BLOCK / 1024))		// NVMe Block Size: 4KB
#define SLICE_PER_ZONE										(NVME_BLOCKS_PER_ZONE / NVME_BLOCKS_PER_SLICE)			

/* Zone Group: # of Block Group Per Die */
#define BLOCK_GROUP_IN_ROW									(8192 / NUM_OF_BLOCK_PER_ZONE)
#define BLOCK_GROUP_IN_COLUMN								(64 / NUM_OF_DIE_PER_ZONE)
#define BLOCK_GROUP_PER_SSD									(BLOCK_GROUP_IN_ROW * BLOCK_GROUP_IN_COLUMN)
#define BLOCK_PER_BLOCK_GROUP								(NUM_OF_BLOCK_PER_ZONE * NUM_OF_DIE_PER_ZONE)

/* Zone NVMe LBA Range */
#define ZNS_LBA_END_NVME_BLOCK								(ZNS_LBA_START_NVME_BLOCK + (MAXIMUM_ACTIVE_ZONE_COUNT * NVME_BLOCKS_PER_ZONE) - 1)
#define ZNS_LSA_START										(ZNS_LBA_START_NVME_BLOCK / NVME_BLOCKS_PER_SLICE)
#define ZNS_LSA_END											(ZNS_LBA_END_NVME_BLOCK / NVME_BLOCKS_PER_SLICE)

/* Zone Data Buffer Range */
#define DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE				(SLICE_PER_STRIPE + ADDITION_DATA_BUFFER_PER_OPEN_ZONE)
#define ZNS_DATA_BUFFER_ENTRY_START							(AVAILABLE_DATA_BUFFER_ENTRY_COUNT)

#define OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT					(MAXIMUM_OPEN_ZONE_COUNT * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE)
#define BUFFER_QUEUE_DEPTH									(OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT / 8)

#define ZONE_BLOCK_GROUP_START								(ZNS_LBA_START_NVME_BLOCK / NVME_BLOCKS_PER_ZONE)

/*-------------------------------------------------------------
		Section : Strcut for ZNS Metadata
-------------------------------------------------------------*/

#define Lba2ZoneId(lba)										((lba - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE)
#define Lsa2ZoneId(logicalSliceAddr)						(logicalSliceAddr / SLICE_PER_ZONE)

typedef struct _ZNS_VirtualSliceAddr
{
    union{
        unsigned int dword;
        struct{
			unsigned int chNo			     			: 3;
            unsigned int wayNo		        			: 3;
            unsigned int pageNo					 		: 7;
			unsigned int blockNo						: 13; 
        };
    };
}ZNS_VirtualSliceAddr;

typedef struct _ZONE_REG
{
    unsigned char Zone_State;
	unsigned int SLBA;
    unsigned int Write_Pointer;
	int Buffer_ID;
	int Phy_Block_Group_ID;
} ZONE_REG;

typedef struct _ZONE_MAP
{
    unsigned int Num_Open_Zone;
    ZONE_REG zoneReg[MAXIMUM_ACTIVE_ZONE_COUNT];
} ZONE_MAP, *P_ZONE_MAP;

typedef struct _VALID_BLOCK_GROUP_FIFO
{
	int FIFO_LIST[BLOCK_GROUP_PER_SSD];
	unsigned int Valid_Count;
	int Head;	// Head of FIFO
	int Rear;	// Tail of FIFO
	int Num;
} VALID_BLOCK_GROUP_FIFO, *P_VALID_BLOCK_GROUP_FIFO;

typedef struct _ZONE_WRITE_BUF_REG
{
	int ZoneID;
	int curBufWriteIdx;
	int dirtyBufIdx;
} ZONE_WRITE_BUF_REG;

typedef struct _ZONE_WRITE_BUFFER_MAP
{
	unsigned int FIFO_LIST[MAXIMUM_OPEN_ZONE_COUNT];
	ZONE_WRITE_BUF_REG zoneWriteBufReg[MAXIMUM_OPEN_ZONE_COUNT];
	int Head;	// Head of FIFO
	int Rear;	// Tail of FIFO
	int Num;
} ZONE_WRITE_BUFFER_MAP, *P_ZONE_WRITE_BUFFER_MAP;

typedef struct _UNI_BUF_REG
{
	int curBufWriteIdx;
} UNI_BUF_REG, *P_UNI_BUF_REG;

typedef struct _WRR
{
	int buffer_count[MAXIMUM_OPEN_ZONE_COUNT];
	int total_buffer_count;

	int zoneList[MAXIMUM_OPEN_ZONE_COUNT];
	int weightList[MAXIMUM_OPEN_ZONE_COUNT];
	int listLength;
	int total_weight;
} WRR, *P_WRR;

/*-------------------------------------------------------------
		Section : NVMe ZNS Command Specification
-------------------------------------------------------------*/

/*Opcodes for ZNS IO Commands */
#define IO_ZNS_MANAGEMENT_SEND								0x79
#define IO_ZNS_MANAGEMENT_RECEIVE							0x7A
#define IO_ZNS_ZONE_APPEND									0x7D

/* Zone Management Receive action */
#define ZRA_REPORT_ZONES									0x00

/* Report Zones buffer in the reserved DRAM window (not the admin Identify buffer) */
#define ZNS_ZONE_REPORT_DRAM_ADDR							0x00300000
#define ZNS_ZONE_REPORT_DRAM_SIZE							(1024 * 1024)
#define ZNS_PRP_LIST_DRAM_ADDR								0x00400000

/* Zone Descriptor - Zone State (ZS) */
#define EMPTY												0x1
#define IMPLICITLY_OPENED									0x2
#define EXPLICITLY_OPENED									0x3
#define CLOSED												0x4
#define READ_ONLY											0xD
#define FULL												0xE
#define OFFLINE												0xF

/* ZNS Command Set 1.1 command-specific status (SCT = 1) */
#define SC_ZONED_BOUNDARY_ERROR								0xB8
#define SC_ZONE_IS_FULL										0xB9
#define SC_ZONE_IS_READ_ONLY								0xBA
#define SC_ZONE_IS_OFFLINE									0xBB
#define SC_ZONE_INVALID_WRITE								0xBC
#define SC_TOO_MANY_ACTIVE_ZONES							0xBD
#define SC_TOO_MANY_OPEN_ZONES								0xBE

/* Zone Mangaement Send Zone Send Action (ZSA) */
#define CLOSE_ZONE											0x1
#define FINISH_ZONE											0x2
#define OPEN_ZONE											0x3
#define RESET_ZONE											0x4
#define OFFLINE_ZONE										0x5
#define SET_ZONE_DESCRIPTOR_EXTENSION						0x10

/* ZNS Identify Namespace Data Structure (4096 bytes, LBAFE[0] at offset 2816) */
#pragma pack(push, 1)
typedef struct _ADMIN_IDENTIFY_ZNS_COMMAND_SET
{
	unsigned short ZOC;
	unsigned short OZCS;

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
		unsigned char reserved[7];
	} LBAFE[64];

	unsigned char vendor[256];

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



/* Byte 0: ZT in bits 3:0. Byte 1: ZS in bits 7:4 (Linux reads zone condition as zs >> 4). */
typedef struct _ZONE_DESCRIPTOR
{
	unsigned char ZT;
	unsigned char ZS;
	unsigned char ZA;
	unsigned char ZAI;
	unsigned char reserved2[4];
	unsigned long long ZCAP;
	unsigned long long ZSLBA;
	unsigned long long WP;
	unsigned char reserved3[32];
} ZONE_DESCRIPTOR;

typedef struct _IO_ZNS_MANAGEMENT_RECEIVE_ZONE_REPORT
{
	unsigned long long num_zone;
	unsigned char reserved0[56];
	ZONE_DESCRIPTOR zone_descriptor[MAXIMUM_OPEN_ZONE_COUNT];
} IO_ZNS_MANAGEMENT_RECEIVE_ZONE_REPORT;
#pragma pack(pop)

typedef char zns_identify_is_4096[(sizeof(ADMIN_IDENTIFY_ZNS_COMMAND_SET) == 4096) ? 1 : -1];
typedef char zns_lbafe_at_2816[((unsigned int)__builtin_offsetof(ADMIN_IDENTIFY_ZNS_COMMAND_SET, LBAFE) == 2816) ? 1 : -1];
typedef char zns_zone_desc_is_64[(sizeof(ZONE_DESCRIPTOR) == 64) ? 1 : -1];

#endif
