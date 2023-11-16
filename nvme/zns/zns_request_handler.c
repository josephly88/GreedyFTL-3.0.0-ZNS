#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

#include <stdlib.h>
#include "xtime_l.h"

P_ZONE_MAP zoneMapPtr;
P_VALID_BLOCK_GROUP_FIFO validBlockGroupFifoPtr;
P_ZONE_WRITE_BUFFER_MAP zoneWriteBufMapPtr;

// Bad block tuples (Assume the tuples are sorted and not overlapped)
// {4052, 4057} means virtual blocks 4052 - 4057 are bad blocks
int BadBlockTuples[] = {4052, 4057};

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
	
	validBlockGroupFifoPtr = (P_VALID_BLOCK_GROUP_FIFO) VALID_BLOCK_GROUP_FIFO_ADDR;
	validBlockGroupFifoPtr->Valid_Count = 0;
	eliminateBadBlockGroups();
	shuffleValidBlockGroups();
	validBlockGroupFifoPtr->Num = validBlockGroupFifoPtr->Valid_Count;
	
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
		zoneWriteBufMapPtr->zoneWriteBufReg[i].curWriteIdx = 0;
	}
	zoneWriteBufMapPtr->Head = 0;
	zoneWriteBufMapPtr->Rear = MAXIMUM_OPEN_ZONE_COUNT - 1;
	zoneWriteBufMapPtr->Num = MAXIMUM_OPEN_ZONE_COUNT;

	// Initialize Read Buffer
	for(i = 0; i < SLICE_PER_STRIPE; i++){
		zoneMapPtr->readBufPtr[i] = 0;
	}
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
	zoneWriteBufMapPtr->zoneWriteBufReg[BufferID].curWriteIdx = 0;
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
		XTime t;
		XTime_GetTime(&t);
		srand(t);
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
	int element;

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

int ZoneWriteCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	// Zone State Check
	if(zoneReg.Zone_State != IMPLICITLY_OPENED && zoneReg.Zone_State != EXPLICITLY_OPENED
	 && zoneReg.Zone_State != CLOSED && zoneReg.Zone_State != EMPTY){
		xil_printf("Zone [%d] Zone State Error: %d\r\n", zoneID, zoneReg.Zone_State);
		return -1;
	}
	
	// Sequential Write Check
	if(zoneReg.Write_Pointer != slba){
		xil_printf("Sequential Write Error: WP: %x SLBA: %x\r\n", zoneReg.Write_Pointer, slba);
		return -1;
	}

	// Out-of-Bound Check
	if(zoneReg.Write_Pointer + nlb > zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		return -1;
	}

	if(zoneMapPtr->zoneReg[zoneID].Zone_State == EMPTY){
		if(zoneMapPtr->Num_Open_Zone >= MAXIMUM_OPEN_ZONE_COUNT){
			xil_printf("Maximum Open Zone Count Reached: %d\r\n", MAXIMUM_OPEN_ZONE_COUNT);
			return -1;
		}
		zoneMapPtr->zoneReg[zoneID].Zone_State = IMPLICITLY_OPENED;
		zoneMapPtr->Num_Open_Zone++;

		int bufferID = bufferIDFifo_Dequeue();
		zoneMapPtr->zoneReg[zoneID].Buffer_ID = bufferID;
		zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].ZoneID = zoneID;
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

int ZoneReadCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	// Zone State Check
	if(zoneReg.Zone_State == EMPTY || zoneReg.Zone_State == OFFLINE){
		xil_printf("Zone State Error: %d\r\n", zoneReg.Zone_State);
		return -1;
	}

	// Out-of-Bound Check
	if(slba + nlb > zoneReg.Write_Pointer){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		return -1;
	}

	return zoneID;
}

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset){
	int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
	int curWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curWriteIdx;
	unsigned int off_idx = (curWriteIdx + offset + DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
	return ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + off_idx;
}

void incrementDataBufPointer(unsigned int zoneID){
	int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
	int curWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curWriteIdx;
	zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curWriteIdx = (curWriteIdx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE;
}

unsigned int checkZoneWriteDataBuf(unsigned int reqSlotTag, unsigned int zoneID){
	// Not in an open state
	if(zoneMapPtr->zoneReg->Buffer_ID == -1)
		return DATA_BUF_FAIL;

	unsigned int LatestdataBufEntry = GetZoneDataBuf(zoneID, -1);
	unsigned int slice_diff = dataBufMapPtr->dataBuf[LatestdataBufEntry].logicalSliceAddr - reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

	if(slice_diff < SLICE_PER_STRIPE){
		unsigned int dataBufEntry = GetZoneDataBuf(zoneID, -(1+slice_diff));
		if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr == reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr){
			//xil_printf("\tHit: Slice_diff : %d\r\n", slice_diff);
			return dataBufEntry;
		}
		else{
			return DATA_BUF_FAIL;
		}
	}
	else{
		return DATA_BUF_FAIL;
	}
}

unsigned int checkZoneReadDataBuf(unsigned int reqSlotTag){
	unsigned int bufEntry, logicalSliceAddr;

	logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
	bufEntry = znsReadBufHashTablePtr->dataBufHash[(logicalSliceAddr % ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT)].headEntry;

	while(bufEntry != DATA_BUF_NONE){
		if(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr == logicalSliceAddr){
		
			if((dataBufMapPtr->dataBuf[bufEntry].nextEntry != DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry != DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].prevEntry].nextEntry = dataBufMapPtr->dataBuf[bufEntry].nextEntry;
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].nextEntry].prevEntry = dataBufMapPtr->dataBuf[bufEntry].prevEntry;
			}
			else if((dataBufMapPtr->dataBuf[bufEntry].nextEntry == DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry != DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].prevEntry].nextEntry = DATA_BUF_NONE;
				znsReadBufLruList.tailEntry = dataBufMapPtr->dataBuf[bufEntry].prevEntry;
			}
			else if((dataBufMapPtr->dataBuf[bufEntry].nextEntry != DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry== DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].nextEntry].prevEntry  = DATA_BUF_NONE;
				znsReadBufLruList.headEntry = dataBufMapPtr->dataBuf[bufEntry].nextEntry;
			}
			else
			{
				znsReadBufLruList.tailEntry = DATA_BUF_NONE;
				znsReadBufLruList.headEntry = DATA_BUF_NONE;
			}

			if(znsReadBufLruList.headEntry != DATA_BUF_NONE)
			{
				dataBufMapPtr->dataBuf[bufEntry].prevEntry = DATA_BUF_NONE;
				dataBufMapPtr->dataBuf[bufEntry].nextEntry = znsReadBufLruList.headEntry;
				dataBufMapPtr->dataBuf[znsReadBufLruList.headEntry].prevEntry = bufEntry;
				znsReadBufLruList.headEntry = bufEntry;
			}
			else
			{
				dataBufMapPtr->dataBuf[bufEntry].prevEntry = DATA_BUF_NONE;
				dataBufMapPtr->dataBuf[bufEntry].nextEntry = DATA_BUF_NONE;
				znsReadBufLruList.headEntry = bufEntry;
				znsReadBufLruList.tailEntry = bufEntry;
			}	
			
			return bufEntry;
		}
		else
			bufEntry = dataBufMapPtr->dataBuf[bufEntry].hashNextEntry;
	}

	return DATA_BUF_FAIL;

}

unsigned int AllocateZoneDataBuf()
{
	unsigned int evictedEntry = znsReadBufLruList.tailEntry;

	if(evictedEntry == DATA_BUF_NONE)
		assert(!"[WARNING] There is no valid buffer entry [WARNING]");

	if(dataBufMapPtr->dataBuf[evictedEntry].prevEntry != DATA_BUF_NONE)
	{
		dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[evictedEntry].prevEntry].nextEntry = DATA_BUF_NONE;
		znsReadBufLruList.tailEntry = dataBufMapPtr->dataBuf[evictedEntry].prevEntry;

		dataBufMapPtr->dataBuf[evictedEntry].prevEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[evictedEntry].nextEntry = znsReadBufLruList.headEntry;
		dataBufMapPtr->dataBuf[znsReadBufLruList.headEntry].prevEntry = evictedEntry;
		znsReadBufLruList.headEntry = evictedEntry;

	}
	else
	{
		dataBufMapPtr->dataBuf[evictedEntry].prevEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[evictedEntry].nextEntry = DATA_BUF_NONE;
		znsReadBufLruList.headEntry = evictedEntry;
		znsReadBufLruList.tailEntry = evictedEntry;
	}

	SelectiveGetFromZoneDataBufHashList(evictedEntry);

	return evictedEntry;
}

