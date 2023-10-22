## Overview

TODO

## Tasks Completed

TODO

## Testing

 * We created our own test suit (vvsfs/vvsfs_tests)
    - This was used to drive test driven design as we could create tests for expected behaviour and build new features.
    - Additionally we utilised it as a regression test suit to ensure that new code didn't break anything. And whenever we fixed problems that were discoved we built a test to ensure we didn't break it again.
    - It is composed of a set of helper scripts that provide automatic generation of a test environment, and an [assertion framework](https://github.com/torokmark/assert.sh/blob/main/assert.sh) to provide nice error messages.

 * We used the [pjdfstest](https://github.com/pjd/pjdfstest) to check our implentation for posix complience and various other edge cases. By the end we passed all tests with the following exceptions:
    1. The tests for large files (2gb) files.
    2. The filesystem does not keep track of the `.` & `..` files in directories as such we failed the test testing that folder link counts were incremented correctly. We chose to ignore this due to [https://edstem.org/au/courses/12685/discussion/1633469](https://edstem.org/au/courses/12685/discussion/1633469).
    3. The filesystem does not correctly update ctime on truncate. (TODO: Does anyone want to fix this?)
    4. The filesystem does not store high presision time, only seconds like minix & ext2. (TODO: Does anyone want to fix this?)

## Baseline

### Unlink Dentries and Removing Directories

TODO

### Renaming

TODO

### Inode Attributes

TODO

### Supporting FS Stats

TODO

## Advanced

### Indirect Blocks

TODO

## Extensions

### Hardlinks and Symbolic Links

TODO

### Special Devices

TODO
