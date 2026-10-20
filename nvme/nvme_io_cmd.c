//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include "debug.h"
#include "string.h"
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"

#include "../memory_map.h"

#include "../ftl_config.h"
#include "../request_transform.h"

#include "zns/zns_mgmt.h"
#include "zns/zns_request_handler.h"

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_READ);
}


void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	//if(writeInfo12.FUA == 1)
	//	xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_WRITE);
}

static int ZnsZoneStateSelected(unsigned int filter, unsigned int zoneState)
{
	if(filter == 0x0)
		return 1;
	if(filter == 0x1)
		return zoneState == EMPTY;
	if(filter == 0x2)
		return zoneState == IMPLICITLY_OPENED;
	if(filter == 0x3)
		return zoneState == EXPLICITLY_OPENED;
	if(filter == 0x4)
		return zoneState == CLOSED;
	if(filter == 0x5)
		return zoneState == FULL;
	if(filter == 0x6)
		return zoneState == READ_ONLY;
	if(filter == 0x7)
		return zoneState == OFFLINE;
	return -1;
}

static void ZnsTxOnePage(unsigned int devAddr, unsigned int pcieAddrH, unsigned int pcieAddrL, unsigned int len)
{
	if(len == 0)
		return;
	set_direct_tx_dma(devAddr, pcieAddrH, pcieAddrL, len);
	check_direct_tx_dma_done();
}

/* PRP1 is the first data page. PRP2 is the next data page, or a PRP list when more than one page remains. */
static void ZnsTxPrps(unsigned int devAddr, unsigned int prp1L, unsigned int prp1H, unsigned int prp2L, unsigned int prp2H, unsigned int dataLen)
{
	unsigned int offset;
	unsigned int chunk;
	unsigned int *prpList;

	offset = prp1L & 0xFFF;
	chunk = 0x1000 - offset;
	if(chunk > dataLen)
		chunk = dataLen;

	ZnsTxOnePage(devAddr, prp1H, prp1L, chunk);
	devAddr += chunk;
	dataLen -= chunk;

	if(dataLen == 0)
		return;

	if(dataLen <= 0x1000)
	{
		ZnsTxOnePage(devAddr, prp2H, prp2L, dataLen);
		return;
	}

	prpList = (unsigned int *)ZNS_PRP_LIST_DRAM_ADDR;
	set_direct_rx_dma(ZNS_PRP_LIST_DRAM_ADDR, prp2H, prp2L, 0x1000);
	check_direct_rx_dma_done();

	while(dataLen > 0)
	{
		unsigned int pagesLeft;
		unsigned int dataEntries;
		unsigned int i;

		pagesLeft = (dataLen + 0x1000 - 1) / 0x1000;
		if(pagesLeft > (0x1000 / 8))
			dataEntries = (0x1000 / 8) - 1;
		else
			dataEntries = pagesLeft;

		for(i = 0; i < dataEntries; i++)
		{
			unsigned int entryL = prpList[i * 2];
			unsigned int entryH = prpList[i * 2 + 1];
			unsigned int n = dataLen;

			if(n > 0x1000)
				n = 0x1000;
			ZnsTxOnePage(devAddr, entryH, entryL, n);
			devAddr += n;
			dataLen -= n;
		}

		if(dataLen > 0)
		{
			unsigned int nextL = prpList[((0x1000 / 8) - 1) * 2];
			unsigned int nextH = prpList[((0x1000 / 8) - 1) * 2 + 1];
			set_direct_rx_dma(ZNS_PRP_LIST_DRAM_ADDR, nextH, nextL, 0x1000);
			check_direct_rx_dma_done();
		}
	}
}

unsigned short handle_nvme_io_zns_mgmt_send(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd){
	IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo;
	unsigned long long SLBA;

	(void)cmdSlotTag;
	mgmtSendInfo.dword = nvmeIOCmd->dword[13];
	SLBA = (((unsigned long long)nvmeIOCmd->dword11 << 32) + nvmeIOCmd->dword10);

	switch(mgmtSendInfo.ZSA)
	{
		case CLOSE_ZONE:
			return handle_zns_close_zone(mgmtSendInfo, SLBA);
		case FINISH_ZONE:
			return handle_zns_finish_zone(mgmtSendInfo, SLBA);
		case OPEN_ZONE:
			return handle_zns_open_zone(mgmtSendInfo, SLBA);
		case RESET_ZONE:
			return handle_zns_reset_zone(mgmtSendInfo, SLBA);
		case OFFLINE_ZONE:
		case SET_ZONE_DESCRIPTOR_EXTENSION:
			return handle_zns_offline_zone(mgmtSendInfo, SLBA);
		default:
			xil_printf("Not Support Zone Management Send Command OPC: 0x%X\r\n", mgmtSendInfo.ZSA);
			return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);
	}
}

