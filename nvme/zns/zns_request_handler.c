#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

int ZoneWriteCheck(unsigned int slba, unsigned int numOfSlice){

    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    ZONE_ID_EXTRACTOR_SLBA zoneIDExtr;
	zoneIDExtr.dword = slba;
	unsigned int zoneID = zoneIDExtr.ZONE_ID;
	
	// Zone ID Check
	if(zoneID < 0 || zoneID >= MAXIMUM_ZONE_COUNT){
		xil_printf("Zone ID Error: %d\r\n", zoneID);
		return 0;
	}

	// Zone State Check
	unsigned char zoneState = zoneMapPtr->zoneReg[zoneID].Zone_State;
	if(zoneState != IMPLICITLY_OPENED && zoneState != EXPLICITLY_OPENED && zoneState != CLOSED && zoneState != EMPTY){
		xil_printf("Zone State Error: %d\r\n", zoneState);
		return 0;
	}
	
	// Sequential Write Check
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer != slba){
		xil_printf("Sequential Write Error: WP: %x SLBA: %x\r\n", zoneMapPtr->zoneReg[zoneID].Write_Pointer, slba);
		return 0;
	}

	// Out-of-Bound Check
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer + numOfSlice*4 > (zoneID + 1) * (NVME_BLOCKS_PER_ZONE/NVME_BLOCKS_PER_SLICE)){
		xil_printf("Out-of-Bound Error: WP: %x SLBA: %x numOfSlice : %d\r\n", zoneMapPtr->zoneReg[zoneID].Write_Pointer, slba, numOfSlice*4);
		return 0;
	}

	// Increment the write pointer
	zoneMapPtr->zoneReg[zoneID].Write_Pointer += numOfSlice*4;
	if(zoneMapPtr->zoneReg[zoneID].Write_Pointer == (zoneID + 1) * (NVME_BLOCKS_PER_ZONE/NVME_BLOCKS_PER_SLICE)){
		zoneMapPtr->zoneReg[zoneID].Zone_State = FULL;
	}

    return 1;
}

void ZNS_ReqTransSliceToLowLeve(unsigned int reqSlotTag){
    //unsigned int dataBufEntry;

	xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x\r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);
}
