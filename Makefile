run:
	clang -I include src/main.c src/world.c src/worldgen.c src/blocks/registry.c -o engine
	./engine
