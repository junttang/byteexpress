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
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"
#include "../memory_map.h"

#include "../ftl_config.h"
#include "../request_transform.h"
#include "../sstable/sstable.h"
#include "../sstable/super.h"
#include "../memtable/memtable.h"
#include "../iterator/iterator.h"

#include "xtime_l.h"

#define SEED1 0xcc9ed51
#define HASH_NUM 30

// #define DEBUG_iLSM

static SKIPLIST_HEAD* skiphead = (SKIPLIST_HEAD*)MEMTABLE_HEAD_ADDR;
static SUPER_LEVEL_INFO* super_level_info = (SUPER_LEVEL_INFO*)SUPER_BLOCK_ADDR;
static SUPER_SSTABLE_LIST* super_sstable_list = (SUPER_SSTABLE_LIST*)SUPER_SSTABLE_LIST_ADDR;
static SUPER_SSTABLE_INFO* super_sstable_list_level[4] = {
		(SUPER_SSTABLE_INFO*)SUPER_SSTABLE_LIST0_ADDR,
		(SUPER_SSTABLE_INFO*)SUPER_SSTABLE_LIST1_ADDR,
		(SUPER_SSTABLE_INFO*)SUPER_SSTABLE_LIST2_ADDR,
		(SUPER_SSTABLE_INFO*)SUPER_SSTABLE_LIST3_ADDR
};

const unsigned int MAX_SSTABLE_LEVEL[4] = {
	MAX_SSTABLE_LEVEL0,
	MAX_SSTABLE_LEVEL1,
	MAX_SSTABLE_LEVEL2,
	MAX_SSTABLE_LEVEL3
};

const unsigned int COMPACTION_THRESHOLD_LEVEL[4] = {
	MAX_SSTABLE_LEVEL0-1,
	MAX_SSTABLE_LEVEL1-1,
	MAX_SSTABLE_LEVEL2-1,
	MAX_SSTABLE_LEVEL3-1
};

static int __value_offset_search(unsigned int kv_key, unsigned int* kv_lba, unsigned int* kv_length);
static void CompactMemTable();
static unsigned int MaybeDoCompaction();
static void DoCompaction(unsigned int victim_level);

extern AUTO_CPL_INFO auto_cpl_info[MAX_NUM_NVME_SLOT];

enum CMD_TYPE commandType[1024];
XTime st_read_log[AVAILABLE_OUNTSTANDING_REQ_COUNT];
XTime ed_read_log[AVAILABLE_OUNTSTANDING_REQ_COUNT];
struct STAT_PER_TYPE stat[LAST_CMD];

inline unsigned int GetTypefromCmdSlotTag (int cmdSlotTag) {
	return commandType[cmdSlotTag];
}

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	IO_READ_COMMAND_DW12 readInfo12;
	unsigned int startLba[2];
	unsigned int nlb;
	
	readInfo12.dword = nvmeIOCmd->dword[12];
	startLba[0] = nvmeIOCmd->dword[10] + BLOCK_BASE_LBA;
	// startLba[0] = nvmeIOCmd->dword[10];

	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;
	//xil_printf("NVMe Read Command: NLB=%u\r\n", nlb + 1);
	
	//xil_printf("---- NVMe Read Command ----\r\n");
	//xil_printf("Command Slot Tag: %u\r\n", cmdSlotTag);
	//xil_printf("NVMe Command DWORD[10]: 0x%X (Start LBA Low)\r\n", nvmeIOCmd->dword[10]);
	//xil_printf("NVMe Command DWORD[11]: 0x%X (Start LBA High)\r\n", nvmeIOCmd->dword[11]);
	//xil_printf("NVMe Command DWORD[12]: 0x%X (NLB & Control Bits)\r\n", nvmeIOCmd->dword[12]);
	//xil_printf("Start LBA: 0x%X %X\r\n", startLba[1], startLba[0]);
	//xil_printf("NLB (Number of Logical Blocks): %u (0x%X)\r\n", nlb, nlb);

	// xil_printf("read check: 0x%X < %X\r\n", startLba[0], storageCapacity_L);
	// if(startLba[0] >= storageCapacity_L)
	//	xil_printf("-----[KI]: lba is larger than capacity-----\r\n");
	
	ASSERT(startLba[0] < storageCapacity_L + BLOCK_BASE_LBA && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);
	//ASSERT(nlb < MAX_NUM_OF_NLB);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_READ);
}

void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	IO_READ_COMMAND_DW12 writeInfo12;
	NVME_COMPLETION nvmeCPL;
	unsigned int startLba[2];
	unsigned int nlb;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	startLba[0] = nvmeIOCmd->dword[10] + BLOCK_BASE_LBA;
	// startLba[0] = nvmeIOCmd->dword[10];

	//if (nvmeIOCmd->dword[10] >= BLOCK_BASE_LBA)
	//	xil_printf("++++++++>[KI] Block Region check: already in Block Region\r\n");
	// xil_printf("[KI]: Put to Block Region: 0x%x\r\n",startLba[0]);
	// xil_printf("write check: 0x%X < %X\r\n", startLba[0], storageCapacity_L);
	// if(startLba[0] >= storageCapacity_L)
	//	xil_printf("[KI]: addr to block region: exceed!\r\n");

	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;
	//xil_printf("NVMe Write Command: NLB=%u\r\n", nlb + 1);
	
	//xil_printf("---- NVMe Write Command ----\r\n");
	//xil_printf("Command Slot Tag: %u\r\n", cmdSlotTag);
	//xil_printf("NVMe Command DWORD[10]: 0x%X (Start LBA Low)\r\n", nvmeIOCmd->dword[10]);
	//xil_printf("NVMe Command DWORD[11]: 0x%X (Start LBA High)\r\n", nvmeIOCmd->dword[11]);
	//xil_printf("NVMe Command DWORD[12]: 0x%X (NLB & Control Bits)\r\n", nvmeIOCmd->dword[12]);
	//xil_printf("Start LBA: 0x%X %X\r\n", startLba[1], startLba[0]);
	//xil_printf("NLB (Number of Logical Blocks): %u (0x%X)\r\n", nlb, nlb);

	ASSERT(startLba[0] < storageCapacity_L + BLOCK_BASE_LBA && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);
	// ASSERT(nlb < MAX_NUM_OF_NLB);

	nvmeCPL.dword[0] = 0;
	nvmeCPL.specific = 0x0;
	
	//set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_WRITE);
}

/* Key-Value Feature Start! */
void handle_nvme_io_kv_get(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	
	NVME_COMPLETION nvmeCPL;
	unsigned int kv_key, kv_length, kv_lba, kv_nlb;
	int hit = 0;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	/* [KI] overlap -> non overlap */
	/*
	if(nvmeIOCmd->dword[10] >= BLOCK_BASE_LBA){
		//xil_printf("------>[KI] KV Region check: overlapping 0x%x || 0x%x\r\n", nvmeIOCmd->dword[10],BLOCK_BASE_LBA);
		nvmeIOCmd->dword[10] %= BLOCK_BASE_LBA;
		//xil_printf("------>[KI] KV Region re-check: +++after+++ 0x%x || 0x%x\r\n", nvmeIOCmd->dword[10],BLOCK_BASE_LBA);
	}
	*/
	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];

	nlb = readInfo12.NLB;
	kv_key = startLba[0];

