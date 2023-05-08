#include "zns_request_handler.h"

#include "xil_printf.h"
#include <assert.h>
#include "../nvme.h"
#include "../host_lld.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"

int ZoneWriteCheck(unsigned int startLBA){

    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    unsigned int zoneID;
        // Let's say we already translate the ZoneID from the startLBA
    if(zoneID == 0)
        return 0;

    unsigned char zone_state = zoneMapPtr->zoneReg[zoneID].Zone_State;
    if(zone_state == IMPLICITLY_OPENED || zone_state == EXPLICITLY_OPENED || zone_state == CLOSED){
        if(zoneMapPtr->zoneReg[zoneID].Write_Pointer == startLBA)
            return 1;
    }
    
    return 0;
}

void ZNS_ReqTransSliceToLowLeve(){

    unsigned int reqSlotTag, dataBufEntry;

	while(sliceReqQ.headReq != REQ_SLOT_TAG_NONE)
	{
		reqSlotTag = GetFromSliceReqQ();
		if(reqSlotTag == REQ_SLOT_TAG_FAIL)
			return ;

        //allocate a data buffer entry for this request
		dataBufEntry = CheckDataBufHit(reqSlotTag);
		if(dataBufEntry != DATA_BUF_FAIL)
		{
			//data buffer hit
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		}
		else
		{
			//data buffer miss, allocate a new buffer entry
			dataBufEntry = AllocateDataBuf();
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

			//clear the allocated data buffer entry being used by a previous request
			EvictDataBufEntry(reqSlotTag);

			//update meta-data of the allocated data buffer entry
			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
			PutToDataBufHashList(dataBufEntry);

			if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_READ)
				DataReadFromNand(reqSlotTag);
			else if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_WRITE)
				if(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock != NVME_BLOCKS_PER_SLICE) //for read modify write
					DataReadFromNand(reqSlotTag);
		}

		//transform this slice request to nvme request
		if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_WRITE)
		{
			dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;
		}
		else if(reqPoolPtr->reqPool[reqSlotTag].reqCode  == REQ_CODE_READ)
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		else
			assert(!"[WARNING] Not supported reqCode. [WARNING]");

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
		SelectLowLevelReqQ(reqSlotTag);
	}
}