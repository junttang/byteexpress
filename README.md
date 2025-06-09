# ByteExpress: A High-Performance and Traffic-Efficient Inline Transfer of Small Payloads over NVMe

<!--[![HotStorage 2025](https://img.shields.io/badge/HotStorage'25-Accepted-blue)](https://doi.org/10.1145/3736548.3737837)-->

This repository contains a collection of components implementing **ByteExpress**, a lightweight and high-performance extension to the NVMe protocol that enables **inline transfer of small payloads via the Submission Queue (SQ)**, bypassing the conventional PRP-based mechanism.

ByteExpress improves PCIe bandwidth utilization and payload transfer performance for computational storage devices, especially in small-data scenarios common in SQL pushdown and key-value workloads.

> 📄 **Published Paper**: **ByteExpress: A High-Performance and Traffic-Efficient Inline Transfer of Small Payloads over NVMe** *Junhyeok Park, Junghee Lee, and Youngjae Kim.*  In Proceedings of the 17th ACM Workshop on Hot Topics in Storage and File Systems (HotStorage ’25), July 2025. [🔗DOI](https://doi.org/10.1145/3736548.3737837)

---

## Repository Structure

This monorepo includes three main components:

### [`byteexpress-driver/`](./byteexpress-driver)

A minimal patch to the Linux NVMe kernel driver (`pci.c`) that enables recognition and injection of payloads into the NVMe Submission Queue (SQ) in 64B-unit.  
This module builds into a standalone kernel module (`custom_nvme.ko`) that can be dynamically loaded.

> See [README](./byteexpress-driver/README.md) for build and setup instructions.

---

### [`byteexpress-controller/`](./byteexpress-controller)

Modified host interface controller code for Cosmos+ OpenSSD.  
The core logic (`get_nvme_cmds`) is extended to:
- Detect custom opcodes (e.g., `0xA8`)
- Extract inline 64B payload chunks from the SQ
- Copy the data into DRAM buffer for firmware processing

> See [README](./byteexpress-controller/README.md) for details on controller architecture and integration steps.

---

### [`test_app/`](./test_app)

A simple C++-based interface that sends NVMe passthrough commands using opcode `0xA8`.  
Used to validate the end-to-end correctness of the ByteExpress flow.

> See [README](./test_app/README.md) for usage example and build instructions.

---

## 📬 Contact

**Junhyeok Park** ([junttang@sogang.ac.kr](mailto:junttang@sogang.ac.kr))
