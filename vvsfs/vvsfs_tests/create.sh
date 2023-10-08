./umount.sh 2>/dev/null # ensure its not mounted
dd if=/dev/zero of=test.img bs=1024 count=20484 2>/dev/null
../mkfs.vvsfs test.img >/dev/null
mkdir testdir -p
./mount.sh