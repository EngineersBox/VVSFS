#!/bin/bash

./generate_env.sh

for test in test_*.sh; do
    ./$test
done
