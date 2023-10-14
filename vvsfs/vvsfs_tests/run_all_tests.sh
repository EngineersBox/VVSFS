#!/bin/bash

./generate_env.sh
source vvsfs_env.sh

./test_basic.sh
./test_access_modification.sh
./test_link_create.sh
./test_uid_gid.sh
./test_unlink.sh
./test_rmdir.sh

