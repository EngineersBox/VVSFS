source ./assert.sh

log_header "Testing uid gid"

./create.sh

touch testdir/bin
touch testdir/daemon
sudo chown bin testdir/bin
sudo chgrp bin testdir/bin
sudo chown daemon testdir/daemon
sudo chgrp daemon testdir/daemon

./umount.sh
./mount.sh

assert_eq "$(stat testdir/bin -c "%U:%G")" "bin:bin" "expected bin file to be owned by bin"
assert_eq "$(stat testdir/daemon -c "%U:%G")" "daemon:daemon" "expected daemon file to be owned by bin"
