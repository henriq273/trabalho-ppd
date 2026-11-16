
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

long long int N = 100000;
int num_threads = 1;
int run_serial = 1;
long long int precision = 20; // casas decimais (d)

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

// Espaço mínimo do buffer de saída para d casas decimais:
// até 20 dígitos da parte inteira (unsigned long) + '.' + d casas + '\0' (+ folga)
#define SUM_OUTPUT_CAP(d) ((size_t) (d) + 24)

// Calcula H(n) com d casas decimais e grava em output (capacidade cap).
// Retorna 0 em caso de sucesso, -1 se cap for insuficiente.
int sum (char *output, const size_t cap, const long unsigned int d, const long unsigned int n) {

    if (cap < SUM_OUTPUT_CAP(d)) return -1;

    long unsigned int *digits = calloc(d + 11, sizeof(long unsigned int));
    if (digits == NULL) return -1;

    /*
     * Para d grande (evita estouro de pilha com VLA):
     * alocar digits no heap com calloc, que já devolve a memória zerada
     * 
     * Obs.: as cópias privadas criadas pelo reduction(+:digits[:d + 11]) ficam
     * na pilha de cada thread, mesmo com digits no heap. Se ainda estourar,
     * precisa aumentar OMP_STACKSIZE (ex.: export OMP_STACKSIZE=64M).
    */

    #pragma omp parallel for num_threads(num_threads) reduction(+:digits[:d + 11]) schedule(guided)
	for (long unsigned int i = 1; i <= n; ++i) {
		long unsigned int remainder = 1;
		for (long unsigned int digit = 0; digit < d + 11 && remainder; ++digit) {
			long unsigned int div = remainder / i;
			long unsigned int mod = remainder % i;
			digits[digit] += div;
			remainder = mod * 10;
		}
	}

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
    pos += snprintf(output + pos, cap - pos, "%lu.", digits[0]);
    for (long unsigned int i = 1; i <= d; ++i) {
        output[pos++] = '0' + digits[i]; // cada digits[i] já está entre 0 e 9
    }
    output[pos] = '\0';

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
*/

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s [--size=N] [--threads=T] [--precision=D] [--no-serial]\n", prog);
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
        } else if (strncmp(argv[i], "--precision=", 12) == 0) {
            if (!parse_positive(argv[i] + 12, &v) || v > 100000) {
                fprintf(stderr, "Valor invalido para --precision: '%s'\n", argv[i] + 12);
                return 1;
            }
            precision = v;
        } else if (strcmp(argv[i], "--no-serial") == 0) {
            run_serial = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            printf("Argumentos:\n"
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
            fprintf(stderr, "Argumento desconhecido: '%s'\n"
                "Para mais informações, utilize \"--help\"\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("Tamanho: %lld | Threads: %d | Precisao: %lld | Serial: %s\n\n", N, num_threads, precision, run_serial ? "sim" : "nao");

    if (run_serial) {
        double start = get_time();
        long double result = harmonic_sum_serial(N);
        double end = get_time();

        printf("Soma de %lld números de Harmonica: %.*Lf\n", N, (int) precision, result);
        printf("Tempo serializado: %f\n\n", end - start);
        fflush(stdout);
    }

    // Soma de precisão arbitrária (divisão longa dígito a dígito)
    size_t cap = SUM_OUTPUT_CAP(precision);
    char *precise = malloc(cap);
    if (precise == NULL) {
        fprintf(stderr, "Falha ao alocar %zu bytes para o resultado\n", cap);
        return 1;
    }

    double start = 0, end = 0;

    start = get_time();
    if (sum(precise, cap, (long unsigned int) precision, (long unsigned int) N) != 0) {
        fprintf(stderr, "Falha ao calcular a soma de precisão arbitrária\n");
        free(precise);
        return 1;
    }
    end = get_time();

    printf("Soma de %lld números de Harmonica (precisa): %s\n", N, precise);
    printf("Tempo total (precisa): %f\n", end - start);

    free(precise);
    return 0;
}
