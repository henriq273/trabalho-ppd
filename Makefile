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
#	- make run: executa com mpirun (NP=processos, ARGS=argumentos do programa)
#	            ex.: make run NP=4 ARGS="--size=100000"
#
# Todos os alvos compilam e linkam com MPI (flags obtidos de "mpicc --showme").

WARN     = -Wall -Wextra -Wno-error=unused-parameter
SRC      = harmonic-sum.c
BIN      = harmonic-sum
OPTFLAGS = -O3 -march=native -flto -DNDEBUG
# clang so usa -fopenmp se libomp estiver instalada (Arch: pacman -S openmp)
CLANG_OMP := $(shell echo 'int main(void){return 0;}' | clang -fopenmp -x c - -o /dev/null 2>/dev/null && echo -fopenmp)

# MPI: flags de include e de link vindos do proprio mpicc (portavel entre
# maquinas/cluster). Compilamos com gcc/clang e nao com mpicc para manter
# a escolha do compilador de cada alvo.
MPI_CFLAGS := $(shell mpicc --showme:compile 2>/dev/null)
MPI_LIBS   := $(shell mpicc --showme:link 2>/dev/null)

# Parametros do alvo "run"
NP   ?= 2
ARGS ?=

.PHONY: all clean debug optimize clang pgo run help check-mpi

# Falha cedo, com mensagem clara, se o MPI nao estiver disponivel
check-mpi:
	@command -v mpicc >/dev/null || { echo "mpicc nao encontrado: instale um MPI (Arch: pacman -S openmpi)"; exit 1; }

all: check-mpi $(SRC)
	gcc -fopenmp -O0 -g $(WARN) $(MPI_CFLAGS) -fno-omit-frame-pointer -pg -fsanitize=address $(SRC) -o $(BIN) -lm $(MPI_LIBS)

debug: check-mpi $(SRC)
	gcc -g $(WARN) $(MPI_CFLAGS) -fno-omit-frame-pointer -fsanitize=address $(SRC) -o $(BIN) -lm $(MPI_LIBS)

optimize: check-mpi $(SRC)
	gcc $(OPTFLAGS) -fopenmp $(WARN) $(MPI_CFLAGS) $(SRC) -o $(BIN) -lm $(MPI_LIBS)

clang: check-mpi $(SRC)
	clang $(OPTFLAGS) $(CLANG_OMP) $(WARN) $(MPI_CFLAGS) $(SRC) -o $(BIN) -lm $(MPI_LIBS)

pgo: check-mpi $(SRC)
	clang -O3 -march=native $(CLANG_OMP) -DNDEBUG $(WARN) $(MPI_CFLAGS) -fprofile-instr-generate $(SRC) -o $(BIN)-prof -lm $(MPI_LIBS)
	LLVM_PROFILE_FILE=$(BIN).profraw ./$(BIN)-prof
	llvm-profdata merge -o $(BIN).profdata $(BIN).profraw
	clang $(OPTFLAGS) $(CLANG_OMP) $(WARN) $(MPI_CFLAGS) -fprofile-instr-use=$(BIN).profdata $(SRC) -o $(BIN) -lm $(MPI_LIBS)
	rm -f $(BIN)-prof $(BIN).profraw

run: check-mpi
	mpirun -np $(NP) ./$(BIN) $(ARGS)

clean:
	rm -rf $(BIN) $(BIN)-prof $(BIN).profraw $(BIN).profdata gmon.out

help:
	@echo "make (default): compila com gcc, -fopenmp, -O0, -g, -Wall, -Wextra, -Werror, -fno-omit-frame-pointer, -pg e -fsanitize=address"
	@echo "make clean: remove arquivos gerados"
	@echo "make debug: compila com gcc, -g, -Wall, -Wextra, -Werror, -fno-omit-frame-pointer e -fsanitize=address"
	@echo "make optimize: compila com gcc, -O3, -march=native, -flto, -fopenmp (sem -pg/ASan, para medir desempenho)"
	@echo "make clang: compila com clang, -O3, -march=native, -flto, -fopenmp"
	@echo "make pgo: compila com clang usando PGO (Profile-Guided Optimization): instrumenta, executa para gerar o perfil e recompila com ele"
	@echo "make run: executa com mpirun (NP=processos, ARGS=argumentos). Ex.: make run NP=4 ARGS=\"--size=100000\""
	@echo "make help: mostra este menu"

# Fallback: qualquer alvo sem regra (ex.: make foo)
.DEFAULT:
	@echo 'comando invalido. Utilize "make help" para mais informações'
