#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

#include <stdlib.h>
#include <string.h>
#include "xtime_l.h"

P_ZONE_MAP zoneMapPtr;
P_VALID_BLOCK_GROUP_FIFO validBlockGroupFifoPtr;
P_ZONE_WRITE_BUFFER_MAP zoneWriteBufMapPtr;
P_UNI_BUF_REG uniBufRegPtr;
P_WRR wrrPtr;

// Bad block tuples (Assume the tuples are sorted and not overlapped)
// {4052, 4057} means virtual blocks 4052 - 4057 are bad blocks
int BadBlockTuples[] = {4052, 4053, 4064, 4065, 4074, 4075};

void ZnsAbortNvmeIo(unsigned int cmdSlotTag, unsigned char sct, unsigned char sc, unsigned char dnr)
{
	NVME_COMPLETION cpl;

	cpl.dword[0] = 0;
	cpl.statusField.SCT = sct;
	cpl.statusField.SC = sc;
	cpl.statusField.DNR = dnr;
	cpl.specific = 0;
	set_auto_nvme_cpl(cmdSlotTag, cpl.specific, cpl.statusFieldWord);
}

void InitZNS()
{	
	parameterCheck();

	// PRINTING
	xil_printf("\r\n");
	xil_printf("ZNS Initialization\r\n");
	xil_printf("- MB_PER_ZONE: %d\r\n", MB_PER_ZONE);
	xil_printf("- BLOCK_GROUP_PER_SSD: %d\r\n", BLOCK_GROUP_PER_SSD);
	xil_printf("- ZONE_BLOCK_GROUP_START: %d\r\n", ZONE_BLOCK_GROUP_START);
	xil_printf("\r\n");

	unsigned int size = 0;
	size += RESERVED_DATA_BUFFER_BASE_ADDR + (TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR + AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT * BYTES_PER_SPARE_REGION_OF_SLICE) - DATA_BUFFER_BASE_ADDR;
	size += TEMPORARY_PAY_LOAD_ADDR + sizeof(TEMPORARY_DATA_BUF_MAP) - COMPLETE_FLAG_TABLE_ADDR;
	size += sizeof(DATA_BUF_MAP) + sizeof(GC_VICTIM_MAP) + sizeof(REQ_POOL);
	size += FTL_MANAGEMENT_END_ADDR - DIE_STATE_TABLE_ADDR;
	size += ZNS_MANAGEMENT_END_ADDR - ZNS_MANAGEMENT_START_ADDR;
	xil_printf("- ZNS_FTL_TOTAL_SIZE: %x\r\n", size);

	XTime t;
	XTime_GetTime(&t);
	srand((unsigned int)t);
	
	validBlockGroupFifoPtr = (P_VALID_BLOCK_GROUP_FIFO) VALID_BLOCK_GROUP_FIFO_ADDR;
	validBlockGroupFifoPtr->Valid_Count = 0;
	eliminateBadBlockGroups();
	shuffleValidBlockGroups();
	validBlockGroupFifoPtr->Num = validBlockGroupFifoPtr->Valid_Count;

	if(BUFFER_MODE == 0 && NON_SHARE_MOD == 1){
		wrrPtr = (P_WRR) WRR_ADDR;
		wrr_init();
	}

	if(BUFFER_MODE == 1){
		uniBufRegPtr = (P_UNI_BUF_REG) UNI_BUF_REG_ADDR;
		uniBufRegPtr->curBufWriteIdx = -1;
	}
	
	zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

	// Initialize ZNS Metadata
    zoneMapPtr->Num_Open_Zone = 0;

	// Initialize Zone Metadata
	int i;
    for (i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++){
        resetZoneReg(i);
    }

	zoneWriteBufMapPtr = (P_ZONE_WRITE_BUFFER_MAP) ZONE_WRITE_BUFFER_MAP_ADDR;
	for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
		zoneWriteBufMapPtr->FIFO_LIST[i] = i;
		zoneWriteBufMapPtr->zoneWriteBufReg[i].ZoneID = -1;
		zoneWriteBufMapPtr->zoneWriteBufReg[i].curBufWriteIdx = -1;
		zoneWriteBufMapPtr->zoneWriteBufReg[i].dirtyBufIdx = -1;
	}
	zoneWriteBufMapPtr->Head = 0;
	zoneWriteBufMapPtr->Rear = MAXIMUM_OPEN_ZONE_COUNT - 1;
	zoneWriteBufMapPtr->Num = MAXIMUM_OPEN_ZONE_COUNT;
}

