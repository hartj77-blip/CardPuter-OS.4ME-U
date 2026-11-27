# Turn a file into a C array, so one binary can carry another.
#   cmake -DIN=<file> -DOUT=<file.c> -DNAME=<symbol> -P embed.cmake
# gives:  const unsigned char <symbol>[];  const unsigned long <symbol>_len;
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR len "${hexlen} / 2")
if(len EQUAL 0)
  message(FATAL_ERROR "embed.cmake: ${IN} is empty")
endif()
# 16 bytes a line.
string(REGEX REPLACE "(................................)" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
file(WRITE "${OUT}"
  "/* Made by cmake/embed.cmake from ${IN}. */\n"
  "const unsigned char ${NAME}[] = {\n${hex}\n};\n"
  "const unsigned long ${NAME}_len = ${len};\n")
