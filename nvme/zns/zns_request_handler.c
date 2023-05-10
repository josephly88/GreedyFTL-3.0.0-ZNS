#include "zns.h"
#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

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
	zoneReg.Write_Pointer += nlb;
	if(zoneReg.Write_Pointer == zoneReg.SLBA + NVME_BLOCKS_PER_ZONE - 1){
		zoneMapPtr->zoneReg[zoneID].Zone_State = FULL;
	}

    return 1;
}

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag){
    unsigned int zoneID, dataBufEntry;

	xil_printf("Catch a ZNS Request LogicalSliceAddr : 0x%x\r\n", reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);

	zoneID = Lsa2ZoneId(reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr);
	xil_printf("Zone ID : %d\r\n", zoneID);
}
