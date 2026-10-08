/* Makefile 
	- make (default): compila harmonic-sum.c com -fopemp, -O0, -g, -Wall, -Wextra, -Werror,
	    	          -fno-omit-frame-pointer, -pg e -fsanitize=address
	- make clean: remove arquivos gerados
	- make debug: compila harmonic-sum.c com -g, -Wall, -Wextra, -Werror,
	    	          -fno-omit-frame-pointer e -fsanitize=address
	- make optimize: compila harmonic-sum.c com -O3, -g, -Wall, -Wextra, -Werror,
	    	          -fno-omit-frame-pointer, -pg e -fsanitize=address
*/

all: harmonic-sum

harmonic-sum: harmonic-sum.c
	gcc -fopenmp -O0 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -pg -fsanitize=address harmonic-sum.c -o harmonic-sum

clean:
	rm -rf harmonic-sum

debug: harmonic-sum
	gcc -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address harmonic-sum.c -o harmonic-sum

optimize: harmonic-sum
	gcc -O3 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -pg -fsanitize=address harmonic-sum.c -o harmonic-sum
