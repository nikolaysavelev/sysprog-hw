GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant
HH_FLAG = ../utils/heap_help/heap_help.c

all: parser.c solution.c
	gcc $(GCC_FLAGS) parser.c solution.c ${HH_FLAG}

clean:
	rm a.out