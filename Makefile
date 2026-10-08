# Makefile
#	- make (default): compila com gcc, -fopenmp, -O0, -g, -Wall, -Wextra, -Werror,
#	                  -fno-omit-frame-pointer, -pg e -fsanitize=address
#	- make clean: remove arquivos gerados
#	- make debug: compila com gcc, -g, -Wall, -Wextra, -Werror,
#	              -fno-omit-frame-pointer e -fsanitize=address
#	- make optimize: compila com gcc, -O3, -march=native, -flto, -fopenmp
#	                 (sem -pg/ASan, para medir desempenho)
#	- make clang: compila com clang, -O3, -march=native, -flto, -fopenmp
#	- make pgo: compila com clang usando PGO (Profile-Guided Optimization):
#	            instrumenta, executa para gerar o perfil e recompila com ele

WARN     = -Wall -Wextra -Werror -Wno-error=unused-parameter
SRC      = harmonic-sum.c
BIN      = harmonic-sum
OPTFLAGS = -O3 -march=native -flto -DNDEBUG
# clang so usa -fopenmp se libomp estiver instalada (Arch: pacman -S openmp)
CLANG_OMP := $(shell echo 'int main(void){return 0;}' | clang -fopenmp -x c - -o /dev/null 2>/dev/null && echo -fopenmp)

.PHONY: all clean debug optimize clang pgo

all: $(SRC)
	gcc -fopenmp -O0 -g $(WARN) -fno-omit-frame-pointer -pg -fsanitize=address $(SRC) -o $(BIN) -lm

debug: $(SRC)
	gcc -g $(WARN) -fno-omit-frame-pointer -fsanitize=address $(SRC) -o $(BIN) -lm

optimize: $(SRC)
	gcc $(OPTFLAGS) -fopenmp $(WARN) $(SRC) -o $(BIN) -lm

clang: $(SRC)
	clang $(OPTFLAGS) $(CLANG_OMP) $(WARN) $(SRC) -o $(BIN) -lm

pgo: $(SRC)
	clang -O3 -march=native $(CLANG_OMP) -DNDEBUG $(WARN) -fprofile-instr-generate $(SRC) -o $(BIN)-prof -lm
	LLVM_PROFILE_FILE=$(BIN).profraw ./$(BIN)-prof
	llvm-profdata merge -o $(BIN).profdata $(BIN).profraw
	clang $(OPTFLAGS) $(CLANG_OMP) $(WARN) -fprofile-instr-use=$(BIN).profdata $(SRC) -o $(BIN) -lm
	rm -f $(BIN)-prof $(BIN).profraw

clean:
	rm -rf $(BIN) $(BIN)-prof $(BIN).profraw $(BIN).profdata gmon.out
