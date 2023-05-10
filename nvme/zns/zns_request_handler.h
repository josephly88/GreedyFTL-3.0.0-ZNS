#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

int ZoneWriteCheck(unsigned int slba, unsigned int numOfSlice);

unsigned int findDataBufForWrite(unsigned int zoneID);

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag);

void ZNS_EvictDataBufStripe(unsigned int zoneID, unsigned int originReqSlotTag);

void ZNS_DataReadFromNand(unsigned int originReqSlotTag);

void ZNS_SelectLowLevelReqQ(unsigned int reqSlotTag);

#endif