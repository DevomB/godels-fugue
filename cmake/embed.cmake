# Turns a text file into a C byte array so the binary carries it.
#   cmake -DIN=file -DOUT=file.c -DNAME=symbol -P embed.cmake
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# A checkout with CRLF line endings embeds the same bytes as one with LF, so
# text the program writes from the array does not end its lines in CR CR LF.
string(REPLACE "0x0d,0x0a," "0x0a," bytes "${bytes}")
string(LENGTH "${bytes}" bytes_len)
math(EXPR size "${bytes_len} / 5")
string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}"
  "/* Generated from ${IN}; edit that file instead. */\n"
  "const unsigned char ${NAME}[] = {\n${bytes}0x00};\n"
  "const unsigned long ${NAME}_len = ${size};\n")
