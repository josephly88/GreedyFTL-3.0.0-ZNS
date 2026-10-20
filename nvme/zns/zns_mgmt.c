#include "zns_mgmt.h"
#include "../../memory_map.h"
#include "zns_request_handler.h"
#include "../nvme.h"

#include "xil_printf.h"

static unsigned short ZnsInvalidField(void)
{
    return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);
}

static unsigned short ZnsInvalidTransition(void)
{
    return ZnsStatusWord(SCT_COMMAND_SPECIFIC_STATUS, SC_ZONE_INVALID_WRITE, 1);
}

static int ZnsMgmtZoneId(unsigned long long SLBA, unsigned int *zoneId)
{
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
    unsigned int id;

    if(SLBA > ZNS_LBA_END_NVME_BLOCK)
        return -1;

    id = (unsigned int)Lba2ZoneId(SLBA);
    if(id >= MAXIMUM_ACTIVE_ZONE_COUNT)
        return -1;
    if((unsigned long long)zoneMapPtr->zoneReg[id].SLBA != SLBA)
        return -1;

    *zoneId = id;
    return 0;
}

static void ZnsDropOpenCount(P_ZONE_MAP zoneMapPtr, unsigned char state)
{
    if(state == IMPLICITLY_OPENED || state == EXPLICITLY_OPENED)
    {
        if(zoneMapPtr->Num_Open_Zone > 0)
            zoneMapPtr->Num_Open_Zone--;
    }
}

static unsigned short ZnsCloseOne(P_ZONE_MAP zoneMapPtr, unsigned int zoneId, unsigned int selectAll)
{
    unsigned char cur_state = zoneMapPtr->zoneReg[zoneId].Zone_State;

    if(cur_state == EXPLICITLY_OPENED || cur_state == IMPLICITLY_OPENED)
    {
        zoneMapPtr->zoneReg[zoneId].Zone_State = CLOSED;
        ZnsDropOpenCount(zoneMapPtr, cur_state);
        return 0;
    }

    if(selectAll)
        return 0;
    return ZnsInvalidTransition();
}

unsigned short handle_zns_close_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        unsigned int i;
        for(i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++){
            unsigned short status = ZnsCloseOne(zoneMapPtr, i, 1);
            if(status != 0)
                return status;
        }
        return 0;
    }

    {
        unsigned int zone_id;
        if(ZnsMgmtZoneId(SLBA, &zone_id) != 0)
            return ZnsInvalidField();
        return ZnsCloseOne(zoneMapPtr, zone_id, 0);
    }
}

static unsigned short ZnsFinishOne(P_ZONE_MAP zoneMapPtr, unsigned int zoneId, unsigned int selectAll)
{
    unsigned char cur_state = zoneMapPtr->zoneReg[zoneId].Zone_State;

    if(cur_state == EMPTY || cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED)
    {
        ZnsDropOpenCount(zoneMapPtr, cur_state);
        zoneMapPtr->zoneReg[zoneId].Zone_State = FULL;
        return 0;
    }

    if(cur_state == FULL || selectAll)
        return 0;
    return ZnsInvalidTransition();
}

unsigned short handle_zns_finish_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        unsigned int i;
        for(i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++){
            unsigned short status = ZnsFinishOne(zoneMapPtr, i, 1);
            if(status != 0)
                return status;
        }
        return 0;
    }

    {
        unsigned int zone_id;
        if(ZnsMgmtZoneId(SLBA, &zone_id) != 0)
            return ZnsInvalidField();
        return ZnsFinishOne(zoneMapPtr, zone_id, 0);
    }
}

static unsigned short ZnsOpenOne(P_ZONE_MAP zoneMapPtr, unsigned int zoneId, unsigned int selectAll)
{
    unsigned char cur_state = zoneMapPtr->zoneReg[zoneId].Zone_State;

    if(cur_state == EXPLICITLY_OPENED)
        return 0;

    if(cur_state == EMPTY || cur_state == IMPLICITLY_OPENED || cur_state == CLOSED)
    {
        if(cur_state != IMPLICITLY_OPENED)
        {
            if(zoneMapPtr->Num_Open_Zone >= MAXIMUM_OPEN_ZONE_COUNT)
                return ZnsStatusWord(SCT_COMMAND_SPECIFIC_STATUS, SC_TOO_MANY_OPEN_ZONES, 1);
            zoneMapPtr->Num_Open_Zone++;
        }
        zoneMapPtr->zoneReg[zoneId].Zone_State = EXPLICITLY_OPENED;
        return 0;
    }

    if(selectAll)
        return 0;
    return ZnsInvalidTransition();
}

