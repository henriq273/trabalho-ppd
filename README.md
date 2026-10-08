# Soma da Série Harmônica - Trabalho de PPD

## Resumo

Este é um programa simples em C que calcula a soma da série de números de Harmonica:

$$
\sum_{i=1}^{n} \frac{1}{i}
$$

## Compilação

Para compilar, basta executar `make` no terminal. O programa será compilado com o GCC, utilizando as opções `-fopenmp`, `-O0`, `-g`, `-Wall`, `-Wextra`, `-fno-omit-frame-pointer` e `-fsanitize=address`.

Para compilar com o Clang, basta executar `make clang` no terminal. O programa será compilado com o Clang, utilizando as opções `-O3`, `-march=native`, `-flto`, `-fopenmp`.

Para compilar com o GCC e utilizar o PGO (Profile-Guided Optimization), basta executar `make pgo` no terminal. O programa será compilado com o GCC, utilizando as opções `-O3`, `-march=native`, `-flto`, `-fopenmp`. Em seguida, o programa será executado para gerar o perfil e recompilado com ele.

## Uso

O programa pode ser executado com os seguintes argumentos:

- `--size=N`: define o tamanho do vetor a ser somado. O padrão é `100000`.
- `--threads=T`: define o número de threads a ser utilizado. O padrão é o número de CPUs disponíveis.
- `--no-serial`: desabilita a execução serial. O padrão é ativado.

Por exemplo, para executar o programa com o tamanho de vetor `10000` e `4` threads, basta executar:

```bash
./harmonic-sum --size=10000 --threads=4
```

## Exemplo de uso

```bash
$ ./harmonic-sum --size=10000 --threads=4
Tamanho: 10000 | Threads: 4 | Serial: sim

Soma de 10000 números de Harmonica: 0.9999999999999999
Tempo serializado: 0.001000

Soma de 10000 números de Harmonica: 0.9999999999999999
Tempo total: 0.001000
```

## Referências

- https://www.gnu.org/software/libc/manual/html_node/Profiling-with-gprof.html
- https://www.gnu.org/software/libc/manual/html_node/Instrumentation-with-gprof.html
- https://www.gnu.org/software/libc/manual/html_node/Profiling-with-gprof.html#Profiling-with-gprof
- https://www.gnu.org/software/libc/manual/html_node/Instrumentation-with-gprof.html#Instrumentation-with-gprof
