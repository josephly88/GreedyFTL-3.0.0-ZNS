#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

#include "../../data_buffer.h"

#include <stdlib.h>
#include "xtime_l.h"

P_ZONE_MAP zoneMapPtr;
P_VALID_BLOCK_GROUP_FIFO validBlockGroupFifoPtr;

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
	xil_printf("Address Translation\r\n");
	xil_printf("- PAGE_COLUMN_BITS: %d\r\n", PAGE_COLUMN_BITS);
	xil_printf("- PAGE_ROW_BITS: %d\r\n", PAGE_ROW_BITS);
	xil_printf("- DIE_GROUP_BITS: %d\r\n", DIE_GROUP_BITS);
	xil_printf("- INNER_ZONE_BLOCK_ROW_BITS: %d\r\n", INNER_ZONE_BLOCK_ROW_BITS);
	xil_printf("- OUTER_ZONE_BLOCK_ROW_BITS: %d\r\n", OUTER_ZONE_BLOCK_ROW_BITS);
	xil_printf("\r\n");

	
	validBlockGroupFifoPtr = (P_VALID_BLOCK_GROUP_FIFO) VALID_BLOCK_GROUP_FIFO_ADDR;
	validBlockGroupFifoPtr->Valid_Count = 0;

	eliminateBadBlockGroups();
	if(BLOCK_SHUFFLE_ENABLE)
		shuffleValidBlockGroups();		

	validBlockGroupFifoPtr->Head = 0;
	validBlockGroupFifoPtr->Rear = validBlockGroupFifoPtr->Valid_Count - 1;
	
	zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

	// Initialize ZNS Metadata
    zoneMapPtr->Num_Open_Zone = 0;
    zoneMapPtr->Num_Close_Zone = 0;
    zoneMapPtr->Num_Full_Zone = 0;
    zoneMapPtr->Num_Empty_Zone = MAXIMUM_ACTIVE_ZONE_COUNT;
    zoneMapPtr->Num_Read_Zone = 0;
    zoneMapPtr->Num_Off_Zone = 0;

	// Initialize Zone Metadata
	int i;
    for (i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++){
        zoneMapPtr->zoneReg[i].Zone_ID = i;
        zoneMapPtr->zoneReg[i].Zone_State = EMPTY;
        zoneMapPtr->zoneReg[i].SLBA = ZNS_LBA_START_NVME_BLOCK + i * NVME_BLOCKS_PER_ZONE;
        zoneMapPtr->zoneReg[i].Write_Pointer = zoneMapPtr->zoneReg[i].SLBA;
        zoneMapPtr->zoneReg[i].Buffer_Idx = 0;
		zoneMapPtr->zoneReg[i].Phy_Block_Group_ID = 0;
    }

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

void eliminateBadBlockGroups(){
	int i, badcnt, badDone;
	
	int BadBlockTuplesSize = (sizeof(BadBlockTuples)/sizeof(int));
	if(BadBlockTuplesSize % 2 == 1)
		assert(!"[Error] BadBlockTuples: The length must be even number [Error]");

	badcnt = 0;
	badDone = 0;

	// Skip the bad block tuples that are smaller than the start of the zone
	while(BadBlockTuples[badcnt+1] < ZONE_BLOCK_GROUP_START){
		badcnt += 2;
		if(badcnt >= BadBlockTuplesSize){ 	// Bad Block Tuples are exhausted
			badDone = 1;
			break;
		}
	}

	// Prepare an array of valid block groups
	for(i = ZONE_BLOCK_GROUP_START; i < BLOCK_GROUP_PER_SSD; i++){

		while(BadBlockTuples[badcnt] < i*BLOCK_PER_BLOCK_GROUP){
			badcnt++;
			if(badcnt >= BadBlockTuplesSize){ 	// Bad Block Tuples are exhausted
				badDone = 1;
				break;
			}
		}

		if(badDone == 0){
			// Start of the intevals
			if(badcnt % 2 == 0){
				if((BadBlockTuples[badcnt] >= i*BLOCK_PER_BLOCK_GROUP) && BadBlockTuples[badcnt] < ((i+1)*BLOCK_PER_BLOCK_GROUP)){
					xil_printf("Block Group - %d is skipped\r\n", i);
					if(BadBlockTuples[badcnt+1] < ((i+1)*BLOCK_PER_BLOCK_GROUP)){
						badcnt += 2;
						if(badcnt >= BadBlockTuplesSize)	// Bad Block Tuples are exhausted
							badDone = 1;
					}
					else{
						badcnt += 1;
					}
					continue;
				}
			}
			// End of the intevals
			else{
				if((BadBlockTuples[badcnt] >= i*BLOCK_PER_BLOCK_GROUP)){
					xil_printf("Block Group - %d is skipped\r\n", i);
					if(BadBlockTuples[badcnt] < ((i+1)*BLOCK_PER_BLOCK_GROUP)){
						badcnt += 1;
						if(badcnt >= BadBlockTuplesSize)	// Bad Block Tuples are exhausted
							badDone = 1;
					}
					continue;
				}
			}
		}

		validBlockGroupFifoPtr->FIFO_LIST[validBlockGroupFifoPtr->Valid_Count] = i;
		validBlockGroupFifoPtr->Valid_Count++;
	}
}

