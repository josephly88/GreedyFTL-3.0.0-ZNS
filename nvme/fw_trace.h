#ifndef __FW_TRACE_H_
#define __FW_TRACE_H_

/*
 * 1 Hz NVMe path counters. CQ head/tail doorbells are inside the NVMe IP
 * and are not MMIO-readable; cq_written/irq count firmware CPL FIFO posts
 * (the IP then writes host CQ and may raise MSI). DMA auto-completion CQ
 * writes are counted separately as hwcq.
 */

typedef struct _FW_TRACE
{
	unsigned int cmd;
	unsigned int opc7d;
	unsigned int opc01;
	unsigned int opc79;
	unsigned int apnd_arm;
	unsigned int apnd_cpl;
	unsigned int cpl;
	unsigned int cq_written;
	unsigned int irq;
	unsigned int hwcq;
	unsigned int last_qid;
	unsigned int last_seq;
	unsigned int last_opc;
} FW_TRACE;

extern FW_TRACE g_fwTrace;
extern unsigned int g_fwLogEnable;

void FwTraceOnCmd(unsigned int qid, unsigned int seq, unsigned int opc);
void FwTraceOnIoOpc(unsigned int opc);
void FwTraceOnAppendArm(void);
void FwTraceOnAppendCpl(void);
void FwTraceOnFwCpl(void);
void FwTraceOnHwAutoCpl(void);
void FwTracePrint1Hz(unsigned int hbCnt, unsigned int st,
	unsigned int dma, unsigned int nand, unsigned int blk,
	unsigned int apndLive);

#endif
