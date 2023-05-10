#include "zns_init.h"

void InitZNS()
{
    P_ZONE_MAP zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
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