void shuffleValidBlockGroups(){
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
}

unsigned int validBlockGroupFifo_Dequeue(){
	int element = validBlockGroupFifoPtr->FIFO_LIST[validBlockGroupFifoPtr->Head];
	validBlockGroupFifoPtr->Head = (validBlockGroupFifoPtr->Head + 1) % validBlockGroupFifoPtr->Valid_Count;

	return element;
}

void validBlockGroupFifo_Enqueue(unsigned int element){
	validBlockGroupFifoPtr->Rear = (validBlockGroupFifoPtr->Rear + 1) % validBlockGroupFifoPtr->Valid_Count;
	validBlockGroupFifoPtr->FIFO_LIST[validBlockGroupFifoPtr->Rear] = element;
}

int ZoneWriteCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	// Zone State Check
	if(zoneReg.Zone_State != IMPLICITLY_OPENED && zoneReg.Zone_State != EXPLICITLY_OPENED
	 && zoneReg.Zone_State != CLOSED && zoneReg.Zone_State != EMPTY){
		xil_printf("Zone State Error: %d\r\n", zoneReg.Zone_State);
		return 0;
	}
	
	// Sequential Write Check
	if(zoneReg.Write_Pointer != slba){
		xil_printf("Sequential Write Error: WP: %x SLBA: %x\r\n", zoneReg.Write_Pointer, slba);
		return 0;
	}

	// Out-of-Bound Check
	if(zoneReg.Write_Pointer + nlb > zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		return 0;
	}

	if(zoneMapPtr->zoneReg[zoneID].Zone_State == EMPTY){
		if(zoneMapPtr->Num_Open_Zone >= MAXIMUM_OPEN_ZONE_COUNT){
			xil_printf("Maximum Open Zone Count Reached: %d\r\n", MAXIMUM_OPEN_ZONE_COUNT);
			return 0;
		}
		zoneMapPtr->zoneReg[zoneID].Zone_State = IMPLICITLY_OPENED;
		zoneMapPtr->Num_Open_Zone++;
		zoneMapPtr->Num_Empty_Zone--;

		zoneMapPtr->zoneReg[zoneID].Phy_Block_Group_ID = validBlockGroupFifo_Dequeue();
		xil_printf("Zone %d (Block Group %d) is implicitly opened\r\n", zoneID, zoneMapPtr->zoneReg[zoneID].Phy_Block_Group_ID);
	}

	// Increment the write pointer
	zoneMapPtr->zoneReg[zoneID].Write_Pointer += nlb;
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer >= zoneReg.SLBA + NVME_BLOCKS_PER_ZONE){
		zoneMapPtr->zoneReg[zoneID].Zone_State = FULL;
	}

    return 1;
}

int ZoneReadCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	// Zone State Check
	if(zoneReg.Zone_State == EMPTY || zoneReg.Zone_State == OFFLINE){
		xil_printf("Zone State Error: %d\r\n", zoneReg.Zone_State);
		return 0;
	}

	// Out-of-Bound Check
	if(slba + nlb > zoneReg.Write_Pointer){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x nlb+1 : %d\r\n", zoneReg.Write_Pointer, slba, nlb);
		return 0;
	}

	return 1;
}

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset){
	unsigned int off_idx = (zoneMapPtr->zoneReg[zoneID].Buffer_Idx + offset + DATA_BUFFER_ENTRY_COUNT_PER_ZONE) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
	return ZNS_DATA_BUFFER_ENTRY_START + (zoneID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE) + off_idx;
}