void parameterCheck(){
	if(NUM_OF_BLOCK_PER_ZONE < 0 || NUM_OF_BLOCK_PER_ZONE > 8192 || 8192 % NUM_OF_BLOCK_PER_ZONE != 0)
		assert(!"[Error] NUM_OF_BLOCK_PER_ZONE must be a factor of 8192 [Error]");

	if(NUM_OF_DIE_PER_ZONE < 0 || NUM_OF_DIE_PER_ZONE > 64 || 64 % NUM_OF_DIE_PER_ZONE != 0)
		assert(!"[Error] NUM_OF_DIE_PER_ZONE must be a factor of 64 [Error]");
}

void resetZoneReg(int ZoneID){
	zoneMapPtr->zoneReg[ZoneID].Zone_State = EMPTY;
	zoneMapPtr->zoneReg[ZoneID].SLBA = ZNS_LBA_START_NVME_BLOCK + ZoneID * NVME_BLOCKS_PER_ZONE;
	zoneMapPtr->zoneReg[ZoneID].Write_Pointer = zoneMapPtr->zoneReg[ZoneID].SLBA;
	zoneMapPtr->zoneReg[ZoneID].Buffer_ID = -1;
	zoneMapPtr->zoneReg[ZoneID].Phy_Block_Group_ID = -1;
}

void resetWriteBufferReg(int BufferID){
	zoneWriteBufMapPtr->zoneWriteBufReg[BufferID].ZoneID = -1;
	zoneWriteBufMapPtr->zoneWriteBufReg[BufferID].curBufWriteIdx = -1;
	zoneWriteBufMapPtr->zoneWriteBufReg[BufferID].dirtyBufIdx = -1;
}

void eliminateBadBlockGroups(){
	int bkidx, baditv;

	for(bkidx = 0; bkidx < BLOCK_GROUP_PER_SSD; bkidx++){
		if(bkidx < ZONE_BLOCK_GROUP_START)
			validBlockGroupFifoPtr->FIFO_LIST[bkidx] = -1;
		else{
			validBlockGroupFifoPtr->FIFO_LIST[bkidx] = bkidx;
			validBlockGroupFifoPtr->Valid_Count++;
		}
	}

	int BadBlockTuplesSize = (sizeof(BadBlockTuples)/sizeof(int));
	if(BadBlockTuplesSize % 2 == 1)
		assert(!"[Error] BadBlockTuples: The length must be even number [Error]");

	for(baditv = 0; baditv < BadBlockTuplesSize; baditv += 2){
		int badblk;
		for(badblk = BadBlockTuples[baditv]; badblk <= BadBlockTuples[baditv+1]; badblk++){
			// Skip the bad block tuples that are smaller than the start of the zone
			if(badblk < ZONE_BLOCK_GROUP_START)
				continue;
			// Calculate the block group that bad block belongs to
			int BG_row = (badblk / 64) / NUM_OF_BLOCK_PER_ZONE;
			int BG_column = (badblk % 64) / NUM_OF_DIE_PER_ZONE;
			int BG = BG_row * (64/NUM_OF_DIE_PER_ZONE) + BG_column;

			validBlockGroupFifoPtr->FIFO_LIST[BG] = -1;
			//xil_printf("Bad Block %d is found in Block Group %d\r\n", badblk, BG);
		}
	}

	int valididx = 0;
	for(bkidx = 0; bkidx < BLOCK_GROUP_PER_SSD; bkidx++){
		if(validBlockGroupFifoPtr->FIFO_LIST[bkidx] != -1){
			validBlockGroupFifoPtr->FIFO_LIST[valididx] = validBlockGroupFifoPtr->FIFO_LIST[bkidx];
			valididx++;
		}
		else{
			if(bkidx >= ZONE_BLOCK_GROUP_START)
				xil_printf("Block Group %d is skipped\r\n", bkidx);
		}
	}
	validBlockGroupFifoPtr->Valid_Count = valididx;
}

void shuffleValidBlockGroups(){
	if(BLOCK_SHUFFLE_ENABLE){
		// Shuffle the validBlockShuffleList
		int i;
		for(i = validBlockGroupFifoPtr->Valid_Count-1; i > 0; i--){
			int r = rand() % (i+1);
			int swap = validBlockGroupFifoPtr->FIFO_LIST[r];
			validBlockGroupFifoPtr->FIFO_LIST[r] = validBlockGroupFifoPtr->FIFO_LIST[i];
			validBlockGroupFifoPtr->FIFO_LIST[i] = swap;
		}

		validBlockGroupFifoPtr->Head = 0;
		validBlockGroupFifoPtr->Rear = validBlockGroupFifoPtr->Valid_Count - 1;
	}
	else{
		XTime t;
		XTime_GetTime(&t);
		srand(t);
		int r = rand() % validBlockGroupFifoPtr->Valid_Count;

		validBlockGroupFifoPtr->Head = r;
		validBlockGroupFifoPtr->Rear = (r + validBlockGroupFifoPtr->Valid_Count - 1) % validBlockGroupFifoPtr->Valid_Count;
	}

}

