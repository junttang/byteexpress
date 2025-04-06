//////////////////////////////////////////////////////////////////////////////////
// nvme_main.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Kibin Park <kbpark@enc.hanyang.ac.kr>
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
//			 Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe Main
// File Name: nvme_main.c
//
// Version: v1.2.0
//
// Description:
//   - initializes FTL and NAND
//   - handles NVMe controller
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.2.0
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//   - Low level scheduler execution is allowed when there is no i/o command
//
// * v1.1.0
//   - DMA status initialization is added
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"
///////////////////////////////////// junhyeok
#include "xiicps.h"
#include "xparameters.h"
#include "xadcps.h"
#define I2C_DEVICE_ID  XPAR_XIICPS_0_DEVICE_ID  // PS I2C controller ID
//#define PMBUS_ADDRESS  0x58                      // PMBus I2C address
#define PMBUS_ADDRESS  0x77                      // PMBus I2C address
#define READ_PIN_CMD   0x97                      // PMBus power measurement command
///////////////////////////////////// junhyeok

#include "nvme.h"
#include "host_lld.h"
#include "nvme_main.h"
#include "nvme_admin_cmd.h"
#include "nvme_io_cmd.h"

#include "../memory_map.h"
#include "../memtable/memtable.h"
#include "../sstable/sstable.h"
#include "../sstable/super.h"

volatile NVME_CONTEXT g_nvmeTask;

static SKIPLIST_HEAD* skiphead = (SKIPLIST_HEAD*)MEMTABLE_HEAD_ADDR;
static SUPER_LEVEL_INFO* super_level_info = (SUPER_LEVEL_INFO*)SUPER_BLOCK_ADDR;
static SUPER_SSTABLE_LIST* super_sstable_list = (SUPER_SSTABLE_LIST*)SUPER_SSTABLE_LIST_ADDR;

AUTO_CPL_INFO auto_cpl_info[MAX_NUM_NVME_SLOT];

//////////////////////////////////////// junhyeok
float PMBus_ConvertLinearData(u16 raw) {
    int exponent = (raw >> 11) & 0x1F;  // Upper 5bits (E)
    int mantissa = raw & 0x07FF;        // Lower 11bits (M)

    if (exponent >= 16) exponent -= 32; 
    if (mantissa >= 1024) mantissa -= 2048;

    double scaling_factor = 1.0;
    if (exponent >= 0) {
        scaling_factor = (double)(1 << exponent);
    } else {
        scaling_factor = 1.0 / (double)(1 << -exponent);
    }

    float result = mantissa * scaling_factor;

    xil_printf("PMBus Data: Raw=0x%04X, Mantissa=%d, Exponent=%d, Scaling Factor=%.8f, Converted=%.4f W\r\n", raw, mantissa, exponent, scaling_factor, result);

    return result;
}

void scan_i2c_bus(XIicPs *IicInstance) {
    int Status;
    u8 buffer[1];
    int i2c_addr;

    xil_printf("Scanning I2C Bus...\r\n");
    for (i2c_addr = 0x03; i2c_addr < 0x78; i2c_addr++) {
        int timeout = 10000; 
        while (XIicPs_BusIsBusy(IicInstance) && --timeout > 0);

        if (timeout == 0) {
            xil_printf("I2C Bus Timeout at address 0x%02X\r\n", i2c_addr);
            continue; 
        }
        Status = XIicPs_MasterRecvPolled(IicInstance, buffer, 1, i2c_addr);

        if (Status == XST_SUCCESS) {
            xil_printf("Found I2C Device at 0x%02X\r\n", i2c_addr);
        } else {
            xil_printf("No response from 0x%02X (Status: %d)\r\n", i2c_addr, Status);
        }
    }
}
//////////////////////////////////////// junhyeok

