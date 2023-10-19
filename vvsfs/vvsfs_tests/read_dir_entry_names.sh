#!/bin/bash

function readDirEntryNames() {
    names=""
    fnames=$(ls -fx $1 | sed "s/[ \t\n\r]\+/ /g")
    for entry in "$fnames"; do
        names="$names $entry"
    done
    names=$(echo "$names" | tr "\n\r" " " | sed -e "s/^[[:space:]]//g" | sed -e 's/[[:space:]]*$//')
}
