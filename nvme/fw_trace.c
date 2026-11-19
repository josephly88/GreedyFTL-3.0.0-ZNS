#include "fw_trace.h"
#include "host_lld.h"
#include "io_access.h"
#include "xil_printf.h"

FW_TRACE g_fwTrace;

void FwTraceOnCmd(unsigned int qid, unsigned int seq, unsigned int opc)
{
	g_fwTrace.cmd++;
	g_fwTrace.last_qid = qid;
	g_fwTrace.last_seq = seq;
	g_fwTrace.last_opc = opc;
}

void FwTraceOnIoOpc(unsigned int opc)
{
	if(opc == 0x7D)
		g_fwTrace.opc7d++;
	else if(opc == 0x01)
		g_fwTrace.opc01++;
	else if(opc == 0x79)
		g_fwTrace.opc79++;
}

void FwTraceOnAppendArm(void)
{
	g_fwTrace.apnd_arm++;
}

void FwTraceOnAppendCpl(void)
{
	g_fwTrace.apnd_cpl++;
}

void FwTraceOnFwCpl(void)
{
	g_fwTrace.cpl++;
	g_fwTrace.cq_written++;
	g_fwTrace.irq++;
}

void FwTraceOnHwAutoCpl(void)
{
	g_fwTrace.hwcq++;
}

void FwTracePrint1Hz(unsigned int hbCnt, unsigned int st,
	unsigned int dma, unsigned int nand, unsigned int blk,
	unsigned int apndLive)
{
	PCIE_FUNC_REG pcieFunc;
	HOST_DMA_FIFO_CNT_REG dmaHead;

	if(!g_fwLogEnable)
		return;

	pcieFunc.dword = IO_READ32(PCIE_FUNC_REG_ADDR);
	dmaHead.dword = IO_READ32(HOST_DMA_FIFO_CNT_REG_ADDR);
	g_hostDmaStatus.fifoHead.dword = dmaHead.dword;

	xil_printf("HB %u st=%u dma=%u nand=%u blk=%u live=%u\r\n",
		hbCnt, st, dma, nand, blk, apndLive);
	xil_printf("HB cmd=%u 7d=%u 01=%u 79=%u arm=%u acpl=%u cpl=%u cq=%u irq=%u hwcq=%u\r\n",
		g_fwTrace.cmd, g_fwTrace.opc7d, g_fwTrace.opc01, g_fwTrace.opc79,
		g_fwTrace.apnd_arm, g_fwTrace.apnd_cpl, g_fwTrace.cpl,
		g_fwTrace.cq_written, g_fwTrace.irq, g_fwTrace.hwcq);
	xil_printf("HB qid=%u seq=%u opc=0x%x rxH=%u rxT=%u txH=%u txT=%u msi=%u msix=%u irqDis=%u\r\n",
		g_fwTrace.last_qid, g_fwTrace.last_seq, g_fwTrace.last_opc,
		dmaHead.autoDmaRx, g_hostDmaStatus.fifoTail.autoDmaRx,
		dmaHead.autoDmaTx, g_hostDmaStatus.fifoTail.autoDmaTx,
		pcieFunc.msiEnable, pcieFunc.msixEnable, pcieFunc.irqDisable);
}
