#!/bin/bash

gcc -c src/main.c -o out/main.o -g -Wall -Iexternal -Isrc
echo "main built"

gcc out/main.o -o out/main.exe -g -Wall
echo "finished linking"
