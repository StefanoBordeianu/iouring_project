#!/bin/bash

for a in 20 100 500 1000 5000 10000 25000 60000
do
  ./p -i 192.168.1.2 -p 2020 -r 1000 -d 1000 -t 4 -s $a
  sleep 60
done