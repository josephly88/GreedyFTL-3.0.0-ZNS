#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

int ZoneWriteCheck(unsigned int logicalSliceAddr);

void ZoneWritePointerIncrement(unsigned int logicalSliceAddr);

void ZNS_ReqTransSliceToLowLeve();

#endif