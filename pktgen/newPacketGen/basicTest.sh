#!/bin/bash

for i in 20 100 500 1000 5000 10000 25000 60000
do
  ./p -i 192.168.1.1 -p 2020 -r 1000 -d 1000 -t 4 -s i
  sleep 60
done