
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <omp.h>

long long int N = 100000;
int num_threads = 1;
int run_serial = 1;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + (double) tv.tv_usec * 1e-6;
}

double harmonic_sum_serial(long long int n) {
    double serial_sum = 0.0;
    for (long long int i = 1; i <= n; i++) {
        serial_sum += 1.0 / i;
    }
    return serial_sum;
}

/*
    Argumentos:
        --size=N: define o tamanho do vetor a ser somado
        Uso: ./harmonic-sum --size=1000

        --threads=T: define o número de threads a ser utilizado
        Uso: ./harmonic-sum --threads=4

        --no-serial: desabilita a execução serial
        Uso: ./harmonic-sum --no-serial
*/

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s [--size=N] [--threads=T] [--no-serial]\n", prog);
}

// Converte str em inteiro positivo; retorna 0 em caso de erro.
static int parse_positive(const char *str, long long *out) {
    char *end;
    errno = 0;
    long long v = strtoll(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0' || v <= 0) return 0;
    *out = v;
    return 1;
}

int main(int argc, char *argv[]) {
    // Padrao: quantidade de CPUs disponiveis (sobrescrito por --threads)
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_threads = ncpus > 0 ? (int) ncpus : 1;

    // Os argumentos podem aparecer em qualquer ordem
    for (int i = 1; i < argc; i++) {
        long long v;
        if (strncmp(argv[i], "--size=", 7) == 0) {
            if (!parse_positive(argv[i] + 7, &v)) {
                fprintf(stderr, "Valor invalido para --size: '%s'\n", argv[i] + 7);
                return 1;
            }
            N = v;
        } else if (strncmp(argv[i], "--threads=", 10) == 0) {
            if (!parse_positive(argv[i] + 10, &v) || v > 65536) {
                fprintf(stderr, "Valor invalido para --threads: '%s'\n", argv[i] + 10);
                return 1;
            }
            num_threads = (int) v;
        } else if (strcmp(argv[i], "--no-serial") == 0) {
            run_serial = 0;
        } else {
            fprintf(stderr, "Argumento desconhecido: '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("Tamanho: %lld | Threads: %d | Serial: %s\n\n", N, num_threads, run_serial ? "sim" : "nao");

    if (run_serial) {
        double start = get_time();
        long double result = harmonic_sum_serial(N);
        double end = get_time();

        printf("Soma de %lld números de Harmonica: %.20Lf\n", N, result);
        printf("Tempo serializado: %f\n\n", end - start);
        fflush(stdout);
    }

    long double sum = 0.0;
    double start = get_time();
    #pragma omp parallel num_threads(num_threads) shared(sum)
    {
        // double start = get_time();

        #pragma omp for reduction(+:sum) schedule(guided)
        for (long long int i = 1; i <= N; i++) {
            sum += 1.0 / i;
        }

        // double end = get_time();

        /*
        #pragma omp critical
        {
            printf("Thread %d: Soma de %lld números de Harmonica: %f\n", omp_get_thread_num(), N, sum);
            printf("Thread %d: Tempo paralelo: %f\n", omp_get_thread_num(), end - start);
        }
        */
    }
    double end = get_time();

    printf("Soma de %lld números de Harmonica: %.20Lf\n", N, sum);
    printf("Tempo total: %f\n", end - start);

    return 0;
}
