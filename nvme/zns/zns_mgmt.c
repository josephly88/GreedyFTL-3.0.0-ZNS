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
        int ZoneRegIdx;
        for(ZoneRegIdx = 0; ZoneRegIdx < MAXIMUM_OPEN_ZONE_COUNT; ZoneRegIdx++){
            ZONE_REG* zoneReg = &zoneMapPtr->zoneReg[ZoneRegIdx];
            unsigned char cur_state = zoneReg->Zone_State;

            if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
                resetZone(ZoneRegIdx);
                
                if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED)
                    zoneMapPtr->Num_Open_Zone--;
                else if(cur_state == CLOSED)
                    zoneMapPtr->Num_Close_Zone--;
                else if(cur_state == FULL)
                    zoneMapPtr->Num_Full_Zone--;
                zoneMapPtr->Num_Empty_Zone++;
            }                
        }
    }
    else{
        unsigned int ZoneRegIdx = (SLBA - ZNS_LBA_START_NVME_BLOCK) / NVME_BLOCKS_PER_ZONE;

        if(ZoneRegIdx >= MAXIMUM_ACTIVE_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }

        unsigned char cur_state = zoneMapPtr->zoneReg[ZoneRegIdx].Zone_State;
        if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
            resetZone(ZoneRegIdx);

            if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED)
                zoneMapPtr->Num_Open_Zone--;
            else if(cur_state == CLOSED)
                zoneMapPtr->Num_Close_Zone--;
            else if(cur_state == FULL)
                zoneMapPtr->Num_Full_Zone--;
            zoneMapPtr->Num_Empty_Zone++;
        }
        else if(cur_state == READ_ONLY || cur_state == OFFLINE){
            // Abort
            xil_printf("Zone State is not active: Current State - 0x%X\r\n", cur_state);
        }
    }
}

void resetZone(unsigned int zoneRegId){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
    int BufferIdx, DieIdx;
    ZONE_REG zoneReg = zoneMapPtr->zoneReg[zoneRegId];

    zoneIDFifo_Enqueue(zoneReg.Zone_ID);

    for(BufferIdx = 0; BufferIdx < DATA_BUFFER_ENTRY_COUNT_PER_ZONE; BufferIdx++){
        unsigned int dataBufEntry = ZNS_DATA_BUFFER_ENTRY_START + (zoneReg.Zone_ID * DATA_BUFFER_ENTRY_COUNT_PER_ZONE) + BufferIdx;
        dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
    }

    int WrittenSize = zoneReg.Write_Pointer - zoneReg.SLBA;

    //xil_printf("Catch a zone reset command - zoneRegId : %d\r\n", zoneRegId);
    //xil_printf("Reset Size in NVMe Block: %d\r\n", WrittenSize);

    unsigned int innerBlockNo = 0;
    while(WrittenSize > 0){
        for(DieIdx = 0; DieIdx < NUM_OF_DIE_PER_ZONE; DieIdx++){
            unsigned int dieNo, blockNo;
            dieNo = ((zoneReg.Phy_Block_Group_ID % BLOCK_GROUP_IN_COLUMN) * NUM_OF_DIE_PER_ZONE) + DieIdx;
            if(CHANNEL_DIE_ORIENTED == 1)      // Swap ch & way
                dieNo = dieNo / 8 + ((dieNo % 8) * 8);
            blockNo = zoneReg.Phy_Block_Group_ID / BLOCK_GROUP_IN_COLUMN * NUM_OF_BLOCK_PER_ZONE + innerBlockNo;
            EraseBlock(dieNo, blockNo);
        }

        WrittenSize -= (NVME_BLOCKS_PER_SLICE * SLICES_PER_BLOCK * NUM_OF_DIE_PER_ZONE);
        innerBlockNo++;
    }

    resetZoneReg(zoneRegId);
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