void nvme_main()
{
	unsigned int exeLlr;
	unsigned int rstCnt = 0;

	xil_printf("!!! Wait until FTL reset complete !!! \r\n");

	InitFTL();

	initSkipList(skiphead);

	xil_printf("value log lba : 0x%08x, sst_lpn : %u\n", value_log_lba, sst_log_lpn);

	// Initialize Skiplist
	skiphead->st_lba = value_log_lba;

	/* 
	 * Superblock Initialization
	 * 1. Initialize Version Layout Info Free Pool
	 * 2. Initialize each level list
	 * */
	xil_printf("super_sstable_list = 0x%08X\r\n", SUPER_SSTABLE_LIST_ADDR);
	for(int i=0; i<4; i++)
		super_level_info->level_count[i] = 0;

	for(int i=0; i<4; i++) {
		xil_printf("&super_sstable_list[%d] = 0x%08X\r\n", i, &super_sstable_list[i]);
		super_sstable_list[i].head = 0;
		super_sstable_list[i].tail = 0;
	}
	
	for (int i=0; i<MAX_NUM_NVME_SLOT; i++) {
		auto_cpl_info[i].mark = 0;
		// auto_cpl_info[i].sqid = 0;
		// auto_cpl_info[i].cid = 0;
		auto_cpl_info[i].numOfNvmeBlock = 0;
		auto_cpl_info[i].sentNvmeBlock = 0;
		auto_cpl_info[i].kv_length = 0;
	}

	init_iter_pool();
	
	xil_printf("DEBUG MODE IS ");
#ifdef DEBUG_iLSM
	xil_printf("ON!\r\n");
#else
	xil_printf("OFF!\r\n");
#endif
	////////////////////////////////////////////////// junhyeok
	// 1. Temperature
	XAdcPs XAdcInst;  // Create an XADC instance
	XAdcPs_Config *ConfigPtr;
	u32 TempRawData;
	float Temperature;

	// XADC initialization
	ConfigPtr = XAdcPs_LookupConfig(XPAR_XADCPS_0_DEVICE_ID);
	XAdcPs_CfgInitialize(&XAdcInst, ConfigPtr, ConfigPtr->BaseAddress);

	// Read the temperature
	TempRawData = XAdcPs_GetAdcData(&XAdcInst, XADCPS_CH_TEMP);
	Temperature = XAdcPs_RawToTemperature(TempRawData);

	xil_printf("Raw Temp Data: %u\r\n", TempRawData);
	xil_printf("FPGA Temperature: %d.%d°C\r\n", (int)Temperature, (int)((Temperature - (int)Temperature) * 100));

	// 2. Power consumption
	XIicPs IicInstance;
	XIicPs_Config *Config;
	int Status;

 	Config = XIicPs_LookupConfig(I2C_DEVICE_ID);
	if (Config == NULL) {
		xil_printf("Error: I2C Config NULL\r\n");
	}

	Status = XIicPs_CfgInitialize(&IicInstance, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		xil_printf("Error: I2C Initialization Failed\r\n");
	}

	// I2C speed setup (100KHz default mode)
	XIicPs_SetSClk(&IicInstance, 100000);
	//scan_i2c_bus(&IicInstance);

	u8 buffer[2] = {0};
	u8 cmd_buffer = READ_PIN_CMD; 
	Status = XIicPs_MasterSendPolled(&IicInstance, &cmd_buffer, 1, PMBUS_ADDRESS);
	xil_printf("I2C Send Status: %d\r\n", Status);
	if (Status != XST_SUCCESS) {
		xil_printf("Error: I2C Send Failed\r\n");
	}

	// Wait until it receives a response
	while (XIicPs_BusIsBusy(&IicInstance));
	Status = XIicPs_MasterRecvPolled(&IicInstance, buffer, 2, PMBUS_ADDRESS);
	xil_printf("I2C Receive Status: %d, Raw Data: 0x%02X%02X\r\n", Status, buffer[0], buffer[1]);
	if (Status != XST_SUCCESS) {
		xil_printf("Error: I2C Receive Failed\r\n");
	}

	// Value translation (mW unit)
	u16 power_raw = (buffer[0] << 8) | buffer[1];
	//float power = power_raw * 0.1;  // Translation
	float power = PMBus_ConvertLinearData(power_raw);
	if (power == 0.0) {
		xil_printf("Warning: Power value is 0. Check PMBus conversion logic.\r\n");
	}
	xil_printf("FPGA Power Consumption: %.1f W\r\n", power);
	////////////////////////////////////////////////// junhyeok

	xil_printf("\r\nFTL reset complete!!! \r\n");
	xil_printf("Turn on the host PC \r\n");
	
	// test();

	while(1)
	{		
		exeLlr = 1;

		if(g_nvmeTask.status == NVME_TASK_WAIT_CC_EN)
		{
			unsigned int ccEn;
			ccEn = check_nvme_cc_en();
			if(ccEn == 1)
			{
				set_nvme_admin_queue(1, 1, 1);
				set_nvme_csts_rdy(1);
				g_nvmeTask.status = NVME_TASK_RUNNING;
				xil_printf("\r\nNVMe ready!!!\r\n");
			}
		}
		else if(g_nvmeTask.status == NVME_TASK_RUNNING)
		{
			NVME_COMMAND nvmeCmd;
			unsigned int cmdValid;
			/////////////////////////////////// junhyeok
			//cmdValid = get_nvme_cmd(&nvmeCmd.qID, &nvmeCmd.cmdSlotTag, &nvmeCmd.cmdSeqNum, nvmeCmd.cmdDword);
			cmdValid = get_nvme_cmd(&nvmeCmd.qID, &nvmeCmd.cmdSlotTag, &nvmeCmd.cmdSeqNum, nvmeCmd.cmdDword, INLINE_TRANSFER_BUFFER_ADDR);
			/////////////////////////////////// junhyeok
			if(cmdValid == 1)
			{	rstCnt = 0;
				if(nvmeCmd.qID == 0)
				{
					handle_nvme_admin_cmd(&nvmeCmd);
				}
				else
				{
					handle_nvme_io_cmd(&nvmeCmd);
					ReqTransSliceToLowLevel();
					
					// if (sync) {
					// 	sync=0;
					// 	SyncAllLowLevelReqDone();
					// }
					exeLlr=0;
				}
			}
		}
		else if(g_nvmeTask.status == NVME_TASK_SHUTDOWN)
		{
			NVME_STATUS_REG nvmeReg;
			nvmeReg.dword = IO_READ32(NVME_STATUS_REG_ADDR);
			if(nvmeReg.ccShn != 0)
			{
				unsigned int qID;
				set_nvme_csts_shst(1);

				for(qID = 0; qID < 8; qID++)
				{
					set_io_cq(qID, 0, 0, 0, 0, 0, 0);
					set_io_sq(qID, 0, 0, 0, 0, 0);
				}

				set_nvme_admin_queue(0, 0, 0);
				g_nvmeTask.cacheEn = 0;
				set_nvme_csts_shst(2);
				g_nvmeTask.status = NVME_TASK_WAIT_RESET;

				//flush grown bad block info
				UpdateBadBlockTableForGrownBadBlock(RESERVED_DATA_BUFFER_BASE_ADDR);

				xil_printf("\r\nNVMe shutdown!!!\r\n");
			}
		}
		else if(g_nvmeTask.status == NVME_TASK_WAIT_RESET)
		{
			unsigned int ccEn;
			ccEn = check_nvme_cc_en();
			if(ccEn == 0)
			{
				g_nvmeTask.cacheEn = 0;
				set_nvme_csts_shst(0);
				set_nvme_csts_rdy(0);
				g_nvmeTask.status = NVME_TASK_IDLE;
				xil_printf("\r\nNVMe disable!!!\r\n");
			}
		}
		else if(g_nvmeTask.status == NVME_TASK_RESET)
		{
			unsigned int qID;
			for(qID = 0; qID < 8; qID++)
			{
				set_io_cq(qID, 0, 0, 0, 0, 0, 0);
				set_io_sq(qID, 0, 0, 0, 0, 0);
			}

			if (rstCnt>= 5){
				pcie_async_reset(rstCnt);
				rstCnt = 0;
				xil_printf("\r\nPcie iink disable!!!\r\n");
				xil_printf("Wait few minute or reconnect the PCIe cable\r\n");
			}
			else
				rstCnt++;

			g_nvmeTask.cacheEn = 0;
			set_nvme_admin_queue(0, 0, 0);
			set_nvme_csts_shst(0);
			set_nvme_csts_rdy(0);
			g_nvmeTask.status = NVME_TASK_IDLE;

			xil_printf("\r\nNVMe reset!!!\r\n");
		}

		if(exeLlr && ((nvmeDmaReqQ.headReq != REQ_SLOT_TAG_NONE) || notCompletedNandReqCnt || blockedReqCnt))
		{
			CheckDoneNvmeDmaReq();
			SchedulingNandReq();
		}
	}
}


