#pragma once

#include <string>
#include <vector>

namespace ByteExpress {
    enum nvme_opcode {
      NVME_CMD_BYTEEXPRESS_PUT = 0xA8  // *XXX: use your own opcode
    };

    class Cosmos {
        public:
            DB() : fd_(-1) {}
            int Open(const std::string &dev);
            int Put(const std::string &key, const std::string &value);
        private:
            int fd_;
            int nvme_passthru(uint8_t opcode,
                    uint8_t flags, uint16_t rsvd, uint32_t nsid,
                    uint32_t cdw2, uint32_t cdw3, uint32_t cdw10, uint32_t cdw11,
                    uint32_t cdw12, uint32_t cdw13, uint32_t cdw14, uint32_t cdw15,
                    uint32_t data_len, void *data, uint32_t &result);
    };
}
