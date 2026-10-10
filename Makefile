run:
	clang -I include src/main.c src/world.c src/worldgen.c src/blocks/registry.c -o engine
	./engine

example:
	clang -std=c11 -Wall -Wextra -Wpedantic -Iinclude example.c src/world.c src/worldgen.c src/blocks/registry.c -o example.exe
