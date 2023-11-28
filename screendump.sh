#!/bin/bash
if [ -d "../screendumps" ] ; then
    cd ../screendumps
    filename=screendump
    
    i=0
    while [[ -e $filename$i.png || -L $filename$i.png ]] ; do
        let i++
    done
    filename=$filename$i
    
    mv ../pistorm-atari/screendump.png "$filename".png
    rm ../pistorm-atari/screendump.raw
fi