unsigned int validBlockGroupFifo_Dequeue(){
	if(validBlockGroupFifoPtr->Num == 0)
		assert(!"[Error] validBlockGroupFifoPtr is empty [Error]");

	int element = validBlockGroupFifoPtr->FIFO_LIST[validBlockGroupFifoPtr->Head];
	if(validBlockGroupFifoPtr->Num > 1)
		validBlockGroupFifoPtr->Head = (validBlockGroupFifoPtr->Head + 1) % validBlockGroupFifoPtr->Valid_Count;

	validBlockGroupFifoPtr->Num--;

	return element;
}

void validBlockGroupFifo_Enqueue(unsigned int element){
	if(validBlockGroupFifoPtr->Num >= validBlockGroupFifoPtr->Valid_Count)
		assert(!"[Error] validBlockGroupFifoPtr is full [Error]");

	if(validBlockGroupFifoPtr->Num > 0)
		validBlockGroupFifoPtr->Rear = (validBlockGroupFifoPtr->Rear + 1) % validBlockGroupFifoPtr->Valid_Count;
	validBlockGroupFifoPtr->FIFO_LIST[validBlockGroupFifoPtr->Rear] = element;

	validBlockGroupFifoPtr->Num++;
}

unsigned int bufferIDFifo_Dequeue(){
	int element = -1;

	if(zoneWriteBufMapPtr->Num == 0){
		int zone_idx;
		for(zone_idx = 0; zone_idx < MAXIMUM_ACTIVE_ZONE_COUNT; zone_idx++){
			if(zoneMapPtr->zoneReg[zone_idx].Zone_State == FULL && zoneMapPtr->zoneReg[zone_idx].Buffer_ID != -1){
				element = zoneMapPtr->zoneReg[zone_idx].Buffer_ID;

				unsigned int reqSlotTag = GetFromSliceReqQ();
				ZNS_EvictAllDataBufEntry(zone_idx, reqSlotTag);
				zoneMapPtr->zoneReg[zone_idx].Buffer_ID = -1;

				break;
			}		
		}
	}
	else{
		element = zoneWriteBufMapPtr->FIFO_LIST[zoneWriteBufMapPtr->Head];
		if(zoneWriteBufMapPtr->Num > 1)
			zoneWriteBufMapPtr->Head = (zoneWriteBufMapPtr->Head + 1) % MAXIMUM_OPEN_ZONE_COUNT;

		zoneWriteBufMapPtr->Num--;
	}

	return element;
}

void bufferIDFifo_Enqueue(unsigned int element){
	if(zoneWriteBufMapPtr->Num >= MAXIMUM_OPEN_ZONE_COUNT)
		assert(!"[Error] zoneWriteBufMapPtr is full [Error]");

	if(zoneWriteBufMapPtr->Num > 0)
		zoneWriteBufMapPtr->Rear = (zoneWriteBufMapPtr->Rear + 1) % MAXIMUM_OPEN_ZONE_COUNT;
	zoneWriteBufMapPtr->FIFO_LIST[zoneWriteBufMapPtr->Rear] = element;

	zoneWriteBufMapPtr->Num++;
}

void wrr_init() {
	
    int i;
    for (i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++) {
		wrrPtr->buffer_count[i] = 0;
		wrrPtr->zoneList[i] = -1;
		wrrPtr->weightList[i] = -1;
    }	
	wrrPtr->total_buffer_count = 0;
	wrrPtr->listLength = 0;
	wrrPtr->total_weight = 0;	
}

int wrr_get_zone_weight(int zoneID) {
    if (zoneID >= 0 && zoneID <= 5) return 1;
    else if (zoneID >= 6 && zoneID <= 9) return 2;
    else if (zoneID >= 10 && zoneID <= 13) return 4;
    else if (zoneID >= 14 && zoneID <= 29) return 16;
    else return 0;
}

void wrr_add_zone(int zoneID){
	int w = wrr_get_zone_weight(zoneID);

	int i;
	for(i = 0; i < wrrPtr->listLength; i++){
		if(wrrPtr->zoneList[i] == zoneID) return;
	}

	wrrPtr->zoneList[wrrPtr->listLength] = zoneID;
	wrrPtr->weightList[wrrPtr->listLength] = w;
	wrrPtr->listLength++;
	wrrPtr->total_weight += w;
}

