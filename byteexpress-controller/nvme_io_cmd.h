//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.h
//
// Version: v1.0.0
//
// Description:
//   - declares functions for handling NVMe IO commands
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef __NVME_IO_CMD_H_
#define __NVME_IO_CMD_H_

#include "xtime_l.h"

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd);
void TriggerInternalPageWrite(const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize);
void TriggerInternalPageRead (const unsigned int startLsa, const unsigned int bufAddr, const unsigned int bufSize);
void TriggerInternalPagesRead (const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages);
void TriggerInternalPagesWrite (const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages);
unsigned int GetTypefromCmdSlotTag (int cmdSlotTag);
void test ();

#define STAT


struct STAT_PER_TYPE {
	unsigned int c;
	unsigned int found;
	unsigned int lost;
	unsigned int hit;
	XTime TOTAL_TIME;
	XTime NAND_READ_SSTABLE;
	XTime NAND_READ_LOG;
	XTime DEVICE_MEMCPY;
};

enum CMD_TYPE {
	START_CMD,
	KV_PUT,
	KV_GET,
	KV_DEL,
	ITER_CREATE,
	ITER_SEEK,
	ITER_NEXT,
	ITER_DESTROY,
	LAST_CMD,
};

#endif	//__NVME_IO_CMD_H_
