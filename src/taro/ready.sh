#!/bin/sh
rm data/*
rm ../hanako/data/*
rm check.md5
for i in `seq 0 999`
do
    cat /dev/urandom | head -c 102400 > data/data$i
done
cd data
md5sum $(find . -type f) | tee ../check.md5
cd ..
cp check.md5 ../hanako/check.md5
