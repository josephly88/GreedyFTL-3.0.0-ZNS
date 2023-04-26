#include "zns_mgmt.h"
#include "../../memory_map.h"

#include "xil_printf.h"

void handle_zns_open_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA){
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;

    if(mgmtSendInfo.SELECT_ALL == 1){
        int i;
        for(i = 0; i < MAXIMUM_ZONE_COUNT; i++){
            if(zoneMapPtr->zoneReg[i].Zone_State == CLOSED){
                zoneMapPtr->zoneReg[i].Zone_State = EXPLICITLY_OPENED;
                // MAP A FBG
            }
        }
    }
    else{
        unsigned int zone_id = SLBA / ZONE_CAP;
        if(zone_id >= MAXIMUM_ZONE_COUNT){
            xil_printf("SLBA out of range: 0x%X\r\n", SLBA);
            return;
        }
        unsigned char old_state = zoneMapPtr->zoneReg[zone_id].Zone_State;
        if(old_state == EMPTY || old_state == IMPLICITLY_OPENED || old_state == CLOSED){
            zoneMapPtr->zoneReg[zone_id].Zone_State = EXPLICITLY_OPENED;
            // MAP A FBG
        }
        else if(old_state == READ_ONLY || old_state == OFFLINE){
            // Abort
        }
    }
}