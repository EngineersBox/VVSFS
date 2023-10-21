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

# Sed lines explanation:
# 1. Keeps lines prefixed with "#define"
# 2. Removes empty definition line "#define VVSFS_H"
#    keeping normal definitions like "#define true 1"
# 3. Extract name of define and replace entire
#    expression of "#define <NAME> <VALUE>" to
#    "<NAME>"
# Note that we use the -E and -dM flags with the
# compiler to perform the following:
# -E: Do a pre-process pass only
# -dM: Dump macro expressions to stdout (note that
#      without -E, this is interpreted instead as
#      -fdump-rtl-mach, used to dump compiler state
#      after the machine dependent reorganisation
#      pass)
DEFINES_NAMES=$(
    sed -nr '{
        /^#define/!d
        /^#define[ \t]*[^ \t]*[ \t]*$/d
        s/^#define[ \t](VVSFS[^ \t(]*)([ \t]\(?).*/\1/p
    }' <($CC -E -dM $VVSFS_HEADER)
)

EXPANSION_CODE=$(
    echo "$DEFINES_NAMES" \
    | awk '{print "EXPAND(\""$0"\","$0");"}'
)

# Code explanation:
#
# The format_specifier macro uses the builtin
# compile time _Generic(x) to extract the type
# name from the expression, then pattern matches
# this against the type expressions to convert
# the appropriate type to a printf format
# specifier. This allows for fully type-safe
# printing of resolved values (expressions).
#
# The EXPAND macro uses the builtin compile time
# __typeof__ expression to fully qualify the
# definition of the temporary _a according to the
# type of the expression passed in (this is
# different from _Generic(x) which is a pattern
# matching expansion and token consumer). Using
# this (now type safe) declaration of _a, we can
# print its value according to the appropriate
# format specifier resolved with the previously
# explained macro format_specifier along with the
# name, into a bash variable delcaration format
# matching <NAME>=<VALUE>.

# Note the use of "|| true" here, since "read"
# returns an exit code of 1, we need to ensure
# that it doesnt trigger any wrapping scripts
# with "set -e"
read -r -d '' EXPANSION_CODE <<-EOF || true
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

