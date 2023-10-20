#!/bin/bash
source ./init.sh
log_header "Testing uid gid"

touch testdir/bin
touch testdir/daemon
sudo chown bin testdir/bin
sudo chgrp bin testdir/bin
sudo chown daemon testdir/daemon
sudo chgrp daemon testdir/daemon

./remount.sh

assert_eq "$(stat testdir/bin -c "%U:%G")" "bin:bin" "expected bin file to be owned by bin"
check_log_success "File is owned by correct user and group when set"
assert_eq "$(stat testdir/daemon -c "%U:%G")" "daemon:daemon" "expected daemon file to be owned by bin"
check_log_success "File in same directory as file owned by others, is owned by correct user and group when set,"