unsigned short handle_nvme_io_zns_mgmt_recv(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd){
	IO_ZNS_ZONE_MANAGEMENT_RECEIVE_DW13 mgmtRecvInfo;
	unsigned long long SLBA;
	unsigned int dataLen;
	unsigned int maxDesc;
	unsigned int written;
	unsigned int matched;
	unsigned char *report;
	P_ZONE_MAP zoneMapPtr;
	unsigned int i;

	(void)cmdSlotTag;
	mgmtRecvInfo.dword = nvmeIOCmd->dword13;
	SLBA = (((unsigned long long)nvmeIOCmd->dword11 << 32) + nvmeIOCmd->dword10);
	dataLen = (nvmeIOCmd->dword12 + 1) * 4;

	if((nvmeIOCmd->PRP1[0] & 0x3) != 0 || (nvmeIOCmd->PRP2[0] & 0x3) != 0)
		return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_PRP_OFFSET_INVALID, 1);

	if(mgmtRecvInfo.ZRA != ZRA_REPORT_ZONES)
	{
		xil_printf("Not Support Zone Receive Action: 0x%X\r\n", mgmtRecvInfo.ZRA);
		return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);
	}

	if(mgmtRecvInfo.ZRA_specific_field > 0x7)
	{
		xil_printf("Not Support Zone Receive Action Specific Field: 0x%X\r\n", mgmtRecvInfo.ZRA_specific_field);
		return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);
	}

	if(dataLen < 64 || dataLen > ZNS_ZONE_REPORT_DRAM_SIZE)
		return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);

	zoneMapPtr = (P_ZONE_MAP) ZONE_MAP_ADDR;
	maxDesc = (dataLen - 64) / 64;
	matched = 0;

	for(i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT; i++)
	{
		int selected;

		if((unsigned long long)zoneMapPtr->zoneReg[i].SLBA < SLBA)
			continue;

		selected = ZnsZoneStateSelected(mgmtRecvInfo.ZRA_specific_field, zoneMapPtr->zoneReg[i].Zone_State);
		if(selected < 0)
		{
			xil_printf("Not Support Zone Receive Action Specific Field: 0x%X\r\n", mgmtRecvInfo.ZRA_specific_field);
			return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);
		}
		if(selected)
			matched++;
	}

	/* Partial bit clear: the buffer must hold every selected descriptor. */
	if(mgmtRecvInfo.ZRA_specific_feature == 0 && matched > maxDesc)
		return ZnsStatusWord(SCT_GENERIC_COMMAND_STATUS, SC_INVALID_FIELD_IN_COMMAND, 1);

	report = (unsigned char *)ZNS_ZONE_REPORT_DRAM_ADDR;
	memset(report, 0, dataLen);

	written = 0;
	for(i = 0; i < MAXIMUM_ACTIVE_ZONE_COUNT && written < maxDesc; i++)
	{
		ZONE_DESCRIPTOR *desc;
		int selected;

		if((unsigned long long)zoneMapPtr->zoneReg[i].SLBA < SLBA)
			continue;

		selected = ZnsZoneStateSelected(mgmtRecvInfo.ZRA_specific_field, zoneMapPtr->zoneReg[i].Zone_State);
		if(selected != 1)
			continue;

		desc = (ZONE_DESCRIPTOR *)(report + 64 + (written * sizeof(ZONE_DESCRIPTOR)));
		desc->ZT = 0x2;
		desc->ZS = (unsigned char)(zoneMapPtr->zoneReg[i].Zone_State << 4);
		desc->ZCAP = NVME_BLOCKS_PER_ZONE;
		desc->ZSLBA = zoneMapPtr->zoneReg[i].SLBA;
		desc->WP = zoneMapPtr->zoneReg[i].Write_Pointer;
		written++;
	}

	((unsigned long long *)report)[0] = written;

	ZnsTxPrps(ZNS_ZONE_REPORT_DRAM_ADDR, nvmeIOCmd->PRP1[0], nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP2[0], nvmeIOCmd->PRP2[1], dataLen);
	return 0;
}

void handle_nvme_io_zns_zone_append(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	/* Part A: one success completion. Dword 0 echoes the host zone start. No data and no WP update. */
	set_auto_nvme_cpl(cmdSlotTag, nvmeIOCmd->dword10, 0);
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;

	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	/*	xil_printf("OPC = 0x%X\r\n", nvmeIOCmd->OPC);
		xil_printf("PRP1[63:32] = 0x%X, PRP1[31:0] = 0x%X\r\n", nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP1[0]);
		xil_printf("PRP2[63:32] = 0x%X, PRP2[31:0] = 0x%X\r\n", nvmeIOCmd->PRP2[1], nvmeIOCmd->PRP2[0]);
		xil_printf("dword10 = 0x%X\r\n", nvmeIOCmd->dword10);
		xil_printf("dword11 = 0x%X\r\n", nvmeIOCmd->dword11);
		xil_printf("dword12 = 0x%X\r\n", nvmeIOCmd->dword12);*/


	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case IO_NVM_FLUSH:
		{
			//	xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		case IO_NVM_WRITE:
		{
			//xil_printf("IO Write Command\r\n");
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
			//xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_ZNS_MANAGEMENT_SEND:
		{
			if(ZNS_IO_COMMAND_SET){
				unsigned short status = handle_nvme_io_zns_mgmt_send(nvmeCmd->cmdSlotTag, nvmeIOCmd);
				set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, 0, status);
			}
			else{
				xil_printf("Not Support ZNS IO Command Set - OPC: %X\r\n", opc);
				ASSERT(0);		
			}
			break;
		}
		case IO_ZNS_MANAGEMENT_RECEIVE:
		{
			if(ZNS_IO_COMMAND_SET){
				unsigned short status = handle_nvme_io_zns_mgmt_recv(nvmeCmd->cmdSlotTag, nvmeIOCmd);
				set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, 0, status);
			}
			else{
				xil_printf("Not Support ZNS IO Command Set - OPC: %X\r\n", opc);
				ASSERT(0);		
			}			
			break;
		}
		case IO_ZNS_ZONE_APPEND:
		{
			if(ZNS_IO_COMMAND_SET)
				handle_nvme_io_zns_zone_append(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			else{
				xil_printf("Not Support ZNS IO Command Set - OPC: %X\r\n", opc);
				ASSERT(0);
			}
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X\r\n", opc);
			ASSERT(0);
			break;
		}
	}
}

