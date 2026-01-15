#ifndef __ZNS_REQUEST_HANDLER_H__
#define __ZNS_REQUEST_HANDLER_H__

#include "zns.h"

void InitZNS();

void parameterCheck();

void resetZoneReg(int ZoneRegID);

void resetWriteBufferReg(int BufferID);

void eliminateBadBlockGroups();

void shuffleValidBlockGroups();

unsigned int validBlockGroupFifo_Dequeue();

void validBlockGroupFifo_Enqueue(unsigned int element);

unsigned int bufferIDFifo_Dequeue();

void bufferIDFifo_Enqueue(unsigned int element);

int chNo_Cal(unsigned int zoneID, unsigned int requestSlotTag);

int bufferQueue_Enqueue(unsigned int chNo);

void bufferQueue_Dequeue(unsigned int chNo);

int ZoneWriteCheck(unsigned int zoneID, unsigned int slba, unsigned int numOfSlice);

int ZoneReadCheck(unsigned int zoneID, unsigned int slba, unsigned int nlb);

unsigned int GetZoneDataBuf(unsigned int zoneID, int offset);

void incrementDataBufPointer(unsigned int zoneID);

unsigned int checkZoneWriteDataBuf(unsigned int reqSlotTag, unsigned int zoneID);

unsigned int checkZoneReadDataBuf(unsigned int reqSlotTag);

unsigned int AllocateZoneDataBuf();

void SelectiveGetFromZoneDataBufHashList(unsigned int bufEntry);

void ZNS_ReqTransSliceToLowLevel(unsigned int reqSlotTag);

unsigned int ZNS_AllocateWriteDataBuf(unsigned int zoneID, unsigned int reqSlotTag);

unsigned int ZNS_AllocateReadDataBuf(unsigned int zoneID, unsigned int reqSlotTag);

void ZNS_EvictDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag);

void ZNS_EvictAllDataBufEntry(unsigned int zoneID, unsigned int originReqSlotTag);

void ZNS_DataReadFromNand(unsigned int zoneID, unsigned int originReqSlotTag);

unsigned int ZNS_AddrTransWrite(unsigned int zoneID, unsigned int lsa);

unsigned int ZNS_AddrTransRead(unsigned int logicalSliceAddr);

#endif
