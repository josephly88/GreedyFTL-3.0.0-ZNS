#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

void InitZNS();

void parameterCheck();

void resetZoneReg(int ZoneRegID);

void eliminateBadBlockGroups();

void shuffleValidBlockGroups();

unsigned int validBlockGroupFifo_Dequeue();

void validBlockGroupFifo_Enqueue(unsigned int element);

unsigned int zoneIDFifo_Dequeue();

void zoneIDFifo_Enqueue(unsigned int element);

int ZoneWriteCheck(unsigned int zoneRegID, unsigned int slba, unsigned int numOfSlice);

int ZoneReadCheck(unsigned int zoneRegID, unsigned int slba, unsigned int nlb);

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset);

void incrementDataBufPointer(unsigned int zoneID);

unsigned int checkZoneWriteDataBuf(unsigned int reqSlotTag, unsigned int zoneID);

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag);

void ZNS_EvictDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag);

void ZNS_DataReadFromNand(unsigned int zoneID, unsigned int originReqSlotTag);

unsigned int ZNS_AddrTrans(unsigned int zoneID, unsigned int lsa);

#endif
