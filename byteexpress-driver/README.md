# ByteExpress Linux Kernel NVMe Driver Extension

This repository provides a modified `pci.c` file from the Linux kernel NVMe driver with support for **ByteExpress**, an extension designed to improve small-payload transfer efficiency on NVMe-based computational storage devices.  
Unlike the conventional PRP-based DMA mechanism, ByteExpress transmits payloads by embedding them inline within the NVMe Submission Queue (SQ), immediately after the command itself. 
This SQ-based transfer approach enables fine-grained PCIe communication in 64-byte units without breaking NVMe compatibility or requiring changes to the standard passthrough interface.

This implementation is based on the design proposed in the following paper:
**[ByteExpress: A High-Performance and Traffic-Efficient Inline Transfer of Small Payloads over NVMe](https://doi.org/10.1145/3736548.3737837)**  *Junhyeok Park, Junghee Lee, and Youngjae Kim. In Proceedings of the 17th ACM Workshop on Hot Topics in Storage and File Systems (HotStorage ’25), July 2025.*

---

## Overview

- Extends `nvme_queue_rq()` and `nvme_driver` registration logic in `pci.c`
- Supports communication with ByteExpress-enabled firmware
- Designed for stability—standard I/O paths are preserved
- Includes example test code in `../test_app`

---

## Setup Instructions

### 1. PCIe Re-Enumeration after Firmware Load

After powering on and completing firmware loading on Cosmos+, trigger PCIe re-enumeration:

```bash
sudo ./re_enumerate.sh
```

---

### 2. Apply Driver Modifications

Navigate to your NVMe host driver source directory:

```bash
cd ~/path/to/nvme/host
```

Apply the patch from this repository. Only the following components were modified:

- `struct pci_driver nvme_driver` in `pci.c`
- `nvme_queue_rq()` function in `pci.c`

> 💡 Tip: Use `diff` to inspect changes compared to your base kernel source.

---

### 3. (Optional) Clean Up Old Artifacts

```bash
rm -f *.o *.ko *.mod.c *.mod.o *.symvers modules.order
```

---

### 4. Build the Custom Driver Module

Build the kernel module targeting your kernel version (tested on **6.6.31**):

```bash
make -C /path/to/linux-6.6.31 M=$(pwd) modules
```

The `Makefile` in this repository builds the module as:

```text
custom_nvme.ko
```

---

### 5. Install the Custom NVMe Module

```bash
sudo cp custom_nvme.ko /lib/modules/$(uname -r)/kernel/drivers/nvme/host/
sudo depmod -a
```

---

### 6. Unload and Reload Module 

```bash
sudo modprobe -r custom_nvme
sudo modprobe custom_nvme
```

---

### 7. Override and Rebind PCIe Device to the Custom Driver

Find your Cosmos+ PCIe address (e.g., `5e:00.0`) and rebind:

```bash
sudo lspci -vvv -s 5e:00.0

echo "0000:5e:00.0" | sudo tee /sys/bus/pci/drivers/nvme/unbind
echo "custom_nvme" | sudo tee /sys/bus/pci/devices/0000:5e:00.0/driver_override
echo "0000:5e:00.0" | sudo tee /sys/bus/pci/drivers_probe

sudo lspci -vvv -s 5e:00.0
```

---

## Testing ByteExpress Commands

ByteExpress is selectively enabled only for specific opcodes (e.g., 0xA8) to facilitate validation and debugging, while maintaining full compliance with the standard NVMe specification.
Use the example client in `../test_app/test_example.h` and `test_example.cc` to send test commands.

---

## Notes

- This driver has been **tested only on Linux 6.6.31**.
- ByteExpress functionality is limited to custom-defined NVMe opcodes.
- This project assumes you have already flashed Cosmos+ OpenSSD with ByteExpress-enabled firmware (see README.md in `../byteexpress-controller`).

---

## Contact

**Junhyeok Park** (junttang@sogang.ac.kr)
