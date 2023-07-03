#include "zns_mgmt.h"
#include "../../memory_map.h"

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
        int i;
        for(i = 0; i < MAXIMUM_OPEN_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == IMPLICITLY_OPENED || zoneMapPtr->zoneReg[i].Zone_State == EXPLICITLY_OPENED
                    || zoneMapPtr->zoneReg[i].Zone_State == CLOSED || zoneMapPtr->zoneReg[i].Zone_State == FULL){
                zoneMapPtr->zoneReg[i].Zone_State = EMPTY;
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
        if(cur_state == IMPLICITLY_OPENED || cur_state == EXPLICITLY_OPENED || cur_state == CLOSED || cur_state == FULL){
            zoneMapPtr->zoneReg[zone_id].Zone_State = EMPTY;
            zoneMapPtr->zoneReg[zone_id].Write_Pointer = zoneMapPtr->zoneReg[zone_id].SLBA;
            // UNMAP A FBG
        }
        else if(cur_state == READ_ONLY || cur_state == OFFLINE){
            // Abort
        }
    }
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
