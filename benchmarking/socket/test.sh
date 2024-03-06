#!/bin/bash
for i in {1..8}
do
  for i in {1..10}
  do
    ./server 2020 90
    sleep 5
  done
  sleep 90
done
