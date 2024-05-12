#!/bin/bash
cwd=$(pwd)
homedir="${cwd##*/}"

if [ -d "../screendumps" ] ; then
    cd ../screendumps
    filename=screendump
    
    i=0
    while [[ -e $filename$i.png || -L $filename$i.png ]] ; do
        let i++
    done
    filename=$filename$i
    
    mv ../$homedir/screendump.png "$filename".png
    rm ../$homedir/screendump.raw
fi
