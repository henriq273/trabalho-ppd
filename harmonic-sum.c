
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <omp.h>

#if defined(__has_include)
#  if __has_include(<mpi.h>)
#    include <mpi.h>
#  endif
#else
#  ifdef MPI_H
#    include <mpi.h>
#  endif
#endif

long unsigned int N = 100000; // n: número de termos
int num_threads = 1;
int run_serial = 1;
long unsigned int precision = 20; // d: casas decimais

/*
 * Referência serial em long double. n (long unsigned int) é convertido para
 * long double, e o contador do laço também é long double (a mantissa de 64 bits
 * do x86 representa inteiros até 2^64 sem perda). Só aqui os valores viram
 * ponto flutuante; no restante do programa n e d são long unsigned int.
 */
static long double harmonic_sum_serial(const long unsigned int n) {
    const long double ld_n = (long double) n;
    long double serial_sum = 0.0L;
    for (long double i = 1.0L; i <= ld_n; i += 1.0L) {
        serial_sum += 1.0L / i;
    }
    return serial_sum;
}

/*
 * Bloco contíguo de denominadores [*begin, *end] (1-based) deste rank:
 * n/size para todos, e os (n % size) primeiros ranks levam um a mais.
 * O intervalo é vazio (*end == *begin - 1) quando n < size.
 */
static void block_range(const long unsigned int n, const int rank, const int size,
                        long unsigned int *begin, long unsigned int *end) {
    const long unsigned int r = (long unsigned int) rank;
    const long unsigned int p = (long unsigned int) size;
    const long unsigned int base = n / p, extra = n % p;
    const long unsigned int count = base + (r < extra ? 1 : 0);
    *begin = r * base + (r < extra ? r : extra) + 1;
    *end = *begin + count - 1;
}

