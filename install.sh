sudo umount /dev/sde1
sudo udisks --mount /dev/sde1
uuid=a05b81f1-33f6-4787-9d36-fabfe744b7d3
sudo rm /media/$uuid/boot/kernel
sudo cp bin/kernel /media/$uuid/boot/kernel
sleep 1
sudo udisks --unmount /dev/sde1
sudo udisks --detach /dev/sde