void wrr_remove_zone(int zoneID){
	int i;
	for (i = 0; i < wrrPtr->listLength; i++) {
        if (wrrPtr->zoneList[i] == zoneID) {
            wrrPtr->total_weight -= wrrPtr->weightList[i];

            // Shift remaining entries left
			int j;
            for (j = i; j < wrrPtr->listLength - 1; j++) {
                wrrPtr->zoneList[j] = wrrPtr->zoneList[j+1];
                wrrPtr->weightList[j] = wrrPtr->weightList[j+1];
            }

            wrrPtr->zoneList[wrrPtr->listLength - 1] = -1;
            wrrPtr->weightList[wrrPtr->listLength - 1] = -1;
            wrrPtr->listLength--;
            return;
        }
    }
}

int wrr_select_zone_probability() {
    if (wrrPtr->total_weight <= 0 || wrrPtr->listLength == 0) {
        return -1; // no active zones
    }

    int x = rand() % wrrPtr->total_weight;  // [0, total_weight-1]
    int sum = 0;
	int i;
    for (i = 0; i < wrrPtr->listLength; i++) {
        sum += wrrPtr->weightList[i];
        if (sum > x) {
            return wrrPtr->zoneList[i];
        }
    }

    return -1; // defensive fallback
}

int weighted_buffer_size(int zoneID) {
	/*
	// Zipfian
	if (zoneID >= 0 && zoneID <= 1) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/16;
    else if (zoneID >= 2 && zoneID <= 5) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/8;
    else if (zoneID >= 6 && zoneID <= 13) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/4;
    else if (zoneID >= 14 && zoneID <= 29) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/2;
    else if (zoneID >= 30 && zoneID <= 61) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
    else return 0;
	*/

	/*
	//Pareto
	if (zoneID >= 0 && zoneID <= 5) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/16;
    else if (zoneID >= 6 && zoneID <= 9) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/8;
    else if (zoneID >= 10 && zoneID <= 13) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE/4;
    else if (zoneID >= 14 && zoneID <= 29) return DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
    else return 0;
	*/

	//Fair_Mix_1
	
    
}

int ZoneWriteCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb,
	unsigned char *sct, unsigned char *sc, unsigned char *dnr){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	if(zoneReg.Zone_State == FULL){
		xil_printf("Zone [%d] Zone State Error: %d\r\n", zoneID, zoneReg.Zone_State);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONE_IS_FULL;
		*dnr = 1;
		return -1;
	}
	if(zoneReg.Zone_State == READ_ONLY){
		xil_printf("Zone [%d] Zone State Error: %d\r\n", zoneID, zoneReg.Zone_State);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONE_IS_READ_ONLY;
		*dnr = 1;
		return -1;
	}
	if(zoneReg.Zone_State == OFFLINE){
		xil_printf("Zone [%d] Zone State Error: %d\r\n", zoneID, zoneReg.Zone_State);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONE_IS_OFFLINE;
		*dnr = 1;
		return -1;
	}
	if(zoneReg.Zone_State != IMPLICITLY_OPENED && zoneReg.Zone_State != EXPLICITLY_OPENED
	 && zoneReg.Zone_State != CLOSED && zoneReg.Zone_State != EMPTY){
		xil_printf("Zone [%d] Zone State Error: %d\r\n", zoneID, zoneReg.Zone_State);
		*sct = SCT_GENERIC_COMMAND_STATUS;
		*sc = SC_INVALID_FIELD_IN_COMMAND;
		*dnr = 1;
		return -1;
	}

	if(zoneReg.Write_Pointer != slba){
		xil_printf("Sequential Write Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONE_INVALID_WRITE;
		*dnr = 1;
		return -1;
	}

	if(zoneReg.Write_Pointer + nlb > zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONED_BOUNDARY_ERROR;
		*dnr = 1;
		return -1;
	}
	
	if(zoneMapPtr->zoneReg[zoneID].Zone_State == EMPTY){
		if(zoneMapPtr->Num_Open_Zone >= MAXIMUM_OPEN_ZONE_COUNT){
			xil_printf("Maximum Open Zone Count Reached: %d\r\n", MAXIMUM_OPEN_ZONE_COUNT);
			*sct = SCT_COMMAND_SPECIFIC_STATUS;
			*sc = SC_TOO_MANY_ACTIVE_ZONES;
			*dnr = 0;
			return -1;
		}
		zoneMapPtr->zoneReg[zoneID].Zone_State = IMPLICITLY_OPENED;
		zoneMapPtr->Num_Open_Zone++;

		int bufferID;
		
		if(BUFFER_MODE == 0){
			bufferID = bufferIDFifo_Dequeue();			
			zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].ZoneID = zoneID;
		}
		else if(BUFFER_MODE == 1){
			bufferID = 0;
		}		
		else{
			assert(!"[Error] Invalid Buffer Mode [Error]");
		}

		zoneMapPtr->zoneReg[zoneID].Buffer_ID = bufferID;
		zoneMapPtr->zoneReg[zoneID].Phy_Block_Group_ID = validBlockGroupFifo_Dequeue();
		//xil_printf("Zone %d (Block Group %d) is implicitly opened\r\n", zoneMapPtr->zoneReg[zoneRegID].Zone_ID, zoneMapPtr->zoneReg[zoneRegID].Phy_Block_Group_ID);
	}

	// Increment the write pointer
	zoneMapPtr->zoneReg[zoneID].Write_Pointer += nlb;
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer >= zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		zoneMapPtr->zoneReg[zoneID].Zone_State = FULL;
		zoneMapPtr->Num_Open_Zone--;
	}

    return zoneMapPtr->zoneReg[zoneID].Buffer_ID;
}

int ZoneReadCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb,
	unsigned char *sct, unsigned char *sc, unsigned char *dnr){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	if(zoneReg.Zone_State == OFFLINE){
		xil_printf("Zone State Error: %d\r\n", zoneReg.Zone_State);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONE_IS_OFFLINE;
		*dnr = 1;
		return -1;
	}

	if(slba + nlb > zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		*sct = SCT_COMMAND_SPECIFIC_STATUS;
		*sc = SC_ZONED_BOUNDARY_ERROR;
		*dnr = 1;
		return -1;
	}

	return zoneID;
}

static int WrapBufIdx(int idx, int ring)
{
    idx %= ring;
    if(idx < 0)
        idx += ring;
    return idx;
}

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset){
	if(BUFFER_MODE == 0){
		int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
		int curBufWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curBufWriteIdx;
		int off_idx = WrapBufIdx(curBufWriteIdx + offset, DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE);
		return ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + off_idx;
	}
	else if(BUFFER_MODE == 1){
		int off_idx = WrapBufIdx(uniBufRegPtr->curBufWriteIdx + offset, OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT);
		return ZNS_DATA_BUFFER_ENTRY_START + off_idx;
	}
	else{
		assert(!"[Error] Invalid Buffer Mode [Error]");
		return 0;
	}
}

void incrementDataBufPointer(unsigned int zoneID){
	if(BUFFER_MODE == 0){
		int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
		int curBufWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curBufWriteIdx;
		if(NON_SHARE_MOD == 1 && curBufWriteIdx == -1)
			zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].dirtyBufIdx = 0;
		if(NON_SHARE_MOD == 2){
			zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curBufWriteIdx = (curBufWriteIdx + 1) % weighted_buffer_size(zoneID);
			return;
		}			
		zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curBufWriteIdx = (curBufWriteIdx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
	}
	else if(BUFFER_MODE == 1){
		int curBufWriteIdx = uniBufRegPtr->curBufWriteIdx;
		uniBufRegPtr->curBufWriteIdx = (curBufWriteIdx + 1) % OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT;
	}
	else{
		assert(!"[Error] Invalid Buffer Mode [Error]");
	}
}

static unsigned int LatestZoneWriteBuf(unsigned int zoneID)
{
	int curBufWriteIdx;
	unsigned int latestEntry;

	if(zoneMapPtr->zoneReg[zoneID].Buffer_ID == -1)
		return DATA_BUF_FAIL;

	if(BUFFER_MODE == 0)
		curBufWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[zoneMapPtr->zoneReg[zoneID].Buffer_ID].curBufWriteIdx;
	else if(BUFFER_MODE == 1)
		curBufWriteIdx = uniBufRegPtr->curBufWriteIdx;
	else
	{
		assert(!"[Error] Invalid Buffer Mode [Error]");
		return DATA_BUF_FAIL;
	}

	if(curBufWriteIdx == -1)
		return DATA_BUF_FAIL;

	latestEntry = GetZoneDataBuf(zoneID, 0);
	if(dataBufMapPtr->dataBuf[latestEntry].logicalSliceAddr == LSA_NONE)
		return DATA_BUF_FAIL;

	return latestEntry;
}

unsigned int CheckPartialSliceWriteBuf(unsigned int reqSlotTag, unsigned int zoneID){
	unsigned int latestEntry, reqLsa;

	if(zoneMapPtr->zoneReg[zoneID].Buffer_ID == -1)
		return DATA_BUF_FAIL;

	if(NON_SHARE_MOD == 1){
		unsigned int slice_diff, dataBufEntry;

		latestEntry = GetZoneDataBuf(zoneID, 0);
		slice_diff = dataBufMapPtr->dataBuf[latestEntry].logicalSliceAddr - reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

		if(slice_diff < -EVICTION_OFFSET){
			dataBufEntry = GetZoneDataBuf(zoneID, -(int)slice_diff);
			if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr == reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr)
				return dataBufEntry;
		}
		return DATA_BUF_FAIL;
	}

	latestEntry = LatestZoneWriteBuf(zoneID);
	if(latestEntry == DATA_BUF_FAIL)
		return DATA_BUF_FAIL;

	reqLsa = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
	if(dataBufMapPtr->dataBuf[latestEntry].logicalSliceAddr == reqLsa)
		return latestEntry;

	return DATA_BUF_FAIL;
}

