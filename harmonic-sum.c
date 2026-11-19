#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <omp.h>
#include <mpi.h>

#include "input.h"

long unsigned int N = 100000;
int num_threads = 1;
int run_serial = 1;
long unsigned int precision = 20; // casas decimais (d)

double harmonic_sum_serial(long unsigned int n);
int sum(char *output, const size_t cap, const long unsigned int d, const long unsigned int n);

// Espaço mínimo do buffer de saída para d casas decimais:
// até 20 dígitos da parte inteira (unsigned long) + '.' + d casas + '\0' (+ folga)
#define SUM_OUTPUT_CAP(d) ((size_t) (d) + 24)



/*
    Corpo do programa. Roda em todos os ranks; só o rank 0 lê argumentos,
    imprime e calcula. Todos os ranks retornam o mesmo código, para que
    nenhum saia antes do MPI_Finalize nem fique esperando numa coletiva.
 */
static int run(int argc, char *argv[], int rank, int size) {

    // Padrao: quantidade de CPUs disponiveis (sobrescrito por --threads)
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_threads = ncpus > 0 ? (int) ncpus : 1;

    /*
        Parâmetros: só o rank 0 interpreta os argumentos (assim --help e as
        mensagens de erro saem uma vez só) e distribui o resultado a todos.
        status segue o contrato de parse_args (0 continua, 1 --help, -1 erro)
        e vai sozinho, como int, porque pode ser -1 (os demais são unsigned).
        É enviado primeiro para todos os ranks saírem juntos se for diferente de 0.
        params = {N, threads, precision, run_serial}.
     */
    int status = 0;
    long unsigned int params[4] = {0, 0, 0, 0};
    if (rank == 0) {
        status = parse_args(argc, argv, &N, &num_threads, &precision, &run_serial);
        params[0] = N;
        params[1] = (long unsigned int) num_threads;
        params[2] = precision;
        params[3] = (long unsigned int) run_serial;
    }
    // MPI Broadcast: todos os ranks recebem o mesmo status e o mesmo params[4] do rank 0
    MPI_Bcast(&status, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (status != 0) return status > 0 ? 0 : 1; // todos saem juntos

    MPI_Bcast(params, 4, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    N           = params[0];
    num_threads = (int) params[1];
    precision   = params[2];
    run_serial  = (int) params[3];

    if (rank == 0) {
        printf("Tamanho: %lu | Processos: %d | Threads: %d | Precisao: %lu | Serial: %s\n\n",
               N, size, num_threads, precision, run_serial ? "sim" : "nao");

        if (run_serial) {
            double start = MPI_Wtime();
            long double result = harmonic_sum_serial(N);
            double end = MPI_Wtime();

            printf("Soma de %lu números de Harmonica: %.*Lf\n", N, (int) precision, result);
            printf("Tempo serializado: %f\n\n", end - start);
            fflush(stdout);
        }
    }

    // Soma de precisão arbitrária (divisão longa dígito a dígito).
    // Só o rank 0 recebe o resultado, então só ele aloca o buffer de saída.
    // Todos chamam sum (coletiva), mesmo se o malloc falhar: sum vê o output
    // NULL, todos os ranks desistem juntos e recebem o mesmo retorno.
    size_t cap = SUM_OUTPUT_CAP(precision);
    char *precise = rank == 0 ? malloc(cap) : NULL;

    MPI_Barrier(MPI_COMM_WORLD); // todos começam juntos
    double start = MPI_Wtime();
    int rc = sum(precise, cap, precision, N);
    double end = MPI_Wtime();

    if (rc != 0) {
        if (rank == 0) fprintf(stderr, "Falha ao calcular a soma de precisão arbitrária\n");
        free(precise);
        return 1;
    }

    if (rank == 0) {
        printf("Soma de %lu números de Harmonica (precisa): %s\n", N, precise);
        printf("Tempo total (precisa): %f\n", end - start);
    }

    free(precise);
    return 0;
}

int main(int argc, char *argv[]) {
    /*
      MPI_Init_thread: o programa é híbrido (cada rank abre várias threads
      OpenMP), e o MPI_Init comum não informa à biblioteca que existem threads.
      Sem essa declaração, a implementação pode assumir um único fio de execução,
      e o uso com threads fica sem garantia (travamento ou corrupção). 
      MPI_Init_thread permite pedir um nível de suporte a threads e confere,
      em "provided", o que foi concedido.
     
      FUNNELED: há várias threads, mas só a principal chama MPI. Isso
      vale aqui porque toda chamada MPI (Bcast, Reduce, Barrier, Wtime) fica
      fora das regiões "#pragma omp parallel". É o nível mais barato que
      cobre o nosso uso: SERIALIZED e MULTIPLE só seriam necessários se mais
      de uma thread chamasse MPI, e MULTIPLE ainda custa travas internas.
     */
    int provided, rank, size;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (provided < MPI_THREAD_FUNNELED && rank == 0) {
        fprintf(stderr, "Aviso: MPI nao oferece MPI_THREAD_FUNNELED (nivel %d); "
                        "o uso combinado com OpenMP pode ser inseguro\n", provided);
    }

    int rc = run(argc, argv, rank, size);

    MPI_Finalize();
    return rc;
}


/*
    Bloco contíguo de denominadores [*begin, *end] (1-based) deste rank.
    Cada rank leva chunk_size = n / size denominadores, e os (n % size)
    primeiros ranks levam um a mais, então os blocos cobrem 1..n sem buraco
    nem sobreposição e diferem em no máximo 1 elemento.
    Se n < size, os últimos ranks ficam com bloco vazio (*end == *begin - 1),
    e o laço de quem recebe esse intervalo não executa nenhuma iteração.
 */
static void block_range(const long unsigned int n, const int rank, const int size,
                        long unsigned int *begin, long unsigned int *end) {
    const long unsigned int r = (long unsigned int) rank;
    const long unsigned int p = (long unsigned int) size;

    const long unsigned int chunk_size = n / p;  // denominadores que todo rank leva
    const long unsigned int extra = n % p;       // ranks [0, extra) levam mais um
    const long unsigned int count = chunk_size + (r < extra ? 1 : 0);

    // Antes do rank r há r blocos cheios, mais 1 extra para cada rank anterior
    // que também levou um a mais (min(r, extra) deles).
    *begin = r * chunk_size + (r < extra ? r : extra) + 1;
    *end = *begin + count - 1;
}

/*
    Acumula em digits (d + 11 posições) a divisão longa de 1/i para cada i em
    [i_begin, i_end]. Paralelizado com OpenMP (redução de array).
    digits deve estar zerado (ou conter uma soma parcial anterior).
 
    Obs.: as cópias privadas criadas pelo reduction(+:digits[:d + 11]) ficam
    na pilha de cada thread, mesmo com digits no heap. Se ainda estourar,
    precisa aumentar OMP_STACKSIZE (ex.: export OMP_STACKSIZE=64M).
 */
static void sum_partial(long unsigned int *digits, const long unsigned int d,
                        const long unsigned int i_begin, const long unsigned int i_end) {

    #pragma omp parallel for num_threads(num_threads) reduction(+:digits[:d + 11]) schedule(guided)
    for (long unsigned int i = i_begin; i <= i_end; ++i) {
        long unsigned int remainder = 1;
        for (long unsigned int digit = 0; digit < d + 11 && remainder; ++digit) {
            long unsigned int div = remainder / i;
            long unsigned int mod = remainder % i;
            digits[digit] += div;
            remainder = mod * 10;
        }
    }
}

/*
    Propaga o carry, arredonda na casa d e formata digits como "I.ddd...d"
    em output (capacidade cap). Sequencial: o carry depende da posição vizinha,
    por isso só o rank 0 executa isto, depois do MPI_Reduce.
 */
static void sum_finalize(char *output, const size_t cap,
                         long unsigned int *digits, const long unsigned int d) {

    for (long unsigned int i = d + 11 - 1; i > 0; --i) {
        digits[i - 1] += digits[i] / 10;
        digits[i] %= 10;
    }

    if (digits[d + 1] >= 5) {
        ++digits[d];
    }

    for (long unsigned int i = d; i > 0; --i) {
        digits[i - 1] += digits[i] / 10;
        digits[i] %= 10;
    }

    size_t pos = 0;
    pos += (size_t) snprintf(output + pos, cap - pos, "%lu.", digits[0]);
    for (long unsigned int i = 1; i <= d; ++i) {
        output[pos++] = (char) ('0' + digits[i]); // cada digits[i] já está entre 0 e 9
    }
    output[pos] = '\0';
}

/*
    Calcula H(n) com d casas decimais usando MPI + OpenMP.

    COLETIVA: deve ser chamada por todos os ranks, com os mesmos d e n.
     - Cada rank soma o seu bloco de i (OpenMP dentro do rank).
     - MPI_Reduce(SUM) junta os vetores de dígitos no rank 0.
     - O rank 0 faz carry, arredondamento e formatação em output (capacidade cap).
    Só o rank 0 precisa passar output/cap válidos; nos demais, output = NULL.

    Retorna 0 em sucesso, -1 em falha (o MESMO valor em todos os ranks).
 */
int sum (char *output, const size_t cap, const long unsigned int d, const long unsigned int n) {

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // O MPI_Reduce usa int para a contagem: d + 11 precisa caber em int
    const int fits_int = d + 11 <= (long unsigned int) INT_MAX;

    // Para d grande (evita estouro de pilha com VLA):
    // digits no heap com calloc, que já devolve a memória zerada
    long unsigned int *digits = fits_int ? calloc(d + 11, sizeof(long unsigned int)) : NULL;

    /*
        Falhas locais (alocação, buffer do rank 0) viram decisão global, para
        nenhum rank ficar esperando no MPI_Reduce de um rank que já parou.

        MPI_Allreduce(&ok, &all_ok, 1, MPI_INT, MPI_MIN, ...): cada rank envia
        o seu "ok" (1 = tudo certo, 0 = falhou) e todos recebem em all_ok o
        mínimo entre os valores de todos os ranks. Basta um rank com ok = 0
        para all_ok ser 0 em todos, e então todos desistem juntos.
     */
    int ok = digits != NULL && (rank != 0 || (output != NULL && cap >= SUM_OUTPUT_CAP(d)));
    int all_ok;
    MPI_Allreduce(&ok, &all_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_ok) {
        free(digits);
        return -1;
    }

    // Bloco de i deste rank (vazio se n < size)
    long unsigned int i_begin, i_end;
    block_range(n, rank, size, &i_begin, &i_end);

    sum_partial(digits, d, i_begin, i_end);

    /*
        Soma elemento a elemento dos vetores de todos os ranks, no rank 0:
        digits[k] do rank 0 passa a ser digits[k] de todos os ranks somados.
        Nenhuma posição estoura: cada uma soma no máximo n * 9.

        MPI_Reduce(sendbuf, recvbuf, count, tipo, MPI_SUM, raiz, ...) lê count
        elementos de sendbuf em cada rank e grava a soma em recvbuf, só na raiz.

        MPI_IN_PLACE (só na raiz): normalmente a raiz tem um buffer de envio e
        outro de recebimento. Com MPI_IN_PLACE no lugar do sendbuf, a raiz
        diz "meu vetor de entrada já está em recvbuf (digits), e o resultado
        sobrescreve ele mesmo". Isso evita alocar e copiar um segundo vetor de
        d + 11 posições no rank 0, e passar o mesmo ponteiro nos dois
        argumentos seria erro do MPI (buffers sobrepostos).
     */
    const int len = (int) (d + 11);
    if (rank == 0) {
        MPI_Reduce(MPI_IN_PLACE, digits, len, MPI_UNSIGNED_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        sum_finalize(output, cap, digits, d);
    } else {
        MPI_Reduce(digits, NULL, len, MPI_UNSIGNED_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    }

    free(digits);
    return 0;
}



double harmonic_sum_serial(long unsigned int n) {
    double serial_sum = 0.0;
    for (long unsigned int i = 1; i <= n; i++) {
        serial_sum += 1.0 / i;
    }
    return serial_sum;
}