#ifdef DEBUG_iLSM
	xil_printf("NVME_KV_GET COMMAND:: <Key, nlb> = <0x%x, %u>\n", kv_key, nlb);
#endif

	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	hit = __value_offset_search(kv_key, &kv_lba, &kv_length);

	if (hit) {
		kv_nlb = (kv_length / BYTES_PER_SECTOR) + ((kv_length % BYTES_PER_SECTOR) > 0 ? 1 : 0);
		//xil_printf("NVME_KV_GET HIT:: <Key, kv_lba, nlb, kv_nlb, kv_length> = <0x%x, %d, %d, %d, %d>\r\n", kv_key, kv_lba, nlb, kv_nlb, kv_length);
		//xil_printf("kv_nlb = %d, nlb = %d\r\n", kv_nlb, nlb);
		ASSERT(kv_nlb <= (nlb+1)); // not sure what will happen if we send more than buffer.

#ifdef DEBUG_iLSM
		xil_printf("NVME_KV_GET HIT:: <Key, kv_lba, nlb, kv_nlb, kv_length> = <0x%x, %d, %d, %d, %d>\r\n", kv_key, kv_lba, nlb, kv_nlb, kv_length);
#endif
		//xil_printf("NVME_KV_GET HIT:: <Key, BBL, kv_lba, nlb, kv_nlb, kv_length> = <@@@0x%x, 0x%x@@@, 0x%x, %d, %d, %d>\r\n", kv_key, BLOCK_BASE_LBA, kv_lba, nlb, kv_nlb, kv_length);
		stat[KV_GET].found++;
		//xil_printf("[KI] found amount: %d\r\n",stat[KV_GET].found);
		auto_cpl_info[cmdSlotTag].mark = 0xFF;
		// auto_cpl_info[cmdSlotTag].sqid = sqid;
		// auto_cpl_info[cmdSlotTag].cid = cid;
		auto_cpl_info[cmdSlotTag].numOfNvmeBlock = kv_nlb;
		auto_cpl_info[cmdSlotTag].sentNvmeBlock = 0;
		auto_cpl_info[cmdSlotTag].kv_length = kv_length;

		ReqTransNvmeToSlice(cmdSlotTag, kv_lba, kv_nlb-1, IO_NVM_READ); 
	} else {
		stat[KV_GET].lost++;
		//xil_printf("Failed to search Key 0x%x!, kv_lba: 0x%x, lost amount: %d\r\n", kv_lba, kv_key,stat[KV_GET].lost);

		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusField.SCT = 0x7;
		nvmeCPL.statusField.SC = 0xC1;
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	}
}

/////////////////////////////////////////// junhyeok
void set_auto_nvme_cpl_with_cid(unsigned int cmdSlotTag, unsigned int cid) {
    NVME_CPL_FIFO_REG nvmeReg;

    nvmeReg.cid = cid;
    nvmeReg.sqId = 0; // optional
    nvmeReg.specific = 0;
    nvmeReg.cmdSlotTag = cmdSlotTag;
    nvmeReg.statusFieldWord = 0;
    //nvmeReg.cplType = ONLY_CPL_TYPE;
    nvmeReg.cplType = AUTO_CPL_TYPE;

    //IO_WRITE32(NVME_CPL_FIFO_REG_ADDR, nvmeReg.dword[0]);
    IO_WRITE32((NVME_CPL_FIFO_REG_ADDR + 4), nvmeReg.dword[1]);
    IO_WRITE32((NVME_CPL_FIFO_REG_ADDR + 8), nvmeReg.dword[2]);
}

void handle_nvme_io_byteexpress(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
    NVME_COMPLETION nvmeCPL;
    nvmeCPL.dword[0] = 0;
    nvmeCPL.specific = 0x0;
    //set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);

    //unsigned int payload_len = nvmeIOCmd->dword[13]; // in bytes
    //unsigned int num_payload_sqe = (payload_len + 63) / 64;  // ceil(payload_len / 64)
    // total completions = 1 for main command + N for payload chunks
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
    //for (unsigned int i = 1; i <= num_payload_sqe; i++) {
    //    unsigned int slot = (cmdSlotTag + i) % (1 << P_SLOT_TAG_WIDTH);  // wraparound safety
        //set_auto_nvme_cpl_with_cid(slot, 0xFFFF);
    //    set_nvme_slot_release(slot);
	/*NVME_CPL_FIFO_REG nvmeReg;

        nvmeReg.specific = 0x0;
        nvmeReg.cmdSlotTag = slot;
        nvmeReg.statusFieldWord = 0x0;
        nvmeReg.cplType = AUTO_CPL_TYPE;

        IO_WRITE32(NVME_CPL_FIFO_REG_ADDR, nvmeReg.dword[0]);
        IO_WRITE32((NVME_CPL_FIFO_REG_ADDR + 4), nvmeReg.dword[1]);
        IO_WRITE32((NVME_CPL_FIFO_REG_ADDR + 8), nvmeReg.dword[2]);*/
    //}
}
/////////////////////////////////////////// junhyeok

void handle_nvme_io_kv_put(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int kv_key, kv_length, kv_nlb, kv_lba;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];
	
	/*
	else{
		xil_printf("------>[KI] KV Region check: non overlapping\r\n");
	}
	*/
	if(writeInfo12.FUA == 1)
		xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	//xil_printf("kv lba: 0x%x\r\n",startLba[0]);
	/*
	if(startLba[0] >= BLOCK_BASE_LBA){
		xil_printf("------>[KI] KV Region check: overlapping 0x%x || 0x%x\r\n", startLba[0],BLOCK_BASE_LBA);
	//nvmeIOCmd->dword[10] %= BLOCK_BASE_LBA;
	//xil_printf("------>[KI] KV Region re-check: +++after+++ 0x%x || 0x%x\r\n", nvmeIOCmd->dword[10],BLOCK_BASE_LBA);
	}
	*/
	startLba[1] = nvmeIOCmd->dword[11];

	nlb = writeInfo12.NLB;

	kv_key = startLba[0];
	kv_length = nvmeIOCmd->dword[13];
	kv_nlb = nlb + 1;
	kv_lba = value_log_lba;
	ASSERT(kv_nlb == (kv_length / BYTES_PER_SECTOR) +((kv_length % BYTES_PER_SECTOR) > 0 ? 1 : 0));

	/*if (skiphead->count == MAX_SKIPLIST_NODE) 
		CompactMemTable();	

	SKIPLIST_NODE* ret = skiplist_insert(skiphead, kv_key, kv_lba, kv_length);

	if (ret != SKIPLIST_NIL) {
		skiplist_remove(skiphead, kv_key);
		ret = skiplist_insert(skiphead, kv_key, kv_lba, kv_length);
		ASSERT(ret == SKIPLIST_NIL);
	}*/

	//value_log_lba += kv_nlb;
	ASSERT(value_log_lba < BLOCK_BASE_LBA);

