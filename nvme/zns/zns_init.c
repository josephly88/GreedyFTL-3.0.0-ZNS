#include "zns_init.h"

void InitZNS()
{
    P_ZONE_MAP zoneMapPtr = ZONE_MAP_ADDR;
    zoneMapPtr->Num_Open_Zone = 0;
    zoneMapPtr->Num_Close_Zone = 0;
    zoneMapPtr->Num_Full_Zone = 0;
    zoneMapPtr->Num_Empty_Zone = MAXIMUM_ZONE_COUNT;
    zoneMapPtr->Num_Read_Zone = 0;
    zoneMapPtr->Num_Off_Zone = 0;

    for (int i = 0; i < MAXIMUM_ZONE_COUNT; i++){
        zoneMapPtr->zoneReg[i].Zone_ID = i;
        zoneMapPtr->zoneReg[i].FBG = 0;
        zoneMapPtr->zoneReg[i].Zone_State = EMPTY;
        zoneMapPtr->zoneReg[i].Write_Pointer = i * ZONE_CAP;
    }
}