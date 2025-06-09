#!/bin/sh
 
PCIE_ADDR=`lspci | grep "Xilinx Corporation Device 7028" | cut -d ' ' -f 1`
if [ -z $PCIE_ADDR ]
then
        echo "No PCIe device found!";
        exit 1;
fi

PCIE_ADDR=0000:`echo $PCIE_ADDR | cut -d' ' -f 1`
echo "PCIe Device Detected at $PCIE_ADDR"
 
echo 1 > /sys/bus/pci/devices/$PCIE_ADDR/remove
sleep 1
echo 1 > /sys/bus/pci/rescan
