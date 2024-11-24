#!/bin/bash

# gcc -c external/GLAD/src/glad.c -Iexternal/GLAD/include -o out/glad.o -g -Wall
# echo "GLAD built"

# gcc -c external/stb/stb_impl.c -Iexternal -o out/stb.o -g -Wall
# echo "STB built"

gcc -c src/main.c -o out/main.o -g -Wall -pedantic -Wextra -Werror -Iexternal -Isrc
echo "main built"

g++ -c src/sound.cpp -o out/sound.o -g -Wall -pedantic -Wextra -Werror -Iexternal -Isrc
echo "sound built"

g++ out/main.o out/sound.o out/stb.o out/glad.o -o out/main.exe -g -Wall -Lexternal/SDL2/bin -lSDL2 -lm -lole32
echo "finished linking"