// Maior valor de t entre todos os ranks (válido no rank 0): o tempo do
// programa é o do rank mais lento.
static double max_time(const double t) {
    double m = t;
    MPI_Reduce(&t, &m, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    return m;
}

/*
 * Soma harmônica em long double com MPI + OpenMP.
 * COLETIVA: todos os ranks chamam com o mesmo n; o resultado só é válido no rank 0.
 * Cada rank soma seu bloco de i (OpenMP dentro do rank) e MPI_Reduce(SUM) junta.
 * A ordem das parcelas depende de P e de T, então o resultado pode diferir
 * nos últimos bits entre configurações (soma de ponto flutuante não é associativa).
 */
static long double harmonic_sum_parallel(const long unsigned int n) {
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    long unsigned int i_begin, i_end;
    block_range(n, rank, size, &i_begin, &i_end);

    long double local = 0.0L;
    #pragma omp parallel for num_threads(num_threads) reduction(+:local) schedule(guided)
    for (long unsigned int i = i_begin; i <= i_end; ++i) {
        local += 1.0L / (long double) i;
    }

    long double total = 0.0L;
    MPI_Reduce(&local, &total, 1, MPI_LONG_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    return total;
}

// Espaço mínimo do buffer de saída para d casas decimais:
// até 20 dígitos da parte inteira (unsigned long) + '.' + d casas + '\0' (+ folga)
#define SUM_OUTPUT_CAP(d) ((size_t) (d) + 24)

/*
 * Acumula em digits (d + 11 posições) a divisão longa de 1/i para cada
 * i em [i_begin, i_end]. Paralelizado com OpenMP (redução de array).
 * digits deve estar zerado (ou conter uma soma parcial anterior).
 */
static void sum_partial(long unsigned int *digits, const long unsigned int d,
                        const long unsigned int i_begin, const long unsigned int i_end) {

    /*
     * Obs.: as cópias privadas criadas pelo reduction(+:digits[:d + 11]) ficam
     * na pilha de cada thread, mesmo com digits no heap. Se ainda estourar,
     * precisa aumentar OMP_STACKSIZE (ex.: export OMP_STACKSIZE=64M).
    */

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
 * Propaga o carry, arredonda na casa d e formata digits como "I.ddd...d"
 * em output (capacidade cap). Sequencial: o carry depende da posição vizinha.
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
 * Calcula H(n) com d casas decimais usando MPI + OpenMP.
 *
 * COLETIVA: deve ser chamada por TODOS os ranks com os mesmos d e n.
 *  - Cada rank soma um bloco contíguo de denominadores i (OpenMP dentro do rank).
 *  - MPI_Reduce(SUM) junta os vetores de dígitos no rank 0.
 *  - O rank 0 faz carry, arredondamento e formatação em output (capacidade cap).
 * Só o rank 0 precisa passar output/cap válidos; nos demais, output = NULL.
 *
 * Retorna 0 em sucesso, -1 em falha (o MESMO valor em todos os ranks).
 */
int sum (char *output, const size_t cap, const long unsigned int d, const long unsigned int n) {

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    long unsigned int *digits = calloc(d + 11, sizeof(long unsigned int));

    // Falhas locais (alocação, buffer do rank 0) viram decisão global, para
    // nenhum rank ficar esperando no MPI_Reduce de um rank que já desistiu.
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

    // Soma elemento a elemento dos vetores de todos os ranks no rank 0
    // MPI usa int para a contagem; d <= INT_MAX - 11 (validado em --precision) cabe
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

/*
    Argumentos:
        --size=N: define o tamanho do vetor a ser somado
        Uso: ./harmonic-sum --size=1000

        --threads=T: define o número de threads a ser utilizado
        Uso: ./harmonic-sum --threads=4

        --precision=D: define o número de casas decimais (padrão: 20)
        Uso: ./harmonic-sum --precision=50

        --no-serial: desabilita a execução serial
        Uso: ./harmonic-sum --no-serial

    Execução com MPI: mpirun -np P ./harmonic-sum [argumentos]
        (--threads é o número de threads OpenMP POR processo;
         padrão: CPUs do nó / processos no nó)
*/

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s [--size=N] [--threads=T] [--precision=D] [--no-serial]\n", prog);
}

// Converte str em inteiro positivo; retorna 0 em caso de erro.
// Exige começar por dígito (strtoul aceitaria "-5" e espaços, com wraparound).
static int parse_positive(const char *str, long unsigned int *out) {
    char *end;
    if (*str < '0' || *str > '9') return 0;
    errno = 0;
    long unsigned int v = strtoul(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0' || v == 0) return 0;
    *out = v;
    return 1;
}

// Corpo do programa. Roda em todos os ranks; só o rank 0 imprime.
static int run(int argc, char *argv[], int rank, int size) {

    // Padrao: CPUs do no divididas pelos ranks que dividem esse no, para
    // P ranks x T threads nao exceder o numero de CPUs (sobrescrito por --threads).
    // MPI_COMM_TYPE_SHARED agrupa os ranks da mesma maquina, entao funciona
    // tambem em cluster com varios nos.
    MPI_Comm node_comm;
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &node_comm);
    int ranks_per_node = 1;
    MPI_Comm_size(node_comm, &ranks_per_node);
    MPI_Comm_free(&node_comm);

    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_threads = ncpus > 0 ? (int) (ncpus / ranks_per_node) : 1;
    if (num_threads < 1) num_threads = 1;

    // Os argumentos podem aparecer em qualquer ordem
    for (int i = 1; i < argc; i++) {
        long unsigned int v;
        if (strncmp(argv[i], "--size=", 7) == 0) {
            if (!parse_positive(argv[i] + 7, &v)) {
                if (rank == 0) fprintf(stderr, "Valor invalido para --size: '%s'\n", argv[i] + 7);
                return 1;
            }
            N = v;
        } else if (strncmp(argv[i], "--threads=", 10) == 0) {
            if (!parse_positive(argv[i] + 10, &v) || v > 65536) {
                if (rank == 0) fprintf(stderr, "Valor invalido para --threads: '%s'\n", argv[i] + 10);
                return 1;
            }
            num_threads = (int) v;
        } else if (strncmp(argv[i], "--precision=", 12) == 0) {
            if (!parse_positive(argv[i] + 12, &v) || v > 2147483636UL) { // INT_MAX - 11: d + 11 precisa caber no int do MPI_Reduce
                if (rank == 0) fprintf(stderr, "Valor invalido para --precision: '%s'\n", argv[i] + 12);
                return 1;
            }
            precision = v;
        } else if (strcmp(argv[i], "--no-serial") == 0) {
            run_serial = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            if (rank == 0) usage(argv[0]);
            if (rank == 0) printf("Argumentos:\n"
                   "  --size=N: define o tamanho do vetor a ser somado\n"
                   "    Uso: ./harmonic-sum --size=1000\n\n"
                   "  --threads=T: define o número de threads a ser utilizado\n"
                   "    Uso: ./harmonic-sum --threads=4\n\n"
                   "  --precision=D: define o número de casas decimais (padrão: 20)\n"
                   "    Uso: ./harmonic-sum --precision=50\n\n"
                   "  --no-serial: desabilita a comparação com a execução serial\n"
                   "    Uso: ./harmonic-sum --no-serial\n");
            return 0; 
        } else {
            if (rank == 0) {
                fprintf(stderr, "Argumento desconhecido: '%s'\n"
                    "Para mais informações, utilize \"--help\"\n", argv[i]);
                usage(argv[0]);
            }
            return 1;
        }
    }

    if (rank == 0) {
        printf("Tamanho: %lu | Processos: %d | Threads: %d | Precisao: %lu | Serial: %s\n\n",
               N, size, num_threads, precision, run_serial ? "sim" : "nao");
    }

    if (run_serial && rank == 0) {
        double start = MPI_Wtime();
        long double result = harmonic_sum_serial(N);
        double end = MPI_Wtime();

        printf("Soma de %lu números de Harmonica: %.100Lf\n", N, result);
        printf("Tempo serializado: %f\n\n", end - start);
        fflush(stdout);
    }

    // Soma em long double (MPI + OpenMP)
    MPI_Barrier(MPI_COMM_WORLD); // todos começam juntos
    double start = MPI_Wtime();
    long double ld_sum = harmonic_sum_parallel(N);
    double ld_time = max_time(MPI_Wtime() - start);

    if (rank == 0) {
        printf("Soma de %lu números de Harmonica (long double): %.100Lf\n", N, ld_sum);
        printf("Tempo total (long double): %f\n\n", ld_time);
        fflush(stdout);
    }

    // Soma de precisão arbitrária (divisão longa dígito a dígito).
    // Só o rank 0 recebe o resultado, então só ele aloca o buffer de saída.
    size_t cap = SUM_OUTPUT_CAP(precision);
    char *precise = rank == 0 ? malloc(cap) : NULL;

    MPI_Barrier(MPI_COMM_WORLD);
    start = MPI_Wtime();
    int rc = sum(precise, cap, precision, N);
    double precise_time = max_time(MPI_Wtime() - start);

    if (rc != 0) {
        if (rank == 0) fprintf(stderr, "Falha ao calcular a soma de precisão arbitrária\n");
        free(precise);
        return 1;
    }

    if (rank == 0) {
        printf("Soma de %lu números de Harmonica (precisa): %s\n", N, precise);
        printf("Tempo total (precisa): %f\n", precise_time);
    }

    free(precise);
    return 0;
}

int main(int argc, char *argv[]) {
    // FUNNELED: só a thread principal chama MPI (sempre fora das regiões OpenMP)
    int provided, rank, size;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (provided < MPI_THREAD_FUNNELED && rank == 0) {
        fprintf(stderr, "Aviso: MPI nao oferece MPI_THREAD_FUNNELED (nivel %d); "
                        "o uso combinado com OpenMP pode ser inseguro\n", provided);
    }

    double prog_start = 0, prog_end = 0;

    prog_start = MPI_Wtime();
    int rc = run(argc, argv, rank, size);
    prog_end = MPI_Wtime();

    if (rank == 0) {
        printf("\nTempo total do programa: %f\n", prog_end - prog_start);
    }

    MPI_Finalize();
    return rc;
}