//#ifdef DEBUG_iLSM
	//xil_printf("NVME_KV_PUT COMMAND:: <Key, LBA, size> = <0x%x, %u, %u, %u>\r\n", kv_key, kv_lba, kv_length, kv_nlb);
//#endif

	// Write to Value Log
	ReqTransNvmeToSlice(cmdSlotTag, kv_lba, kv_nlb - 1, IO_NVM_WRITE);
//xil_printf("DEBUG: in kv put handler");
        //NVME_COMPLETION nvmeCPL;
        //nvmeCPL.dword[0] = 0;
        //nvmeCPL.specific = 0x0;
        //set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
}



void handle_nvme_io_iter_create (unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {

	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	
	NVME_COMPLETION nvmeCPL;
	unsigned int kv_key, kv_length, kv_lba, kv_nlb;
	unsigned int iter_id = 0x7fff;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	// xil_printf("NVME_ITER_CREATE COMMAND\n", kv_key);

	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	iter_id = allocate_iterator();
	
	if (iter_id != 0x7fff) {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.specific = iter_id;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
		// set_nvme_cpl(sqid, cid, nvmeCPL.specific, nvmeCPL.statusFieldWord);

#ifdef DEBUG_iLSM
		xil_printf("PRP1[0]:0x%x, PRP1[1]:0x%x, PRP2[0]:0x%x, PRP2[1]:0x%x\n", nvmeIOCmd->PRP1[0], nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP2[0], nvmeIOCmd->PRP2[1]);
		xil_printf("Iterator %d is allocated\n", iter_id);
#endif
	} else {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusField.SCT = 0x7;
		nvmeCPL.statusField.SC = 0xC1;
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	}
}

void handle_nvme_io_iter_delete(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd){

	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	
	NVME_COMPLETION nvmeCPL;
	unsigned int iter_id = 0x7fff;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;
	iter_id = nvmeIOCmd->dword[13];

	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	// autoCPLMark[cmdSlotTag] = NVME_COMMAND_AUTO_COMPLETION_OFF;
	unsigned int res = delete_iterator(iter_id);
	
	if (res != 0x7fff) {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.specific = iter_id;
		// set_nvme_cpl(sqid, cid, nvmeCPL.specific, nvmeCPL.statusFieldWord);
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
#ifdef DEBUG_iLSM
		xil_printf("PRP1[0]:0x%x, PRP1[1]:0x%x, PRP2[0]:0x%x, PRP2[1]:0x%x\n", nvmeIOCmd->PRP1[0], nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP2[0], nvmeIOCmd->PRP2[1]);
		xil_printf("Iterator %d is deleted\n", iter_id);
#endif
	} else {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusField.SCT = 0x7;
		nvmeCPL.statusField.SC = 0xC1;
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	}
}

void handle_nvme_io_iter_seek(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {

	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	
	NVME_COMPLETION nvmeCPL;
	unsigned int iter_id, start_key;
	unsigned int kv_key, kv_length, kv_lba, kv_nlb;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];	
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;
	
	start_key = startLba[0];
	iter_id = nvmeIOCmd->dword[13];


	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

#ifdef DEBUG_iLSM
//	xil_printf("NVME_KV_SEEK COMMAND:: <cmdSlotTag, autoCPLMark[%u]> = <%u, %u>\n", cmdSlotTag, cmdSlotTag, autoCPLMark[cmdSlotTag]);
	xil_printf("NVME_KV_SEEK COMMAND:: <iter_id, start_key> = <%d, %u>\n", iter_id, start_key);
#endif

	unsigned int hit = iterator_seek(iter_id, start_key, &kv_key, &kv_lba, &kv_length);

	if (hit) {
		kv_nlb = (kv_length / BYTES_PER_SECTOR) + ((kv_length % BYTES_PER_SECTOR) > 0 ? 1 : 0);	

#ifdef DEBUG_iLSM
		xil_printf("SEEK:: Found <iter_id, Key, kv_lba, kv_length> = <%d, %u, %u, %u>\n", iter_id, kv_key, kv_lba, kv_length);
#endif
		ASSERT(kv_nlb <= (nlb+1)); // not sure what will happen if we send more than buffer.

		stat[ITER_SEEK].found++;
		auto_cpl_info[cmdSlotTag].mark = 0xFE;
		// auto_cpl_info[cmdSlotTag].sqid = sqid;
		// auto_cpl_info[cmdSlotTag].cid = cid;
		auto_cpl_info[cmdSlotTag].numOfNvmeBlock = kv_nlb;
		auto_cpl_info[cmdSlotTag].sentNvmeBlock = 0;
		auto_cpl_info[cmdSlotTag].kv_length = kv_length;

		ReqTransNvmeToSlice(cmdSlotTag, kv_lba, kv_nlb-1, IO_NVM_READ);
	} else {
#ifdef DEBUG_iLSM
		xil_printf("No Key in the range(>= %u) %u!\n", start_key);	
#endif
		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusField.SCT = 0x7;
		nvmeCPL.statusField.SC = 0xC1;
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	}
}

void handle_nvme_io_iter_next(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd){

	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	
	NVME_COMPLETION nvmeCPL;
	unsigned int iter_id;
	unsigned int kv_key, kv_length, kv_lba, kv_nlb;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;
	iter_id = nvmeIOCmd->dword[13];
	
	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	unsigned int hit = iterator_next(iter_id, &kv_key, &kv_lba, &kv_length);

	if (hit) {
		kv_nlb = (kv_length / BYTES_PER_SECTOR) + ((kv_length % BYTES_PER_SECTOR) > 0 ? 1 : 0);	
#ifdef DEBUG_iLSM
		xil_printf("NVME_KV_NEXT COMMAND:: <iter_id, kv_key, kv_lba, kv_nlb, kv_length> = <%u, %u, %u, %u, %u>\n", iter_id, kv_key, kv_lba, kv_nlb, kv_length);
#endif

		ASSERT(kv_nlb <= (nlb+1)); // not sure what will happen if we send more than buffer.

		stat[ITER_NEXT].found++;
		auto_cpl_info[cmdSlotTag].mark = 0xFD;
		// auto_cpl_info[cmdSlotTag].sqid = sqid;
		// auto_cpl_info[cmdSlotTag].cid = cid;
		auto_cpl_info[cmdSlotTag].numOfNvmeBlock = kv_nlb;
		auto_cpl_info[cmdSlotTag].sentNvmeBlock = 0;
		auto_cpl_info[cmdSlotTag].kv_length = kv_length;

		ReqTransNvmeToSlice(cmdSlotTag, kv_lba, kv_nlb-1, IO_NVM_READ);
	} else {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusField.SCT = 0x7;
		nvmeCPL.statusField.SC = 0xC1;
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
	}
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;
	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	//xil_printf("OPC = 0x%X\r\n", nvmeIOCmd->OPC);
	//xil_printf("PRP1[63:32] = 0x%X, PRP1[31:0] = 0x%X\r\n", nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP1[0]);
	//xil_printf("PRP2[63:32] = 0x%X, PRP2[31:0] = 0x%X\r\n", nvmeIOCmd->PRP2[1], nvmeIOCmd->PRP2[0]);
	//xil_printf("dword10 = 0x%X\r\n", nvmeIOCmd->dword10);
	//xil_printf("dword11 = 0x%X\r\n", nvmeIOCmd->dword11);
	//xil_printf("dword12 = 0x%X\r\n", nvmeIOCmd->dword12);

	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case IO_NVM_FLUSH:
		{
        	//xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		case IO_NVM_WRITE:
		{	
			//xil_printf("[KI]: Block Region Put\r\n");

			//xil_printf("IO Write Command\r\n");
			
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
			//xil_printf("[KI]: Block Region Get\r\n");
			//xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_WRITE_ZEROES:
		{
			xil_printf("IO Write Zeroes skip\r\n");
			break;
		}
		
		/*
		 * Key-Value Commands
		 * Point Query : KV_PUT, KV_GET, KV_DELETE
		 * Range Query : ITER_CREATE, KV_SEEK, KV_NEXT, ITER_DELETE 
		 */
		case IO_NVM_KV_PUT:
		{
			//xil_printf("[KI]: KV Region Put\r\n");

			// xil_printf("KV Put Command \r\n");
			// commandType[nvmeCmd->cmdSlotTag] = KV_PUT;
			// stat[KV_PUT].c++;
			//xil_printf("before kv put handler");
			handle_nvme_io_kv_put(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			
			//unsigned int retry;
			//do {
			//	retry = MaybeDoCompaction();
			//} while(retry == 1);
			
			break;
		}
		////////////////////// junhyeok
		case IO_NVM_BYTEEXPRESS:
		{
			handle_nvme_io_byteexpress(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		////////////////////// junhyeok
		case IO_NVM_KV_GET:
		{
			//xil_printf("[KI]: KV Region Get\r\n");
			commandType[nvmeCmd->cmdSlotTag] = KV_GET;
			stat[KV_GET].c++;
			//xil_printf("[KI]: key found: %d\r\n",stat[KV_GET].c);

			XTime st, ed;
			XTime_GetTime(&st);
			handle_nvme_io_kv_get(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
			XTime_GetTime(&ed);		
			stat[KV_GET].TOTAL_TIME += ed-st;
			break;
		}
		case IO_NVM_KV_DELETE:
		{
			// xil_printf("KV Delete Command \r\n");
			// handle_nvme_io_kv_delete(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_KV_ITER_CREATE:
		{
			// xil_printf("KV Iterator Create Command \r\n");
			commandType[nvmeCmd->cmdSlotTag] = ITER_CREATE;
			stat[ITER_CREATE].c++;

			XTime st, ed;
			XTime_GetTime(&st);
			handle_nvme_io_iter_create(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
			XTime_GetTime(&ed);		
			stat[ITER_CREATE].TOTAL_TIME += ed-st;
			break;
		}
		case IO_NVM_KV_ITER_SEEK:
		{
			// xil_printf("KV Iterator Seek Command \r\n");
			commandType[nvmeCmd->cmdSlotTag] = ITER_SEEK;
			stat[ITER_SEEK].c++;

			XTime st, ed;
			XTime_GetTime(&st);
			handle_nvme_io_iter_seek(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
			XTime_GetTime(&ed);
			stat[ITER_SEEK].TOTAL_TIME += ed-st;

			break;
		}
		case IO_NVM_KV_ITER_NEXT:
		{
			commandType[nvmeCmd->cmdSlotTag] = ITER_NEXT;
			stat[ITER_NEXT].c++;

			XTime st, ed;
			XTime_GetTime(&st);			
			handle_nvme_io_iter_next(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
			XTime_GetTime(&ed);			
			stat[ITER_NEXT].TOTAL_TIME += ed-st;

			break;
		}
		case IO_NVM_KV_ITER_DELETE:
		{
			// xil_printf("KV Iterator Delete Command \r\n");
			commandType[nvmeCmd->cmdSlotTag] = ITER_DESTROY;
			stat[ITER_DESTROY].c++;

			XTime st, ed;
			XTime_GetTime(&st);
			handle_nvme_io_iter_delete(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
			XTime_GetTime(&ed);
			stat[ITER_DESTROY].TOTAL_TIME += ed-st;

			break;
		}
		case PRINT_TIME:
		{
			printf("\n\n================================================\n");
			for (int i=KV_PUT; i<LAST_CMD; i++) {
				if (stat[i].c != 0) {
					double total, dev_compute, dev_memcpy, nand_sstable, nand_log;
					total = 1.0 * (stat[i].TOTAL_TIME / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
					dev_compute  = 1.0 * ((stat[i].TOTAL_TIME - stat[i].NAND_READ_SSTABLE - stat[i].DEVICE_MEMCPY) / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
					dev_memcpy   = 1.0 * ( stat[i].DEVICE_MEMCPY / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
					nand_sstable = 1.0 * (stat[i].NAND_READ_SSTABLE / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
					nand_log     = 1.0 * (stat[i].NAND_READ_LOG / stat[i].found) / (COUNTS_PER_SECOND / 1000000);

					switch (i) {
						case KV_PUT:
							printf("To be filled\n");
							break;
						case KV_GET:
							printf("%u GET(%u found) took on average %.3lf us (Compute %.3lf + NAND Read[SSTable] %.3lf us) +  NAND Read[Value Log] %.3lf us (hit %u out of %u) \n", stat[i].c, stat[i].found, total, dev_compute, nand_sstable, nand_log, stat[i].hit, stat[i].c);
							break;
						case KV_DEL:
							printf("To be filled\n");
							break;
						case ITER_CREATE:
							printf("%u Create took on average %.3lf us = Compute %.3lf us + Memory Copy %.3lf us\n", stat[i].c, total, dev_compute, dev_memcpy);
							break;
						case ITER_SEEK:
							printf("%u SEEK(%u found) took on average %.3lf us (Compute %.3lf us + NAND Read[SSTable] %.3lf) + NAND Read[Value Log] %.3lf us (hit %u out of %u) \n", stat[i].c, stat[i].found, total, dev_compute, nand_sstable, nand_log, stat[i].hit, stat[i].c);
							break;
						case ITER_NEXT:
							printf("%u NEXT(%u found) took on average %.3lf us (Compute %.3lf us + NAND Read[SSTable] %.3lf) + NAND Read[Value Log] %.3lf us (hit %u out of %u) \n", stat[i].c, stat[i].found, total, dev_compute, nand_sstable, nand_log, stat[i].hit, stat[i].c);
							break;
						case ITER_DESTROY:
							printf("%u DELETE took on average %.3lf us\n", stat[i].c, total);
							break;
					}
				}

				stat[i].c = stat[i].found = stat[i].hit = 0;
				stat[i].TOTAL_TIME = stat[i].NAND_READ_SSTABLE = stat[i].NAND_READ_LOG = stat[i].DEVICE_MEMCPY = 0;
			}

			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X, Tag: %X\r\n", opc, nvmeCmd->cmdSlotTag);
			//set_auto_nvme_cpl_with_cid(nvmeCmd->cmdSlotTag, 0xFFFF);
    			/*NVME_COMPLETION nvmeCPL;
    			nvmeCPL.dword[0] = 0;
    			nvmeCPL.specific = 0x0;
    			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);*/
			set_nvme_slot_release(nvmeCmd->cmdSlotTag);
			//ASSERT(0);
			break;
		}
	}
}

void CompactMemTable()
{
#ifdef DEBUG_iLSM
	xil_printf("Start Compacting MemTable!\n");
#endif
	SSTABLE_INDEX_NODE* sst_index = (SSTABLE_INDEX_NODE*) SSTABLE_INDEX_BUFFER;
	SSTABLE_DATA_NODE* sst_data = (SSTABLE_DATA_NODE*) SSTABLE_DATA_BUFFER;

	unsigned int cur_lpn = sst_log_lpn;
	unsigned int cnt = 0;

	/* Why do we need this part?
	for(int idx = 0; idx<(skiphead->total_value_size); idx++){
		unsigned int tempLpn = (skiphead->st_lba+idx) / SECTOR_NUM_PER_PAGE;
		unsigned int hitEntry = CheckBufHit(tempLpn);
		if(hitEntry != 0x7fff && bufMap->bufEntry[hitEntry].dirty) {
			BUFFER_REQ_INFO bufferCmd;
			bufferCmd.lpn = bufMap->bufEntry[hitEntry].lpn;
			bufferCmd.bufferEntry  = hitEntry;
			PmWrite(&bufferCmd);
		}
	}
	*/

	// Build Table and and Save Bloom Filter
	SKIPLIST_NODE** cur = skiphead->forward;
	memset((char *)BMF_BUFFER_ADDR, 0, 16384);
	char* loc = (char *)BMF_BUFFER_ADDR;
		
	while (cur[0] != SKIPLIST_NIL ) {

		// Build Bloom Filter
		unsigned int h = simple_hash(cur[0]->key, SEED1);
		const unsigned int delta = (h >> 17) | (h << 15);
		for (int k=0; k<HASH_NUM; k++) {
			const unsigned int bitpos = h % (16*1024*8);
			loc[bitpos/8] |= (1 << (bitpos % 8));
			h += delta;
		}

		// Build Table
		sst_index[cnt].key= cur[0]->key;
		sst_data[cnt].lba = cur[0]->lba;
		sst_data[cnt++].length = cur[0]->length;
		cur = cur[0]->forward;
		if (cnt > 4096) {
			xil_printf("something is going bad...");
			assert(0);
		}
	}	
		
	// Superblock Update
#ifdef DEBUG_iLSM
	xil_printf("Node Counts=%u : Start Building Table ...\n", cnt);
#endif
	{
		unsigned int offset;

		// xil_printf("start_lpn=%u ", cur_lpn);
		unsigned int bmf_size = 1;
		unsigned int index_size = (sizeof(struct _SSTABLE_INDEX_NODE) * cnt) / BYTES_PER_DATA_REGION_OF_SLICE;
		unsigned int data_size = (sizeof(struct _SSTABLE_DATA_NODE) * cnt) / BYTES_PER_DATA_REGION_OF_SLICE;

		// Bloom Filter Flush
		TriggerInternalDataWrite(cur_lpn, BMF_BUFFER_ADDR, BYTES_PER_DATA_REGION_OF_SLICE);
		super_sstable_list_level[0][super_sstable_list[0].tail].bloomfilter_size = bmf_size;
		cur_lpn += bmf_size;

		TriggerInternalPagesWrite(cur_lpn, SSTABLE_INDEX_BUFFER, index_size);
		super_sstable_list_level[0][super_sstable_list[0].tail].index_size = index_size;
		cur_lpn += index_size;

		TriggerInternalPagesWrite(cur_lpn, SSTABLE_DATA_BUFFER, data_size);
		super_sstable_list_level[0][super_sstable_list[0].tail].data_size = data_size;
		cur_lpn += data_size;

#ifdef DEBUG_iLSM
		xil_printf("bmf_size=%u, index_size=%u, data_size=%u, cur_lpn=%u\n", 1, index_size, data_size, cur_lpn);
#endif
		
		/*
		// Index Block Flush
		for (offset=0; offset<sizeof(struct _SSTABLE_INDEX_NODE)*cnt; offset+=BYTES_PER_DATA_REGION_OF_SLICE) 
			TriggerInternalDataWrite(cur_lpn++, SSTABLE_INDEX_BUFFER + offset, BYTES_PER_DATA_REGION_OF_SLICE);
		super_sstable_list_level[0][super_sstable_list[0].tail].index_size = offset / BYTES_PER_DATA_REGION_OF_SLICE;

		// xil_printf("DEBUG : index size = %u\n", super_sstable_list_level[0][super_sstable_list[0].tail].index_size);

		// Data Block Flusha
		for (offset=0; offset<sizeof(struct _SSTABLE_DATA_NODE)*cnt; offset+=BYTES_PER_DATA_REGION_OF_SLICE) 
			TriggerInternalDataWrite(cur_lpn++, SSTABLE_DATA_BUFFER + offset, BYTES_PER_DATA_REGION_OF_SLICE);
		super_sstable_list_level[0][super_sstable_list[0].tail].data_size = (cur_lpn - sst_log_lpn) - super_sstable_list_level[0][super_sstable_list[0].tail].index_size - super_sstable_list_level[0][super_sstable_list[0].tail].bloomfilter_size;
		*/
		
		// Update Superblock
		super_sstable_list_level[0][super_sstable_list[0].tail].level = 0;
		super_sstable_list_level[0][super_sstable_list[0].tail].head_lpn = sst_log_lpn;
		super_sstable_list_level[0][super_sstable_list[0].tail].tail_lpn = cur_lpn - 1;
		super_sstable_list_level[0][super_sstable_list[0].tail].total_entry = cnt;
		super_sstable_list_level[0][super_sstable_list[0].tail].min_key = sst_index[0].key;
		super_sstable_list_level[0][super_sstable_list[0].tail].max_key = sst_index[cnt-1].key;
		super_level_info->level_count[0]++;
#ifdef DEBUG_iLSM
		xil_printf("SSTable Info : min=%d, max=%d, head_lpn=%d, tail_lpn=%d\r\n", super_sstable_list_level[0][super_sstable_list[0].tail].min_key, super_sstable_list_level[0][super_sstable_list[0].tail].max_key, super_sstable_list_level[0][super_sstable_list[0].tail].head_lpn, super_sstable_list_level[0][super_sstable_list[0].tail].tail_lpn);
		xil_printf("SSTable Info : cnt=%d, level=%d, level_count=%d\r\n", super_sstable_list_level[0][super_sstable_list[0].tail].total_entry, super_sstable_list_level[0][super_sstable_list[0].tail].level, super_level_info->level_count[0]);
#endif
		
		super_sstable_list[0].tail = (super_sstable_list[0].tail + 1) % MAX_SSTABLE_LEVEL0;
		ASSERT(super_sstable_list[0].tail != super_sstable_list[0].head);
	}

	// SSTable log tail update
	sst_log_lpn = cur_lpn;

	// Skiplist Initialization
	initSkipList(skiphead);
	skiphead->st_lba = value_log_lba;
	
#ifdef DEBUG_iLSM
	xil_printf("Start Sync!!\n");
#endif
	// SyncAllLowLevelReqDone();
	
	// printSSTableInfo();
#ifdef DEBUG_iLSM
	xil_printf("Compacting MemTable Done!\n");
#endif
}

static int __value_offset_search(unsigned int kv_key, unsigned int* kv_lba, unsigned int* kv_length)
{
	XTime st, ed;

	int hit = 0;
	static int one_time = 0;

	{
		SKIPLIST_NODE* ret = skiplist_search(skiphead, kv_key);
		if (ret != SKIPLIST_NIL) {
			*kv_lba = ret->lba;
			*kv_length = ret->length;
			hit = 1;
		}
	}

	if (!hit)
	{
		const unsigned int h_origin = (simple_hash(kv_key, SEED1));
		const unsigned int delta = (h_origin >> 17) | (h_origin << 15);

		for (int level=0; level<=3 && hit==0; level++) {
			if(super_level_info->level_count[level] == 0)
				continue;

			for (unsigned int i = super_sstable_list[level].head;
					i != super_sstable_list[level].tail;
					i = (i+1) % MAX_SSTABLE_LEVEL[level]) {
				unsigned int min, max;
				min = super_sstable_list_level[level][i].min_key;
				max = super_sstable_list_level[level][i].max_key;
				// xil_printf("kv_key is %d, min is %d, max is %d\n", kv_key, min, max);

				if (kv_key < min || kv_key > max)
					continue;

#ifdef DEBUG_iLSM
				xil_printf("about to read bmf\n");
#endif
				unsigned int bmf_lpn = super_sstable_list_level[level][i].head_lpn;
				unsigned int index_size = super_sstable_list_level[level][i].index_size;
				unsigned int data_size = super_sstable_list_level[level][i].data_size;
				unsigned int bmf_size = super_sstable_list_level[level][i].bloomfilter_size;
				XTime_GetTime(&st);
				TriggerInternalPagesRead(bmf_lpn, BMF_BUFFER_ADDR, bmf_size);
				SyncAllLowLevelReqDone();
				XTime_GetTime(&ed);
				stat[KV_GET].NAND_READ_SSTABLE += ed-st;
#ifdef DEBUG_iLSM
				xil_printf("about to check bmf\n");
#endif
				int bmf_hit = 1;
				char* loc = (char *) BMF_BUFFER_ADDR;
				unsigned int h = h_origin;
				for (int k=0; bmf_hit == 1 && k<HASH_NUM; k++) {
					unsigned int bitpos = h % (16 * 1024 * 8 * bmf_size);
					if ((loc[bitpos/8] & (1 << (bitpos%8))) == 0)
						bmf_hit = 0;
					h += delta;
				}

				if(bmf_hit == 1) {
#ifdef DEBUG_iLSM
					xil_printf("bmf filtering pass! here?\n");
					xil_printf("head_lpn : %d, <index, data, bmf size> = <%d, %d, %d>\n", bmf_lpn, index_size, data_size, bmf_size);
#endif
					XTime_GetTime(&st);
					TriggerInternalPagesRead(bmf_lpn+bmf_size, SSTABLE_INDEX_BUFFER, index_size);
					SyncAllLowLevelReqDone();
					XTime_GetTime(&ed);
					stat[KV_GET].NAND_READ_SSTABLE += ed-st;

					{ // Binary Search to find the key
#ifdef DEBUG_iLSM
						xil_printf("before binary search?\n");
#endif
						SSTABLE_INDEX_NODE* index = (SSTABLE_INDEX_NODE*) SSTABLE_INDEX_BUFFER;
						unsigned int lo = 0;
						unsigned int hi = super_sstable_list_level[level][i].total_entry - 1;
						unsigned int mid;
						
						while (lo <= hi) {
							mid = (lo + hi) / 2;
							// xil_printf("<lo, mid, hi> = <%d, %d, %d>", lo, mid, hi);
							if (index[mid].key < kv_key)
								lo = mid + 1;
							else if (index[mid].key > kv_key)
								hi = mid - 1;
							else {
								hit = 1;
								break;
							}
						}

						if (hit == 1) {
#ifdef DEBUG_iLSM
							xil_printf("before data?\n");
#endif
							XTime_GetTime(&st);
							TriggerInternalPagesRead(bmf_lpn+bmf_size+index_size, SSTABLE_DATA_BUFFER, data_size);
							SyncAllLowLevelReqDone();
							XTime_GetTime(&ed);
							stat[KV_GET].NAND_READ_SSTABLE += ed-st;
							SSTABLE_DATA_NODE* data = (SSTABLE_DATA_NODE*) SSTABLE_DATA_BUFFER;
							*kv_lba = data[mid].lba;
							*kv_length = data[mid].length;
							break;
						}
					}
				}
			}
		}
	}

	// xil_printf("all done!\n");
	return hit;
}

// Without Data Buffer [OLD]
void TriggerInternalDataWrite(const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize)
{
	unsigned int virtualSliceAddr = AddrTransWrite(lsa);
	unsigned int reqSlotTag = GetFromFreeReqQ();

	if (bufSize == BYTES_PER_DATA_REGION_OF_SLICE) { 
		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);
	}
	else 
	{
		assert(!"multiple internal page write not supported yet!");
	}
}

void TriggerInternalDataRead (const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize)
{
	unsigned int virtualSliceAddr = AddrTransRead(lsa);
	unsigned int reqSlotTag = GetFromFreeReqQ();

	if (bufSize == BYTES_PER_DATA_REGION_OF_SLICE) {
		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR_NO_SPARE;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);
	}
	else
	{
		assert(!"multiple internal page read not supported yet!!");
	}
}


// // With Data Buffer [Optimizied]
// void TriggerInternalDataWrite(const unsigned int logicalSliceAddr, const unsigned int bufAddr, const unsigned int bufSize)
// {
// 	unsigned int dataBufEntry;

// 	//allocate a data buffer entry for this request
// 	dataBufEntry = CheckDataBufHitWithLSA(logicalSliceAddr);

// 	if (dataBufEntry == DATA_BUF_FAIL) {
// 		//data buffer miss, allocate a new buffer entry
// 		dataBufEntry = AllocateDataBuf();

// 		//clear the allocated data buffer entry being used by previous requests
// 		EvictDataBufEntryForMemoryCopy(dataBufEntry);

// 		//update meta-data of the allocated data buffer entry
// 		dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = logicalSliceAddr;
// 		PutToDataBufHashList(dataBufEntry);

// 		// For Eviction to be completed	
// 		SyncAllLowLevelReqDone();
// 	}

// 	dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;

// 	// Get Data Buffer Address
// 	unsigned int DataBufAddress = (DATA_BUFFER_BASE_ADDR + dataBufEntry * BYTES_PER_DATA_REGION_OF_SLICE);
// 	memcpy((void*)DataBufAddress, (void*)bufAddr, BYTES_PER_DATA_REGION_OF_SLICE);
// }

// void TriggerInternalDataRead (const unsigned int logicalSliceAddr, const unsigned int bufAddr, const unsigned int bufSize)
// {

// 	unsigned int reqSlotTag, dataBufEntry, virtualSliceAddr;

// 	//allocate a data buffer entry for this request
// 	dataBufEntry = CheckDataBufHitWithLSA(logicalSliceAddr);

// 	if (dataBufEntry == DATA_BUF_FAIL) {
// 		//data buffer miss, allocate a new buffer entry
// 		dataBufEntry = AllocateDataBuf();

// 		//clear the allocated data buffer entry being used by previous requests
// 		EvictDataBufEntryForMemoryCopy(dataBufEntry);

// 		//update meta-data of the allocated data buffer entry
// 		dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = logicalSliceAddr;
// 		PutToDataBufHashList(dataBufEntry);
// 		SyncAllLowLevelReqDone();

// 		// Wait for Eviction to be completed
// 		virtualSliceAddr =  AddrTransRead(logicalSliceAddr);

// 		if(virtualSliceAddr != VSA_FAIL)
// 		{
// 			reqSlotTag = GetFromFreeReqQ();

// 			reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
// 			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
// 			// reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
// 			reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = logicalSliceAddr;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
// 			reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
// 			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
// 			UpdateDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
// 			reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

// 			SelectLowLevelReqQ(reqSlotTag);
// 			SyncAllLowLevelReqDone();
// 		} else {
// 			assert(!"VSA_FAIl happens!\n");
// 		}
// 	}

// 	// Get Data Buffer Address
// 	unsigned int DataBufAddress = (DATA_BUFFER_BASE_ADDR + dataBufEntry * BYTES_PER_DATA_REGION_OF_SLICE);
// 	memcpy((void*)bufAddr, (void*)DataBufAddress, BYTES_PER_DATA_REGION_OF_SLICE);
// }

void TriggerInternalPagesRead (const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages)
{
	unsigned int i = 0;
	unsigned int offset = 0;
	unsigned int size = BYTES_PER_DATA_REGION_OF_SLICE * numPages;
    for (i=0, offset=0; offset < size; i++, offset+=BYTES_PER_DATA_REGION_OF_SLICE) 
        TriggerInternalDataRead(startLsa+i, bufAddr + offset, BYTES_PER_DATA_REGION_OF_SLICE);
}

void TriggerInternalPagesWrite (const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages)
{
	unsigned int i = 0;
	unsigned int offset = 0;
	unsigned int size = BYTES_PER_DATA_REGION_OF_SLICE * numPages;
    for (i=0, offset=0; offset < size; i++, offset+=BYTES_PER_DATA_REGION_OF_SLICE) 
        TriggerInternalDataWrite(startLsa+i, bufAddr + offset, BYTES_PER_DATA_REGION_OF_SLICE);
}

static unsigned int MaybeDoCompaction() 
{
	unsigned int victim_level = 0x7fff;

	for(int level=2; level>=0; level--){
		// Compaction Trigger Condition
		if(super_level_info->level_count[level] >= 2
				&& super_level_info->level_count[level] >= COMPACTION_THRESHOLD_LEVEL[level]) {
			victim_level = level;
			break;
		}
	}

	if (victim_level == 0x7fff) 
		return 0;
	else {
		DoCompaction(victim_level);
		return 1;
	}	
}

static void DoCompaction(unsigned int victim_level)
{
	unsigned int s1 = 0x7fff;
	unsigned int s2 = 0x7fff;

#ifdef DEBUG_iLSM
	xil_printf("\r\n----------- COMPACTION START -----------\r\n");
#endif
	s1 = super_sstable_list[victim_level].head;
	s2 = (super_sstable_list[victim_level].head + 1) % MAX_SSTABLE_LEVEL[victim_level];
	ASSERT(s1 != 0x7fff);
	ASSERT(s2 != 0x7fff);

	// load index
	SUPER_SSTABLE_INFO* sstable_info1 = &super_sstable_list_level[victim_level][s1];
	SUPER_SSTABLE_INFO* sstable_info2 = &super_sstable_list_level[victim_level][s2];
	TriggerInternalPagesRead(sstable_info1->head_lpn + sstable_info1->bloomfilter_size,
			CP_INDEX_BUFFER1_ADDR,
			sstable_info1->index_size);
	TriggerInternalPagesRead(sstable_info2->head_lpn + sstable_info2->bloomfilter_size,
			CP_INDEX_BUFFER2_ADDR,
			sstable_info2->index_size);
	TriggerInternalPagesRead(sstable_info1->head_lpn + sstable_info1->bloomfilter_size + sstable_info1->index_size,
			CP_DATA_BUFFER1_ADDR,
			sstable_info1->data_size);
	TriggerInternalPagesRead(sstable_info2->head_lpn + sstable_info2->bloomfilter_size + sstable_info2->index_size,
			CP_DATA_BUFFER2_ADDR,
			sstable_info2->data_size);
	SyncAllLowLevelReqDone();

	// sorting to sstable index buffer
	SSTABLE_INDEX_NODE* sstable_index = (SSTABLE_INDEX_NODE*)SSTABLE_INDEX_BUFFER;
	SSTABLE_DATA_NODE* sstable_data = (SSTABLE_DATA_NODE*)SSTABLE_DATA_BUFFER;
	int total_entry = 0;
	{
		SSTABLE_INDEX_NODE* victim_index1 = (SSTABLE_INDEX_NODE*)CP_INDEX_BUFFER1_ADDR;
		SSTABLE_INDEX_NODE* victim_index2 = (SSTABLE_INDEX_NODE*)CP_INDEX_BUFFER2_ADDR;
		SSTABLE_DATA_NODE* victim_data1 = (SSTABLE_DATA_NODE*)CP_DATA_BUFFER1_ADDR;
		SSTABLE_DATA_NODE* victim_data2 = (SSTABLE_DATA_NODE*)CP_DATA_BUFFER2_ADDR;
		unsigned int v1 = 0, v2 = 0;
		while(v1 < sstable_info1->total_entry || v2 < sstable_info2->total_entry) {
			unsigned int from = 0;
			unsigned int skip = 0;
			if(v1 == sstable_info1->total_entry){
				from = 2;
			} else if (v2 == sstable_info2->total_entry) {
				from = 1;
			} else {
				if(victim_index1[v1].key < victim_index2[v2].key) {
					from = 1;
				} else if(victim_index1[v1].key > victim_index2[v2].key) {
					from = 2;
				} else {
					from = 2; // v2 is latest
					skip = 1;
				}
			}
			if(from == 1) {
				sstable_index[total_entry].key = victim_index1[v1].key;
				sstable_data[total_entry].lba = victim_data1[v1].lba;
				sstable_data[total_entry].length = victim_data1[v1].length;
				v1++;
			} else {
				sstable_index[total_entry].key = victim_index2[v2].key;
				sstable_data[total_entry].lba = victim_data2[v2].lba;
				sstable_data[total_entry].length = victim_data2[v2].length;
				v2++;
			}

			if(skip==1){
				v1++; // overlapping value. use latest (v2).
			}
			total_entry++;
		}

	}
	unsigned int new_level = victim_level + 1;
	unsigned int sst_lpn = sst_log_lpn;
	unsigned int bloomfilter_size = sstable_info1->bloomfilter_size * 2;
	unsigned int index_size;
	unsigned int data_size;
	{
		// SSTABLE INFO FLUSH, stlpn, edlpn, bloomfilter size, index size, level, ...
		// BUILD BLOOMFILTER and INDEX
		memset( (char*)BMF_BUFFER_ADDR, 0, 16384 * bloomfilter_size);

		char* loc = (char *)BMF_BUFFER_ADDR;
		for(int i=0;i<total_entry;i++) {
			unsigned int h = (simple_hash(sstable_index[i].key, SEED1));
			const unsigned int delta = (h >> 17) | (h<<15);
			for(int k=0;k<HASH_NUM;k++) {
				const unsigned int bitpos = h % (16*1024*8 * bloomfilter_size);
				loc[bitpos/8] |= (1<<(bitpos%8));
				h += delta;
			}
		}

		TriggerInternalPagesWrite(sst_lpn, BMF_BUFFER_ADDR, bloomfilter_size);
		sst_lpn += bloomfilter_size;
	}

	// INDEX FLUSH
	index_size = ((sizeof(struct _SSTABLE_INDEX_NODE) * total_entry) / BYTES_PER_DATA_REGION_OF_SLICE) + ((sizeof(struct _SSTABLE_INDEX_NODE) * total_entry) % BYTES_PER_DATA_REGION_OF_SLICE ? 1 : 0) ;
	TriggerInternalPagesWrite(sst_lpn, SSTABLE_INDEX_BUFFER, index_size);
	sst_lpn += index_size;

	// DATA FLUSH
	data_size = ((sizeof(struct _SSTABLE_DATA_NODE) * total_entry) / BYTES_PER_DATA_REGION_OF_SLICE) + ((sizeof(struct _SSTABLE_DATA_NODE) * total_entry) % BYTES_PER_DATA_REGION_OF_SLICE ? 1 : 0);
	TriggerInternalPagesWrite(sst_lpn, SSTABLE_DATA_BUFFER, data_size);
	sst_lpn += data_size;

	// sstable info
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].level = new_level;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].head_lpn = sst_log_lpn;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].tail_lpn = sst_lpn - 1;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].bloomfilter_size = bloomfilter_size;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].index_size = index_size;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].data_size = data_size;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].total_entry = total_entry;

	// Min/Max key
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].min_key = sstable_index[0].key;
	super_sstable_list_level[new_level][super_sstable_list[new_level].tail].max_key = sstable_index[total_entry-1].key;

	// level count
	// xil_printf("L%d, node count of victim 1 %d, victim 2 %d,", victim_level, super_sstable_list_level[victim_level][super_sstable_list[victim_level].head].total_entry, super_sstable_list_level[victim_level][(super_sstable_list[victim_level].head+1)% MAX_SSTABLE_LEVEL[victim_level]].total_entry);  
	super_level_info->level_count[victim_level] -= 2;
	super_sstable_list[victim_level].head = (super_sstable_list[victim_level].head + 2) % MAX_SSTABLE_LEVEL[victim_level];
	super_level_info->level_count[new_level]++;
	super_sstable_list[new_level].tail = (super_sstable_list[new_level].tail + 1) % MAX_SSTABLE_LEVEL[new_level];
	ASSERT(super_sstable_list[new_level].tail != super_sstable_list[new_level].head);

	// sstable log tail update
	sst_log_lpn = sst_lpn;

	// invalidate old sstables
	for(unsigned int i = sstable_info1->head_lpn; i<=sstable_info1->tail_lpn;i++){
		InvalidateOldVsa(i);
		// UpdateMetaForInvalidate(i);
	}
	for(unsigned int i = sstable_info2->head_lpn; i<=sstable_info2->tail_lpn;i++){
		InvalidateOldVsa(i);
		// UpdateMetaForInvalidate(i);
	}

	xil_printf("\r\n---------- COMPACTION END ----------\r\n");
	return;
}

void test () {

    xil_printf("START FILLING BUFFER!\n");

    SSTABLE_INDEX_NODE* sst_index = (SSTABLE_INDEX_NODE*) SSTABLE_INDEX_BUFFER;
    
	unsigned int num = 1; 

    for (int i=num*MAX_SKIPLIST_NODE-1; i>=0; i--) 
        sst_index[i].key = num*MAX_SKIPLIST_NODE-1-i;

    for (int i=0; i<num*MAX_SKIPLIST_NODE; i++)
        xil_printf("[BEFORE] index[%u].key = %u\n ", i, sst_index[i].key);

    unsigned int cur_lpn = 100;
	TriggerInternalPagesWrite(cur_lpn, SSTABLE_INDEX_BUFFER, num);
	SyncAllLowLevelReqDone();

    for (int i=0; i<num*MAX_SKIPLIST_NODE; i++)
        sst_index[i].key = i;

    for (int i=0; i<num*MAX_SKIPLIST_NODE; i++)
		xil_printf("[AFTER_WRITE] index[%u].key = %u\n ", i, sst_index[i].key);

	TriggerInternalPagesRead(cur_lpn, SSTABLE_INDEX_BUFFER, num);
	SyncAllLowLevelReqDone();

	for (int i=0; i<(num+1)*MAX_SKIPLIST_NODE; i++)
        xil_printf("[AFTER_READ] index[%u].key = %u\n ", i, sst_index[i].key);

    return;
}
