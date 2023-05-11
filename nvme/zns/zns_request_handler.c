#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

#include "../../data_buffer.h"

int ZoneWriteCheck(unsigned int slba, unsigned int nlb){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
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

unsigned int findDataBufForWrite(unsigned int zoneID){
	P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

	return AVAILABLE_DATA_BUFFER_ENTRY_COUNT + zoneID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE + zoneMapPtr->zoneReg[zoneID].Buffer_Idx;
}

unsigned int findDataBufForRead(unsigned int reqSlotTag, unsigned int zoneID){
	P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

	int level = 1 - (zoneMapPtr->zoneReg[zoneID].Buffer_Idx / SLICE_PER_STRIPE);

	unsigned int dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);

	return AVAILABLE_DATA_BUFFER_ENTRY_COUNT + level * SLICE_PER_STRIPE + dieNo;
}

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag){
	P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
    unsigned int zoneID, dataBufEntry;

	xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x\r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);

	zoneID = Lsa2ZoneId(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);
	
	if(reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE){
			
		dataBufEntry = findDataBufForWrite(zoneID);
		
		zoneMapPtr->zoneReg[zoneID].Buffer_Idx = (zoneMapPtr->zoneReg[zoneID].Buffer_Idx + 1) % DATA_BUFFER_ENTRY_COUNT_PER_ZONE;

		ZNS_EvictDataBufStripe(zoneID, reqSlotTag);

		dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

		if(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock != NVME_BLOCKS_PER_SLICE) //for read modify write
			ZNS_DataReadFromNand(reqSlotTag);

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;
	}
	else{

		dataBufEntry = findDataBufForRead(reqSlotTag, zoneID);

		if(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr != reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr){
			ZNS_DataReadFromNand(reqSlotTag);
		}

		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

	UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
	SelectLowLevelReqQ(reqSlotTag);
}

void ZNS_EvictDataBufStripe(unsigned int zoneID, unsigned int originReqSlotTag){
	P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;

	if(zoneMapPtr->zoneReg[zoneID].Buffer_Idx % SLICE_PER_STRIPE == 0){
		int level = 1 - (zoneMapPtr->zoneReg[zoneID].Buffer_Idx / SLICE_PER_STRIPE);		

		xil_printf("Evict buffer ID from : %d \r\n", AVAILABLE_DATA_BUFFER_ENTRY_COUNT + level * SLICE_PER_STRIPE);

		int i;
		for(i = 0; i < SLICE_PER_STRIPE; i++){

			dataBufEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT + level * SLICE_PER_STRIPE + i;

			if(dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY){
				reqSlotTag = GetFromFreeReqQ();
				virtualSliceAddr = Lsa2Lva(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

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
