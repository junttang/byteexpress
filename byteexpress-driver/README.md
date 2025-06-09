// Run the Cosmos+ OpenSSD at 88
sudo ./re_enumerate.sh

cd ~/linux/drivers/nvme/host
rm -f *.o *.ko *.mod.c *.mod.o *.symvers modules.order
make -C /home/kache/linux-6.6.31 M=$(pwd) modules
sudo cp custom_nvme.ko /lib/modules/$(uname -r)/kernel/drivers/nvme/host/
sudo depmod -a
sudo modprobe -r custom_nvme
sudo modprobe custom_nvme
sudo lspci -vvv -s 5e:00.0
echo "0000:5e:00.0" | sudo tee /sys/bus/pci/drivers/nvme/unbind
echo "custom_nvme" | sudo tee /sys/bus/pci/devices/0000:5e:00.0/driver_override
echo "0000:5e:00.0" | sudo tee /sys/bus/pci/drivers_probe
sudo lspci -vvv -s 5e:00.0

=> 일단 어째서인지 filesystem mount에 실패함.
custom_nvme에서 추가한거라곤 pci.c에 printk를 추가한게 다인데?
이거 때문이 아니라 저번에 현선이가 얘기한 버전 문제가 아닐지?
