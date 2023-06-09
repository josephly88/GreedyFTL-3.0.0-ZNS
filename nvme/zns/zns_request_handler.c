#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

#include "../../data_buffer.h"

P_ZONE_MAP zoneMapPtr;

void InitZNS()
{
    zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    zoneMapPtr->Num_Open_Zone = 0;
    zoneMapPtr->Num_Close_Zone = 0;
    zoneMapPtr->Num_Full_Zone = 0;
    zoneMapPtr->Num_Empty_Zone = MAXIMUM_ZONE_COUNT;
    zoneMapPtr->Num_Read_Zone = 0;
    zoneMapPtr->Num_Off_Zone = 0;

    int i;
    for (i = 0; i < MAXIMUM_ZONE_COUNT; i++){
        zoneMapPtr->zoneReg[i].Zone_ID = i;
        zoneMapPtr->zoneReg[i].OUTER_BLOCK_GROUP_ROW_ID = 0;
        zoneMapPtr->zoneReg[i].Zone_State = EMPTY;
        zoneMapPtr->zoneReg[i].SLBA = ZNS_LBA_START + i * NVME_BLOCKS_PER_ZONE;
        zoneMapPtr->zoneReg[i].Write_Pointer = zoneMapPtr->zoneReg[i].SLBA;
        zoneMapPtr->zoneReg[i].Buffer_Idx = 0;
    }
}

int ZoneWriteCheck(unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;

	unsigned int zoneID = Lba2ZoneId(slba);
	
	// Zone ID Check
	if(zoneID < 0 || zoneID >= MAXIMUM_ZONE_COUNT){
		xil_printf("Zone ID Error: %d\r\n", zoneID);
		return 0;
	}

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

	// Increment the write pointer
	zoneMapPtr->zoneReg[zoneID].Write_Pointer += nlb;
	if(zoneMapPtr->zoneReg[zoneID].Zone_State == EMPTY){
		zoneMapPtr->zoneReg[zoneID].Zone_State = IMPLICITLY_OPENED;
	}
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer == zoneReg.SLBA + NVME_BLOCKS_PER_ZONE - 1){
		zoneMapPtr->zoneReg[zoneID].Zone_State = FULL;
	}

    return 1;
}

int ZoneReadCheck(unsigned int slba, unsigned int nlb){
	ZONE_REG zoneReg;

	unsigned int zoneID = Lba2ZoneId(slba);
	
	// Zone ID Check
	if(zoneID < 0 || zoneID >= MAXIMUM_ZONE_COUNT){
		xil_printf("Zone ID Error: %d\r\n", zoneID);
		return 0;
	}

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
	unsigned int off_idx = (zoneMapPtr->zoneReg[zoneID].Buffer_Idx + DATA_BUFFER_ENTRY_COUNT_PER_ZONE + offset) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
	return AVAILABLE_DATA_BUFFER_ENTRY_COUNT + zoneID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE + off_idx;
}

void incrementDataBufPointer(unsigned int zoneID){
	zoneMapPtr->zoneReg[zoneID].Buffer_Idx = (zoneMapPtr->zoneReg[zoneID].Buffer_Idx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE;
}

unsigned int checkZoneDataBufStripe(unsigned int reqSlotTag, unsigned int zoneID){
	unsigned int LatestdataBufEntry = GetZoneDataBuf(zoneID, -1);
	unsigned int slice_diff = dataBufMapPtr->dataBuf[LatestdataBufEntry].logicalSliceAddr - reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

	if(slice_diff < SLICE_PER_STRIPE){
		unsigned int dataBufEntry = GetZoneDataBuf(zoneID, -(slice_diff+1));
		if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr == reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr){
			xil_printf("Slice_diff : %d\r\n", slice_diff);
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

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag){
    unsigned int zoneID, dataBufEntry;

	zoneID = Lsa2ZoneId(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);

	xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x, zone ID : %d \r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr, zoneID);
	
	if(reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE){

		dataBufEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

		/*
		dataBufEntry = GetZoneDataBuf(zoneID, 0);
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		
		incrementDataBufPointer(zoneID);

		ZNS_EvictDataBufEntry(zoneID, reqSlotTag);
		dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
		*/

		/*
		if(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock != NVME_BLOCKS_PER_SLICE) //for read modify write
			ZNS_DataReadFromNand(reqSlotTag);
		*/

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ){

		dataBufEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;

		/*
		dataBufEntry = checkZoneDataBufStripe(reqSlotTag, zoneID);
		if(dataBufEntry == DATA_BUF_FAIL){
			xil_printf("Data Buffer Entry Fail\r\n");
			return;
		}

		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		*/
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
	dataBufEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT + (zoneID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE) + ((zoneMapPtr->zoneReg[zoneID].Buffer_Idx + SLICE_PER_STRIPE) % (2*SLICE_PER_STRIPE));
	if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
	{
		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr =  dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;

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

void ZNS_DataReadFromNand(unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = Lsa2Lva(reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

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
