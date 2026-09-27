# Turns a text file into a C byte array so the binary carries it.
#   cmake -DIN=file -DOUT=file.c -DNAME=symbol -P embed.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}"
  "/* Generated from ${IN}; edit that file instead. */\n"
  "const unsigned char ${NAME}[] = {\n${bytes}0x00};\n"
  "const unsigned long ${NAME}_len = ${size};\n")
