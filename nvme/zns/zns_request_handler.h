#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

void InitZNS();

int ZoneWriteCheck(unsigned int slba, unsigned int numOfSlice);

int ZoneReadCheck(unsigned int slba, unsigned int nlb);

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset);

void incrementDataBufPointer(unsigned int zoneID);

unsigned int checkZoneWriteDataBuf(unsigned int reqSlotTag, unsigned int zoneID);

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag);

void ZNS_EvictDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag);

void ZNS_DataReadFromNand(unsigned int originReqSlotTag);

#endif
