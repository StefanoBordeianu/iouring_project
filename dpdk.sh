#!/bin/bash

sudo apt update
sudo apt install build-essential
sudo apt update
sudo apt install python3-pip
sudo pip3 install meson ninja
sudo apt install python3-pyelftools
sudo apt install libnuma-dev
nano /etc/default/grub

sudo mkdir /mnt/huge
sudo mount -t hugetlbfs pagesize=1GB /mnt/huge
nano /etc/fstab