void SelectiveGetFromZoneDataBufHashList(unsigned int bufEntry)
{
	if(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr != LSA_NONE)
	{
		unsigned int prevBufEntry, nextBufEntry, hashEntry;

		prevBufEntry =  dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry;
		nextBufEntry =  dataBufMapPtr->dataBuf[bufEntry].hashNextEntry;
		hashEntry = dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr % ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT;

		if((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry = nextBufEntry;
			dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry = prevBufEntry;
		}
		else if((nextBufEntry == DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry = DATA_BUF_NONE;
			znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry = prevBufEntry;
		}
		else if((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry == DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry = DATA_BUF_NONE;
			znsReadBufHashTablePtr->dataBufHash[hashEntry].headEntry = nextBufEntry;
		}
		else
		{
			znsReadBufHashTablePtr->dataBufHash[hashEntry].headEntry = DATA_BUF_NONE;
			znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry = DATA_BUF_NONE;
		}
	}
}

void PutToZoneDataBufHashList(unsigned int bufEntry)
{
	unsigned int hashEntry;

	hashEntry = dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr % ACTIVE_ZONE_READ_BUFFER_ENTRY_COUNT;

	if(znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry != DATA_BUF_NONE)
	{
		dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry ;
		dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = REQ_SLOT_TAG_NONE;
		dataBufMapPtr->dataBuf[znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry].hashNextEntry = bufEntry;
		znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry = bufEntry;
	}
	else
	{
		dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = REQ_SLOT_TAG_NONE;
		dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = REQ_SLOT_TAG_NONE;
		znsReadBufHashTablePtr->dataBufHash[hashEntry].headEntry = bufEntry;
		znsReadBufHashTablePtr->dataBufHash[hashEntry].tailEntry = bufEntry;
	}
}

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag){
    unsigned int zoneID, dataBufEntry, last_dataBufEntry;

	zoneID = Lsa2ZoneId(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);
	
	if(reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ZONE_WRITE){
		
		//xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x, zone ID : %d \r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr, zoneID);
		
		// In case write smaller than a slice
		last_dataBufEntry = GetZoneDataBuf(zoneID, -1);
		if(dataBufMapPtr->dataBuf[last_dataBufEntry].logicalSliceAddr == reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr){
			dataBufEntry = last_dataBufEntry;
		}
		else{
			dataBufEntry = GetZoneDataBuf(zoneID, 0);
		}

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		ZNS_EvictDataBufEntry(zoneID, reqSlotTag);
		if(dataBufEntry != last_dataBufEntry)
			incrementDataBufPointer(zoneID);

		dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;

		//xil_printf("Write Req. DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ZONE_READ){

		//xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x, zone ID : %d \r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr, zoneID);
		
		dataBufEntry = checkZoneWriteDataBuf(reqSlotTag, zoneID);
		if(dataBufEntry == DATA_BUF_FAIL){
			dataBufEntry = checkZoneReadDataBuf(reqSlotTag);
			if(dataBufEntry == DATA_BUF_FAIL){
				if(FLASH_BATCH_READ){

				}
				else{
					dataBufEntry = AllocateZoneDataBuf();
					
					dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
					PutToZoneDataBufHashList(dataBufEntry);

					ZNS_DataReadFromNand(zoneID, reqSlotTag);
				}
			}
		}

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		//xil_printf("Read Req. DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
	}
	else{
		assert(!"[WARNING] Not supported reqCode. [WARNING]");
	}

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

	UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
	SelectLowLevelReqQ(reqSlotTag);
}

void ZNS_EvictDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag){
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;
	int bufferID = zoneMapPtr->zoneReg[zoneID].Buffer_ID;
	int curWriteIdx = zoneWriteBufMapPtr->zoneWriteBufReg[bufferID].curWriteIdx;

	// Ping-Pong Buffer, Flash write the next row buffer if it is dirty
	dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + ((curWriteIdx + (DATA_BUFFER_STRIPE_PER_OPEN_ZONE / 2) * SLICE_PER_STRIPE) % DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE);
	if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
	{
		//xil_printf("Evict DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr = ZNS_AddrTransWrite(zoneID, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

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
	int curWriteIdx = 0;

	while(curWriteIdx < DATA_BUFFER_STRIPE_PER_OPEN_ZONE * SLICE_PER_STRIPE){
		dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (bufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + curWriteIdx;
		if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
		{
			//xil_printf("Evict DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
			reqSlotTag = GetFromFreeReqQ();
			virtualSliceAddr = ZNS_AddrTransWrite(zoneID, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

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
		
		curWriteIdx++;
	}
	
}

void ZNS_DataReadFromNand(unsigned int zoneID, unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = ZNS_AddrTransRead(zoneID, reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

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

unsigned int ZNS_AddrTransWrite(unsigned int zoneID, unsigned int lsa){
	unsigned int BLOCK_GROUP_ID, virtualSliceAddr, dieNo, innerBlockNo, outerBlockNo;
	ZNS_VirtualSliceAddr* vsaPtr;

	ZONE_REG zoneReg= zoneMapPtr->zoneReg[zoneID];

	BLOCK_GROUP_ID = zoneReg.Phy_Block_Group_ID;
	virtualSliceAddr = lsa;	
	vsaPtr = (ZNS_VirtualSliceAddr*) &virtualSliceAddr;

	dieNo = ((BLOCK_GROUP_ID % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + (lsa % NUM_OF_DIE_PER_ZONE);
	if(CHANNEL_WAY_ORIENTED == 0){
		vsaPtr->chNo = Vdie2PchTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PwayTranslation(dieNo);
	}
	else{
		vsaPtr->chNo = Vdie2PwayTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PchTranslation(dieNo);
	}	

	vsaPtr->pageNo = (lsa / NUM_OF_DIE_PER_ZONE) % (SLICES_PER_BLOCK);

	innerBlockNo = (lsa / (NUM_OF_DIE_PER_ZONE * SLICES_PER_BLOCK)) % (NUM_OF_BLOCK_PER_ZONE);
	outerBlockNo = BLOCK_GROUP_ID / BLOCK_GROUP_IN_COLUMN;

	vsaPtr->blockNo = outerBlockNo * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;

	//xil_printf("ZNS_AddrTrans: lsa: %x, zoneID: %d, BufferID: %d, blockNo: %d, pageNo: %d, wayNo: %d, chNo: %d\r\n", lsa, zoneID, zoneMapPtr->zoneReg[zoneID].Buffer_ID, vsaPtr->blockNo, vsaPtr->pageNo, vsaPtr->wayNo, vsaPtr->chNo);

	virtualBlockMapPtr->block[dieNo][vsaPtr->blockNo].currentPage++;

	return virtualSliceAddr;
}

unsigned int ZNS_AddrTransRead(unsigned int zoneID, unsigned int lsa){
	unsigned int BLOCK_GROUP_ID, virtualSliceAddr, dieNo, innerBlockNo, outerBlockNo;
	ZNS_VirtualSliceAddr* vsaPtr;

	ZONE_REG zoneReg= zoneMapPtr->zoneReg[zoneID];

	BLOCK_GROUP_ID = zoneReg.Phy_Block_Group_ID;
	virtualSliceAddr = lsa;	
	vsaPtr = (ZNS_VirtualSliceAddr*) &virtualSliceAddr;

	dieNo = ((BLOCK_GROUP_ID % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + (lsa % NUM_OF_DIE_PER_ZONE);
	if(CHANNEL_WAY_ORIENTED == 0){
		vsaPtr->chNo = Vdie2PchTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PwayTranslation(dieNo);
	}
	else{
		vsaPtr->chNo = Vdie2PwayTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PchTranslation(dieNo);
	}	

	vsaPtr->pageNo = (lsa / NUM_OF_DIE_PER_ZONE) % (SLICES_PER_BLOCK);

	innerBlockNo = (lsa / (NUM_OF_DIE_PER_ZONE * SLICES_PER_BLOCK)) % (NUM_OF_BLOCK_PER_ZONE);
	outerBlockNo = BLOCK_GROUP_ID / BLOCK_GROUP_IN_COLUMN;

	vsaPtr->blockNo = outerBlockNo * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;

	//xil_printf("ZNS_AddrTrans: lsa: %x, zoneID: %d, BufferID: %d, blockNo: %d, pageNo: %d, wayNo: %d, chNo: %d\r\n", lsa, zoneID, zoneMapPtr->zoneReg[zoneID].Buffer_ID, vsaPtr->blockNo, vsaPtr->pageNo, vsaPtr->wayNo, vsaPtr->chNo);

	return virtualSliceAddr;
}
