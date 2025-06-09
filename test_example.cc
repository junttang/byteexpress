#include "test_example.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <inttypes.h>

//#define DEBUG

using namespace std;

const unsigned int PAGE_SIZE = 4096;
const unsigned int MAX_BUFLEN = 1024*1024; // 1MB
const unsigned int NSID = 0; // *XXX: use your own nsid

int ByteExpress::Cosmos::Open(const std::string &dev)
{
    int err;
    err = open(dev.c_str(), O_RDONLY);
    if (err < 0)
        return -1; // fail to open
    fd_ = err;
    struct stat nvme_stat;
    err = fstat(fd_, &nvme_stat);
    if (err < 0)
        return -1;
    if (!S_ISCHR(nvme_stat.st_mode) && !S_ISBLK(nvme_stat.st_mode))
        return -1;
    return 0;
}

// Key-Value SSD Semantics are assumed in here 'PUT<key, value>'
int ByteExpress::Cosmos::Put(const std::string &key, const std::string &value)
{
    void *data = NULL;
    unsigned int data_len = value.size();
    unsigned int nlb = ((data_len-1)/PAGE_SIZE); 
    data_len = (nlb+1) * PAGE_SIZE;
    if (posix_memalign(&data, PAGE_SIZE, data_len)) {
        return -ENOMEM;
    }
    memcpy(data, value.c_str(), value.size());
    int err;
    uint32_t result;
    uint32_t cdw2, cdw3, cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
    cdw2 = cdw3 = cdw10 = cdw12 = cdw13 = cdw14 = cdw15 = 0;
    //    memcpy(&cdw10, key.c_str()+4, 4);
    //    memcpy(&cdw11, key.c_str(), 4);
    memcpy(&cdw10, key.c_str(), 4);
    cdw12 = 0 | (0xFFFF & nlb);
    cdw13 = value.size();
    err = nvme_passthru(NVME_CMD_BYTEEXPRESS_PUT, 0, 0, NSID, cdw2, cdw3, 
            cdw10, cdw11, cdw12, cdw13, cdw14, cdw15, 
            value.size(), data, result); // this doesn't affect PRP routine
            //data_len, data, result);
	    // TODO: it is turned out to be that it does not affect PRP, so this means that we can use this info for the driver to know the size of payload. The next step is to extract host buffer address to memcpy the payload into the submission queue, with a 64B-count calculated by the real payload size. The controller needs corresponding modifcations.
    free(data);
    if (err < 0) {
        return -1;
    } else if (result != 0) {
        return -1;
    }
    return 0;
}

int ByteExpress::Cosmos::nvme_passthru(uint8_t opcode,
        uint8_t flags, uint16_t rsvd,
        uint32_t nsid, uint32_t cdw2, uint32_t cdw3, uint32_t cdw10, uint32_t cdw11,
        uint32_t cdw12, uint32_t cdw13, uint32_t cdw14, uint32_t cdw15,
        uint32_t data_len, void *data, uint32_t &result)
{
    struct nvme_passthru_cmd cmd = {
        .opcode		= opcode,
        .flags		= flags,
        .rsvd1		= rsvd,
        .nsid		= nsid,
        .cdw2		= cdw2,
        .cdw3		= cdw3,
        .metadata	= (uint64_t)(uintptr_t) NULL,
        .addr		= (uint64_t)(uintptr_t) data,
        .metadata_len	= 0,
        .data_len	= data_len,
        .cdw10		= cdw10,
        .cdw11		= cdw11,
        .cdw12		= cdw12,
        .cdw13		= cdw13,
        .cdw14		= cdw14,
        .cdw15		= cdw15,
        .timeout_ms	= 0,
        .result		= 0,
    };
    int err;
#ifdef DEBUG
    {
        fprintf(stderr, "-- ByteExpress::Cosmos::nvme_passthru --\n");
        printf("opcode       : %02x\n", cmd.opcode);
        printf("flags        : %02x\n", cmd.flags);
        printf("rsvd1        : %04x\n", cmd.rsvd1);
        printf("nsid         : %08x\n", cmd.nsid);
        printf("cdw2         : %08x\n", cmd.cdw2);
        printf("cdw3         : %08x\n", cmd.cdw3);
        printf("data_len     : %08x\n", cmd.data_len);
        printf("metadata_len : %08x\n", cmd.metadata_len);
        printf("addr         : %"PRIx64"\n", cmd.addr);
        printf("metadata     : %"PRIx64"\n", cmd.metadata);
        printf("cdw10        : %08x\n", cmd.cdw10);
        printf("cdw11        : %08x\n", cmd.cdw11);
        printf("cdw12        : %08x\n", cmd.cdw12);
        printf("cdw13        : %08x\n", cmd.cdw13);
        printf("cdw14        : %08x\n", cmd.cdw14);
        printf("cdw15        : %08x\n", cmd.cdw15);
        printf("timeout_ms   : %08x\n", cmd.timeout_ms);
    }
#endif
    err = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
    if (!err && result)
        result = cmd.result;
    result = cmd.result;
    return err;
}

