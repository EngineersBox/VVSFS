#!/bin/sh

pandoc report.md \
    --toc=true \
    --toc-depth=5 \
    -V geometry:margin=3cm \
    -V author='Jack Kilrain (u6940136) Daniel Herald (u7480080) Angus Atkinson (u7117106)' \
    -V date='22 October 2023' \
    -V title='Very Very Simple File System (VVSFS)'\
    -N \
    -o Group17_report.pdf