unsigned short handle_zns_open_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        unsigned int i;
        for(i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++){
            unsigned short status = ZnsOpenOne(zoneMapPtr, i, 1);
            if(status != 0)
                return status;
        }
        return 0;
    }

    {
        unsigned int zone_id;
        if(ZnsMgmtZoneId(SLBA, &zone_id) != 0)
            return ZnsInvalidField();
        return ZnsOpenOne(zoneMapPtr, zone_id, 0);
    }
}

unsigned short handle_zns_reset_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int ZoneID;
        for(ZoneID = 0; ZoneID < MAXIMUM_ACTIVE_ZONE_COUNT; ZoneID++){
            unsigned char cur_state = zoneMapPtr->zoneReg[ZoneID].Zone_State;

            if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
                resetZone(ZoneID);
                ZnsDropOpenCount(zoneMapPtr, cur_state);
            }
        }
        return 0;
    }

    {
        unsigned int ZoneID;
        unsigned char cur_state;

        if(ZnsMgmtZoneId(SLBA, &ZoneID) != 0)
            return ZnsInvalidField();

        cur_state = zoneMapPtr->zoneReg[ZoneID].Zone_State;
        if(cur_state == EMPTY)
            return 0;
        if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
            resetZone(ZoneID);
            ZnsDropOpenCount(zoneMapPtr, cur_state);
            return 0;
        }
        return ZnsInvalidTransition();
    }
}

void resetZone(unsigned int zoneId){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
    int BufferIdx, DieIdx;
    ZONE_REG zoneReg = zoneMapPtr->zoneReg[zoneId];
    int zoneBufferID = zoneReg.Buffer_ID;
    P_WRR wrrPtr = (P_WRR) WRR_ADDR;

    // Clear all write buffers
    if(BUFFER_MODE == 0){
        for(BufferIdx = 0; BufferIdx < DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE; BufferIdx++){
            unsigned int dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (zoneBufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + BufferIdx;
            dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
        }

        if(NON_SHARE_MOD == 1){
            int cnt = wrrPtr->buffer_count[zoneId];
            wrrPtr->buffer_count[zoneId] = 0;
            wrrPtr->total_buffer_count -= cnt;

            wrr_remove_zone(zoneId);
        }
    }
    else if(BUFFER_MODE == 1){
        for(BufferIdx = 0; BufferIdx < OPEN_ZONE_DATA_BUFFER_ENTRY_COUNT; BufferIdx++){
            unsigned int dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + BufferIdx;
            if(Lsa2ZoneId(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr) == zoneId){
                dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
            }
        }
    }
    else{
        xil_printf("Invalid Buffer Mode: %d\r\n", BUFFER_MODE);
    }

    int WrittenSize = zoneReg.Write_Pointer - zoneReg.SLBA;

    //xil_printf("Catch a zone reset command - zoneRegId : %d\r\n", zoneRegId);
    //xil_printf("Reset Size in NVMe Block: %d\r\n", WrittenSize);

    // Erase all written blocks in the zone
    unsigned int innerBlockNo = 0;
    while(WrittenSize > 0){
        for(DieIdx = 0; DieIdx < NUM_OF_DIE_PER_ZONE; DieIdx++){
            unsigned int dieNo, blockNo;
            dieNo = ((zoneReg.Phy_Block_Group_ID % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + DieIdx;
            if(CHANNEL_WAY_ORIENTED == 1)      // Swap ch & way
                dieNo = dieNo / 8 + ((dieNo % 8) * 8);
            blockNo = zoneReg.Phy_Block_Group_ID / BLOCK_GROUP_IN_COLUMN * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;
            EraseBlock(dieNo, blockNo);
        }
        
        SyncAllLowLevelReqDone();

        WrittenSize -= (NVME_BLOCKS_PER_SLICE * SLICES_PER_BLOCK * NUM_OF_DIE_PER_ZONE);
        innerBlockNo++;
    }

    validBlockGroupFifo_Enqueue(zoneReg.Phy_Block_Group_ID);

    resetWriteBufferReg(zoneBufferID);
    if(BUFFER_MODE == 0 && zoneReg.Buffer_ID != -1)
        bufferIDFifo_Enqueue(zoneReg.Buffer_ID);

    resetZoneReg(zoneId);
}

unsigned short handle_zns_offline_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    (void)mgmtSendInfo;
    (void)SLBA;
    return ZnsInvalidField();
}
