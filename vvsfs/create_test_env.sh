#/bin/bash

DIR=testing
IMG=disk.img

# Clear old stuff if present
umount $DIR
echo "[VVSFS] Unmounted directory: $DIR"
rm -rf $DIR $IMG
echo "[VVSFS] Removed directory $DIR and image $IMG"

# Create disk image
dd if=/dev/zero of=$IMG bs=1024 count=20484
echo "[VVSFS] Created disk image: $IMG"
./mkfs.vvsfs $IMG
echo "[VVSFS] Created VVSFS with image $IMG"

# Create test directory
mkdir $DIR
echo "[VVSFS] Created directory: $DIR"

# Mount VVSFS at directory
mount -o loop -t vvsfs $IMG $DIR
echo "[VVSFS] Mounted directory $DIR from image $IMG"
