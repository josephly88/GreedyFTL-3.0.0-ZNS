#ifndef __ZNS_MGMT_H__
#define __ZNS_MGMT_H__

#include "zns.h"

void handle_zns_close_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA);

void handle_zns_finish_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA);

void handle_zns_open_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA);

void handle_zns_reset_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA);

void resetZone(unsigned int zoneRegId);

void handle_zns_offline_zone(IO_ZNS_ZONE_MANGAEMENT_SEND_DW13 mgmtSendInfo, unsigned long long SLBA);

#endif