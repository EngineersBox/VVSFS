#!/bin/bash

function readDirEntryNames() {
    names=""
    fnames=$(ls -fx $1 | sed "s/[ \t]\+/ /g")
    for entry in "$fnames"; do
        names="$names $entry"
    done
    names=$(echo "$names" | tr -s "[:space:]" | sed "s/^ //g")
}