unsigned int CheckUnevictedSliceBuf(unsigned int reqSlotTag, unsigned int zoneID){
	unsigned int latestEntry, latestLsa, reqLsa, dataBufEntry;
	int d;

	latestEntry = LatestZoneWriteBuf(zoneID);
	if(latestEntry == DATA_BUF_FAIL)
		return DATA_BUF_FAIL;

	latestLsa = dataBufMapPtr->dataBuf[latestEntry].logicalSliceAddr;
	reqLsa = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
	d = (int)latestLsa - (int)reqLsa;
	if(d < 0 || d >= -EVICTION_OFFSET)
		return DATA_BUF_FAIL;

	dataBufEntry = GetZoneDataBuf(zoneID, -d);
	if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr == reqLsa)
		return dataBufEntry;

	return DATA_BUF_FAIL;
}

static unsigned int znsReadBufIdx;

static unsigned int AllocateZnsReadRingBuf(void)
{
	unsigned int base = AVAILABLE_DATA_BUFFER_ENTRY_COUNT + OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT;
	unsigned int i;

	for(i = 0; i < ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT; i++)
	{
		unsigned int dataBufEntry = base + znsReadBufIdx;

		znsReadBufIdx = (znsReadBufIdx + 1) % ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT;
		if(dataBufMapPtr->dataBuf[dataBufEntry].blockingReqTail == REQ_SLOT_TAG_NONE)
			return dataBufEntry;
	}

	return DATA_BUF_FAIL;
}

static int ZnsWriteSliceIncomplete(unsigned int zoneID, unsigned int logicalSliceAddr)
{
	unsigned int wp, slba, zoneEnd, wpLsa;

	if(zoneID >= MAXIMUM_ACTIVE_ZONE_COUNT)
		return 0;
	if(logicalSliceAddr == LSA_NONE)
		return 0;

	wp = zoneMapPtr->zoneReg[zoneID].Write_Pointer;
	slba = zoneMapPtr->zoneReg[zoneID].SLBA;
	zoneEnd = slba + NVME_BLOCKS_PER_ZONE;
	if(wp >= zoneEnd)
		return 0;

	wpLsa = (zoneID * SLICE_PER_ZONE) + ((wp / NVME_BLOCKS_PER_SLICE) % SLICE_PER_ZONE);
	return (logicalSliceAddr == wpLsa);
}

static void ZnsAbortSliceReq(unsigned int reqSlotTag, unsigned char sct, unsigned char sc, unsigned char dnr)
{
	ZnsAbortNvmeIo(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, sct, sc, dnr);
	PutToFreeReqQ(reqSlotTag);
}

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag){
    unsigned int zoneID, dataBufEntry;

	zoneID = Lsa2ZoneId(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);

	//transform this slice request to nvme request
	if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_ZONE_WRITE)
	{
		unsigned int nvmeBlockOffset = reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset;
		unsigned int numOfNvmeBlock = reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock;
		unsigned int bufHit;

		if((nvmeBlockOffset + numOfNvmeBlock) > NVME_BLOCKS_PER_SLICE){
			ZnsAbortSliceReq(reqSlotTag, SCT_GENERIC_COMMAND_STATUS, SC_INTERNAL_DEVICE_ERROR, 1);
			return;
		}

		if(zoneMapPtr->zoneReg[zoneID].Buffer_ID == -1){
			ZnsAbortSliceReq(reqSlotTag, SCT_GENERIC_COMMAND_STATUS, SC_INTERNAL_DEVICE_ERROR, 1);
			return;
		}

		dataBufEntry = CheckPartialSliceWriteBuf(reqSlotTag, zoneID);
		bufHit = (dataBufEntry != DATA_BUF_FAIL);

		if(!bufHit){
			dataBufEntry = ZNS_AllocateWriteDataBuf(zoneID, reqSlotTag);
			if(dataBufEntry == DATA_BUF_FAIL){
				ZnsAbortSliceReq(reqSlotTag, SCT_GENERIC_COMMAND_STATUS, SC_INTERNAL_DEVICE_ERROR, 1);
				return;
			}

			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
			ZNS_EvictDataBufEntry(zoneID, reqSlotTag);
			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
		}
		else{
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		}

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;

		if(!bufHit && BUFFER_MODE == 0 && NON_SHARE_MOD == 1){
			if(wrrPtr->buffer_count[zoneID] == 0)
				wrr_add_zone(zoneID);	
			wrrPtr->total_buffer_count += 1;
			wrrPtr->buffer_count[zoneID] += 1;		
		}
	}
	else if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_ZONE_READ)
	{
		dataBufEntry = CheckUnevictedSliceBuf(reqSlotTag, zoneID);
		if(dataBufEntry != DATA_BUF_FAIL)
		{
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		}
		else
		{
			unsigned int virtualSliceAddr;

			dataBufEntry = AllocateZnsReadRingBuf();
			if(dataBufEntry == DATA_BUF_FAIL)
			{
				ZnsAbortSliceReq(reqSlotTag, SCT_GENERIC_COMMAND_STATUS, SC_INTERNAL_DEVICE_ERROR, 1);
				return;
			}

			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

			virtualSliceAddr = ZNS_AddrTransRead(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);
			if(virtualSliceAddr != VSA_FAIL)
				ZNS_DataReadFromNand(zoneID, reqSlotTag);
			else
				memset((void *)(DATA_BUFFER_BASE_ADDR + dataBufEntry * BYTES_PER_DATA_REGION_OF_SLICE), 0, BYTES_PER_DATA_REGION_OF_SLICE);

			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		}
	}
	else
		assert(!"[WARNING] Not supported reqCode. [WARNING]");

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

	UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
	SelectLowLevelReqQ(reqSlotTag);
}

