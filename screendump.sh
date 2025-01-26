#!/bin/bash
pistormDIR=$(pwd)
if [ -d "../screendumps" ] ; then
    cd ../screendumps
    filename=screendump
    
    i=0
    while [[ -e $filename$i.png || -L $filename$i.png ]] ; do
        let i++
    done
    filename=$filename$i
    
    mv $pistormDIR/screendump.png "$filename".png
    rm $pistormDIR/screendump.raw
fi