void incrementDataBufPointer(unsigned int zoneID){
	zoneMapPtr->zoneReg[zoneID].Buffer_Idx = (zoneMapPtr->zoneReg[zoneID].Buffer_Idx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
}

unsigned int checkZoneWriteDataBuf(unsigned int reqSlotTag, unsigned int zoneID){
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
		unsigned base = AVAILABLE_DATA_BUFFER_ENTRY_COUNT + MAXIMUM_OPEN_ZONE_COUNT * DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
		unsigned key = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr % SLICE_PER_STRIPE;
		int i;
		for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
			unsigned int dataBufEntry = base + i * SLICE_PER_STRIPE + key;
			if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr == reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr){
				//xil_printf("\tRead Buffer Hit: dataBufEntry : %d\r\n", dataBufEntry);
				return dataBufEntry;
			}
		}

		return DATA_BUF_FAIL;
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

		unsigned int ReadFromNand = 0;
		
		dataBufEntry = checkZoneWriteDataBuf(reqSlotTag, zoneID);
		if(dataBufEntry == DATA_BUF_FAIL){
			unsigned base = AVAILABLE_DATA_BUFFER_ENTRY_COUNT + MAXIMUM_OPEN_ZONE_COUNT * DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
			unsigned key = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr % SLICE_PER_STRIPE;
			dataBufEntry = base + zoneMapPtr->readBufPtr[key] * SLICE_PER_STRIPE + key;
			zoneMapPtr->readBufPtr[key] = (zoneMapPtr->readBufPtr[key] + 1) % MAXIMUM_OPEN_ZONE_COUNT;

			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

			ReadFromNand = 1;
			//xil_printf("\tRead Buffer Miss: Read NAND to dataBufEntry : %d\r\n", dataBufEntry);
		}

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		if(ReadFromNand == 1)
			ZNS_DataReadFromNand(zoneID, reqSlotTag);
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

	// Circular Buffer
	dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (zoneID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE) + ((zoneMapPtr->zoneReg[zoneID].Buffer_Idx + SLICE_PER_STRIPE) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE);
	if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
	{
		xil_printf("Evict DataBufEntry : %d, SliceAddr : 0x%x\r\n", dataBufEntry, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);
		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr = ZNS_AddrTrans(zoneID, dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

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

void ZNS_DataReadFromNand(unsigned int zoneID, unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = ZNS_AddrTrans(zoneID, reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

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

unsigned int ZNS_AddrTrans(unsigned int zoneID, unsigned int lsa){
	unsigned int BLOCK_GROUP_ID, vsa, dieNo, innerBlockNo, outerBlockNo;
	ZNS_VirtualSliceAddr* vsaPtr;

	ZONE_REG zoneReg;
	zoneReg = zoneMapPtr->zoneReg[zoneID];

	BLOCK_GROUP_ID = zoneReg.Phy_Block_Group_ID;
	vsa = lsa;	
	vsaPtr = (ZNS_VirtualSliceAddr*) &vsa;

	dieNo = ((BLOCK_GROUP_ID % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + (lsa % NUM_OF_DIE_PER_ZONE);
	if(CHANNEL_DIE_ORIENTED == 0){
		vsaPtr->chNo = Vdie2PchTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PwayTranslation(dieNo);
	}
	else{
		vsaPtr->chNo = Vdie2PwayTranslation(dieNo);
		vsaPtr->wayNo = Vdie2PchTranslation(dieNo);
	}
	

	vsaPtr->pageNo = (lsa / NUM_OF_DIE_PER_ZONE) % (SLICES_PER_BLOCK);

	innerBlockNo = (lsa / (NUM_OF_DIE_PER_ZONE * SLICES_PER_BLOCK)) % (BLOCK_PER_BLOCK_GROUP);
	outerBlockNo = BLOCK_GROUP_ID / BLOCK_GROUP_IN_COLUMN;

	vsaPtr->blockNo = outerBlockNo * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;

	xil_printf("ZNS_AddrTrans: lsa: %x, zoneID: %d, blockNo: %d, pageNo: %d, wayNo: %d, chNo: %d\r\n", lsa, zoneID, vsaPtr->blockNo, vsaPtr->pageNo, vsaPtr->wayNo, vsaPtr->chNo);

	return vsa;
}