unsigned int ZNS_AllocateWriteDataBuf(unsigned int zoneID, unsigned int reqSlotTag){

    unsigned int dataBufEntry;

	//xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x, zone ID : %d \r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr, zoneID);
	
	// In case write smaller than a slice which can fit the last buffer
	if(BUFFER_MODE == 0){
		incrementDataBufPointer(zoneID);
		dataBufEntry = GetZoneDataBuf(zoneID, 0);
	}
	else if(BUFFER_MODE == 1){
		incrementDataBufPointer(zoneID);
		dataBufEntry = GetZoneDataBuf(zoneID, 0);
	}
	else{
		assert(!"[Error] Invalid Buffer Mode [Error]");
	}	

	return dataBufEntry;
}

void ZNS_EvictDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag){
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;

	if(BUFFER_MODE == 0){		
		if(NON_SHARE_MOD == 1){

			if(wrrPtr->total_buffer_count >= EVICTION_THRESHOLD){
				unsigned int evict_zoneID = wrr_select_zone_probability();
				if(evict_zoneID == -1)
					assert(!"[ERROR] No valid zone selected in probability balancer [ERROR]");
				
				int bufferID = zoneMapPtr->zoneReg[evict_zoneID].Buffer_ID;
				int dirtyIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].dirtyBufIdx;
				dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + dirtyIdx;

				if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY
					&& ZnsWriteSliceIncomplete(evict_zoneID, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr))
					return;
				
				zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].dirtyBufIdx = (dirtyIdx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
				wrrPtr->buffer_count[evict_zoneID] -= 1;
				wrrPtr->total_buffer_count -= 1;	

				/*
				xil_printf("[DEBUG] evict_zone=%d weight=%d bufferID=%d dirtyIdx=%d bufCount=%d totalBuf=%d totalWeight=%d listLen=%d\r\n",
					evict_zoneID,
					wrr_get_zone_weight(evict_zoneID),
					bufferID,
					dirtyIdx,
					wrrPtr->buffer_count[evict_zoneID],
					wrrPtr->total_buffer_count,
					wrrPtr->total_weight,
					wrrPtr->listLength);
				*/
					

				if(wrrPtr->buffer_count[evict_zoneID] == 0){
					wrr_remove_zone(evict_zoneID);
				}
			}
			else
				return;
		}
		else{
			int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
			int curBufWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curBufWriteIdx;

			// Ping-Pong Buffer, Flash write the next row buffer if it is dirty
			// Evict next (N/2)
			int evictIdx = (curBufWriteIdx + (EVICTION_OFFSET + DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE)) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;			

			if(NON_SHARE_MOD == 2){
				evictIdx = (curBufWriteIdx + (EVICTION_OFFSET + weighted_buffer_size(zoneID))) % weighted_buffer_size(zoneID);
			}		

			dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + evictIdx;
		}

	}
	else if(BUFFER_MODE == 1){
		int curBufWriteIdx = uniBufRegPtr->curBufWriteIdx;
		int evictIdx = (curBufWriteIdx + (EVICTION_OFFSET + OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT)) % OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT;
		dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + evictIdx;
	}
	else{
		assert(!"[Error] Invalid Buffer Mode [Error]");
	}		

	if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
	{
		unsigned int entryLsa = dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
		unsigned int entryZone = Lsa2ZoneId(entryLsa);

		if(ZnsWriteSliceIncomplete(entryZone, entryLsa))
			return;

		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr = ZNS_AddrTransWrite(entryLsa);

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
	}
}

