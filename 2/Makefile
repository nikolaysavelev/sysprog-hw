GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: parser.c solution.c
	gcc $(GCC_FLAGS) parser.c solution.c ../utils/heap_help/heap_help.c

clean:
	rm a.out