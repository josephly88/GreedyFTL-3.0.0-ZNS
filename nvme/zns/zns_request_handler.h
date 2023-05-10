#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

int ZoneWriteCheck(unsigned int slba, unsigned int numOfSlice);

unsigned int findDataBufForWrite(unsigned int zoneID);

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag);

#endif