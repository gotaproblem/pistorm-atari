#!/bin/bash
filename=screendump
if [[ -e $filename.png || -L $filename.png ]] ; then
    i=0
    while [[ -e $filename$i.png || -L $filename$i.png ]] ; do
        let i++
    done
    filename=$filename$i
fi
mv screendump.png "$filename".png
rm screendump.raw