void ZNS_EvictAllDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag){
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;
	int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
	int bufEntryIdx = 0;

	while(bufEntryIdx < DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE){
		dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + bufEntryIdx;
		if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
		{
			//xil_printf("Evict DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
			reqSlotTag = GetFromFreeReqQ();
			virtualSliceAddr = ZNS_AddrTransWrite(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

			reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
			reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
			reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
			reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;

			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
			reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
			reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
			UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
			reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

			SelectLowLevelReqQ(reqSlotTag);

			dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
		}
		
		bufEntryIdx++;
	}

	if(BUFFER_MODE == 0 && NON_SHARE_MOD){
		int cnt = wrrPtr->buffer_count[zoneID];
		wrrPtr->buffer_count[zoneID] = 0;
		wrrPtr->total_buffer_count -= cnt;
	}
	
}

void ZNS_DataReadFromNand(unsigned int zoneID, unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = ZNS_AddrTransRead(reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

	if(virtualSliceAddr != VSA_FAIL)
	{
		reqSlotTag = GetFromFreeReqQ();

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
		UpdateDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);
	}
}

static unsigned int ZnsLogicalSliceToVsa(unsigned int logicalSliceAddr)
{
	unsigned int blockGroupId, dieNo, innerBlockNo, outerBlockNo, blockNo, pageNo, zoneID;
	int sliceID;

	zoneID = Lsa2ZoneId(logicalSliceAddr);
	blockGroupId = zoneMapPtr->zoneReg[zoneID].Phy_Block_Group_ID;
	sliceID = logicalSliceAddr - (zoneID * SLICE_PER_ZONE);

	dieNo = ((blockGroupId % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + (sliceID % NUM_OF_DIE_PER_ZONE);
	if(CHANNEL_WAY_ORIENTED == 1)
		dieNo = ((dieNo % 8) * 8) + (dieNo / 8);

	pageNo = (sliceID / NUM_OF_DIE_PER_ZONE) % (SLICES_PER_BLOCK);
	innerBlockNo = (sliceID / (NUM_OF_DIE_PER_ZONE * SLICES_PER_BLOCK)) % NUM_OF_BLOCK_PER_ZONE;
	outerBlockNo = blockGroupId / BLOCK_GROUP_IN_COLUMN;
	blockNo = outerBlockNo * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;

	return Vorg2VsaTranslation(dieNo, blockNo, pageNo);
}

unsigned int ZNS_AddrTransWrite(unsigned int logicalSliceAddr){
	unsigned int virtualSliceAddr, dieNo, blockNo;

	virtualSliceAddr = ZnsLogicalSliceToVsa(logicalSliceAddr);
	dieNo = Vsa2VdieTranslation(virtualSliceAddr);
	blockNo = Vsa2VblockTranslation(virtualSliceAddr);
	virtualBlockMapPtr->block[dieNo][blockNo].currentPage++;

	logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr = virtualSliceAddr;
	virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr = logicalSliceAddr;

	return virtualSliceAddr;
}

unsigned int ZNS_AddrTransRead(unsigned int logicalSliceAddr){
	unsigned int zoneID, wp, slba, wpLsa;

	if(logicalSliceAddr >= SLICES_PER_SSD)
	{
		assert(!"[WARNING] Logical address is larger than maximum logical address served by SSD [WARNING]");
		return VSA_FAIL;
	}

	zoneID = Lsa2ZoneId(logicalSliceAddr);
	if(zoneID >= MAXIMUM_ACTIVE_ZONE_COUNT)
		return VSA_FAIL;

	if(zoneMapPtr->zoneReg[zoneID].Phy_Block_Group_ID < 0)
		return VSA_FAIL;

	wp = zoneMapPtr->zoneReg[zoneID].Write_Pointer;
	slba = zoneMapPtr->zoneReg[zoneID].SLBA;
	if(wp < slba + NVME_BLOCKS_PER_ZONE)
	{
		wpLsa = (zoneID * SLICE_PER_ZONE) + ((wp / NVME_BLOCKS_PER_SLICE) % SLICE_PER_ZONE);
		if(logicalSliceAddr >= wpLsa)
			return VSA_FAIL;
	}

	return ZnsLogicalSliceToVsa(logicalSliceAddr);
}
