#!/bin/bash

# This script retrieves all of the #define
# constants used in the VVSFS implementation,
# evaluates them with the host compiler, and
# converts them into variables usable in
# bash scripts as a sourcable env file

ENV_FILE_NAME=${ENV_FILE_NAME:-"vvsfs_env.sh"}
VVSFS_HEADER=${VVSFS_HEADER:-"../vvsfs.h"}
CC=${CC:-"$(which gcc)"}

echo "Generting env vars using parameters:"
echo " - ENV_FILE_NAME = $ENV_FILE_NAME"
echo " - VVSFS_HEADER = $VVSFS_HEADER"
echo " - CC = $CC"
echo

DEFINES_NAMES=$(
    sed -nr '{
        /^#define/!d
        s/^#define[ \t](VVSFS[^ \t(]*)([ \t]\(?).*/\1/p
    }' <($CC -E -dM $VVSFS_HEADER)
)

EXPANSION_CODE=$(
    echo "$DEFINES_NAMES" \
    | awk '{print "EXPAND(\""$0"\","$0");"}'
)

# Note the use of "|| true" here, since "read"
# returns an exit code of 1, we need to ensure
# that it doesnt trigger any wrapping scripts
# with "set -e"
read -r -d '' EXPANSION_CODE <<EOF
#include "$VVSFS_HEADER"
#include <stdio.h>
#define format_specifier(x) _Generic((x), \
    _Bool: "%s=%s\n", \
    unsigned char: "%s=%uc\n", \
    char: "%s=%c\n", \
    signed char: "%s=%c\n", \
    unsigned short int: "%s=%hu\n", \
    short int: "%s=%hi\n", \
    unsigned int: "%s=%u\n", \
    int: "%s=%d\n", \
    unsigned long int: "%s=%lu\n", \
    long int: "%s=%l\n", \
    unsigned long long int: "%s=%llu\n", \
    long long int: "%s=%ll\n", \
    float: "%s=%f\n", \
    double: "%s=%lf\n", \
    long double: "%s=%Lf\n", \
    char*: "%s=%s\n", \
    void*: "%s=%p\n", \
    default: "%s=%s\n")
#define EXPAND(n, a) ({\
    __typeof__(a) _a = (a);\
    printf(format_specifier(_a), n, _a);\
})
int main() {
$EXPANSION_CODE
}
EOF

TMP=$(mktemp)
echo "$EXPANSION_CODE" | gcc -o $TMP -xc -
chmod a+x $TMP
echo "#!/bin/bash" > $ENV_FILE_NAME
$TMP >> $ENV_FILE_NAME
chmod a+x $ENV_FILE_NAME
rm $TMP

source $ENV_FILE_NAME

echo "Written to $ENV_FILE_NAME... done"

