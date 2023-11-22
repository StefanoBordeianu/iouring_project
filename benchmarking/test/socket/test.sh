#!/bin/bash
./server 2020 60
for i in {1..10}
do
   ./server 2020 60
   sleep 5
done

