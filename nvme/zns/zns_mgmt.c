#include "zns_mgmt.h"
#include "../../memory_map.h"
#include "zns_request_handler.h"

#include "xil_printf.h"

void handle_zns_close_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int i;
        for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == EXPLICITLY_OPENED || zoneMapPtr->zoneReg[i].Zone_State == IMPLICITLY_OPENED){
                zoneMapPtr->zoneReg[i].Zone_State = CLOSED;
                // UNMAP A FBG
            }
        }
    }
    else{
        unsigned int zone_id = (SLBA - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE;

        if(zone_id >= MAXIMUM_OPEN_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[zone_id].Zone_State;
        if(cur_state == EXPLICITLY_OPENED || cur_state == IMPLICITLY_OPENED){
            zoneMapPtr->zoneReg[zone_id].Zone_State = CLOSED;
            // UNMAP A FBG
        }
        else if(cur_state == EMPTY || cur_state == FULL || cur_state == READ_ONLY || cur_state == OFFLINE){
            // Abort
        }
    }
}

void handle_zns_finish_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int i;
        for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == IMPLICITLY_OPENED || zoneMapPtr->zoneReg[i].Zone_State == EXPLICITLY_OPENED
                    || zoneMapPtr->zoneReg[i].Zone_State == CLOSED){
                zoneMapPtr->zoneReg[i].Zone_State = FULL;
                // UNMAP A FBG
            }
        }
    }
    else{
        unsigned int zone_id = (SLBA - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE;

        if(zone_id >= MAXIMUM_OPEN_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[zone_id].Zone_State;
        if(cur_state == EMPTY || cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED){
            zoneMapPtr->zoneReg[zone_id].Zone_State = FINISH_ZONE;
            // UNMAP A FBG
        }
        else if(cur_state == READ_ONLY || cur_state == FULL){
            // Abort
        }
    }
}

void handle_zns_open_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int i;
        for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == CLOSED){
                zoneMapPtr->zoneReg[i].Zone_State = EXPLICITLY_OPENED;
                // MAP A FBG
            }
        }
    }
    else{
        unsigned int zone_id = (SLBA - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE;

        if(zone_id >= MAXIMUM_OPEN_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[zone_id].Zone_State;
        if(cur_state == EMPTY || cur_state == IMPLICITLY_OPENED || cur_state == CLOSED){
            zoneMapPtr->zoneReg[zone_id].Zone_State = EXPLICITLY_OPENED;
            // MAP A FBG
        }
        else if(cur_state == READ_ONLY || cur_state == OFFLINE){
            // Abort
        }
    }
}

void handle_zns_reset_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int ZoneID;
        for(ZoneID = 0; ZoneID < MAXIMUM_ACTIVE_ZONE_COUNT; ZoneID++){
            ZONE_REG* zoneReg = &zoneMapPtr->zoneReg[ZoneID];
            unsigned char cur_state = zoneReg->Zone_State;

            if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
                resetZone(ZoneID);
                
                if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED)
                    zoneMapPtr->Num_Open_Zone--;
            }                
        }
    }
    else{
        unsigned int ZoneID = Lba2ZoneId(SLBA);

        if(ZoneID >= MAXIMUM_ACTIVE_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[ZoneID].Zone_State;
        if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
            resetZone(ZoneID);

            if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED)
                zoneMapPtr->Num_Open_Zone--;
        }
        else if(cur_state == READ_ONLY || cur_state == OFFLINE){
            // Abort
            xil_printf("Zone State is not active: Current State - 0x%X\r\n", cur_state);
        }
    }
}

void resetZone(unsigned int zoneId){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
    P_UNI_BUF_REG uniBufRegPtr = (P_UNI_BUF_REG) UNI_BUF_REG_ADDR;
    int BufferIdx, DieIdx;
    ZONE_REG zoneReg = zoneMapPtr->zoneReg[zoneId];
    int zoneBufferID = zoneReg.Buffer_ID;

    // Clear all write buffers
    if(PER_ZONE_BUFFER == 1){
        for(BufferIdx = 0; BufferIdx < DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE; BufferIdx++){
            unsigned int dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (zoneBufferID * DATA_BUFFER_ENTRY_COUNT_PER_OPEN_ZONE) + BufferIdx;
            dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
        }
    }
    else{
        int lastBufIdx = uniBufRegPtr->LAST_BUF[zoneId];
        if(lastBufIdx >= 0)
            dataBufMapPtr->dataBuf[lastBufIdx].dirty = DATA_BUF_CLEAN;
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
    if(PER_ZONE_BUFFER == 1 && zoneReg.Buffer_ID != -1)
        bufferIDFifo_Enqueue(zoneReg.Buffer_ID);

    resetZoneReg(zoneId);
}

void handle_zns_offline_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int i;
        for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == READ_ONLY){
                zoneMapPtr->zoneReg[i].Zone_State = OFFLINE;
                // UNMAP A FBG
            }
        }
    }
    else{
        unsigned int zone_id = (SLBA - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE;

        if(zone_id >= MAXIMUM_OPEN_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[zone_id].Zone_State;
        if(cur_state == READ_ONLY){
            zoneMapPtr->zoneReg[zone_id].Zone_State = OFFLINE;
            // UNMAP A FBG
        }
        else if(cur_state == EMPTY || cur_state == IMPLICITLY_OPENED 
            || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
            // Abort
        }
    }
}
