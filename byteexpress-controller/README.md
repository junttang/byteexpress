# ByteExpress Host Interface Controller (Cosmos+ OpenSSD)

This repository contains the modified NVMe host interface controller code for **Cosmos+ OpenSSD**, incorporating support for **ByteExpress**—an extension designed to accelerate small-payload transmission via direct command and data injection.

---

## What's Modified

Only one core function is extended for ByteExpress:

### `get_nvme_cmds()`  
This function is updated to:
- **Recognize incoming NVMe commands issued via ByteExpress** (e.g., opcode `0xA8`)
- **Directly fetch the inline 64-byte payload** transmitted right after the command
- **Copy the payload into a designated DRAM buffer address** within the OpenSSD device

This allows the controller to process custom commands with inline data in a tightly-coupled fashion.

---

## Host Interface Architecture

The Cosmos+ OpenSSD host interface is architected in two layers (refer to [this paper](https://dl.acm.org/doi/10.1145/3385073)):

1. **High-level NVMe Controller (C-based Software) [[link](https://github.com/Cosmos-OpenSSD/Cosmos-plus-OpenSSD/tree/master/source/software/GreedyFTL-3.0.0/nvme)]**  
   This layer, implemented in C (the focus of this repository), is responsible for parsing NVMe commands pulled from a central **host command queue** and invoking appropriate firmware logic. The key entry point is `get_nvme_cmds()`.

2. **Low-level NVMe Controller (Verilog HDL) [[link](https://github.com/Cosmos-OpenSSD/Cosmos-plus-OpenSSD/tree/master/source/hardware/nvme/nvme_host_ctrl_8lane-1.0.0)]**  
   This hardware layer scans across 8 host-side NVMe Submission Queues (SQs) in sequence (from SQ 0 to SQ 7). Upon detecting a command in any SQ, it enqueues the command into a shared **host command queue**, which is consumed by the high-level controller described above.

### Dispatching Limitation and Workaround

In the ByteExpress study, we **did not modify** the low-level Verilog HDL layer. 
However, it is important to note that this dispatch logic **does not operate in a round-robin fashion**. Instead, after every enqueued command, the scanner **restarts (re-evaluate an arbitration) from SQ 0**—a naive approach that can introduce unfairness (refer to [this code](https://github.com/Cosmos-OpenSSD/Cosmos-plus-OpenSSD/blob/master/source/hardware/nvme/nvme_host_ctrl_8lane-1.0.0/pcie_hcmd_sq_arb.v)).

This leads to an **interleaving issue** under high concurrency:  
If ByteExpress transactions are simultaneously issued from multiple random SQs, a command from a higher-index SQ may be delayed or interleaved by newly arriving commands in lower-index SQs, leading to out-of-order behavior or memory failure.

To **work around** this behavior without modifying the HDL code, our current prototype employs **only a single active SQ**, ensuring isolation and avoiding interleaving.  
That said, **proper ByteExpress support under full parallelism requires revising the Verilog-based SQ dispatch logic**, which we plan to address in a future update.

---

## Integration Note

We have chosen not to release other controller logic files, as they are tightly coupled with our proprietary KV-SSD firmware stack.

However, we guarantee that `host_lld.c` and `host_lld.h` in this repository are cleanly designed and **can be integrated into any OpenSSD firmware** with minimal effort—**simply by specifying the DRAM buffer address** to use for payload copying.

---

## Contact

**Junhyeok Park** (junttang@sogang.ac.kr)
