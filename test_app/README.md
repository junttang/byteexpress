# ByteExpress Test Application

This repository provides a simple test interface for evaluating **ByteExpress** functionality using NVMe passthrough commands. 
It is designed to work with a Cosmos+ OpenSSD device running firmware modified to support ByteExpress.

---

## Overview

- Uses NVMe passthrough (`NVME_IOCTL_IO_CMD`) to issue custom ByteExpress commands
- Demonstrates inline small-payload transmission with opcode `0xA8`
- Works directly with `/dev/nvmeX` block devices

The main components of this repo:

- `test_example.h`: Defines a minimal C++ class (`ByteExpress::Cosmos`) with a `Put()` interface.
- `test_example.cc`: Implements device open and NVMe passthrough logic to invoke ByteExpress PUT.

---

## Usage Example

```cpp
#include "test_example.h"
#include <iostream>

int main() {
    ByteExpress::Cosmos db;
    if (db.Open("/dev/nvme0") != 0) {
        std::cerr << "Failed to open NVMe device" << std::endl;
        return 1;
    }

    std::string key = "key1";
    std::string value = "this_is_a_small_payload";
    if (db.Put(key, value) != 0) {
        std::cerr << "ByteExpress PUT failed" << std::endl;
        return 1;
    }

    std::cout << "ByteExpress PUT succeeded" << std::endl;
    return 0;
}
```

Compile the test:

```bash
g++ test_example.cc -o test_app
```

Run it with appropriate permissions (you may need `sudo`):

```bash
sudo ./test_app
```

---

## Notes

- Only **small payloads (256 bytes or less)** are suitable for ByteExpress.
- The driver must support opcode `0xA8` (or your designated opcode) for PUT operations.
- This test app is intended for Cosmos+ OpenSSD running the ByteExpress-enabled firmware.

---

## Contact

**Junhyeok Park** (junttang@sogang.ac.kr)
