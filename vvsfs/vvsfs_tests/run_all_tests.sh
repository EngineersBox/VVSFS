#!/bin/bash

set -e # Exit on error

./generate_env.sh
source vvsfs_env.sh

for file in test_*.sh; do
    ./$file
done
