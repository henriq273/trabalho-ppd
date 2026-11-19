#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <omp.h>
#include <mpi.h>

#include "input.h"

long long int N = 100000;
int num_threads = 1;
int run_serial = 1;
long long int precision = 20; // casas decimais (d)

double harmonic_sum_serial(long long int n);
int sum(char *output, const size_t cap, const long unsigned int d, const long unsigned int n);

// Espaço mínimo do buffer de saída para d casas decimais:
// até 20 dígitos da parte inteira (unsigned long) + '.' + d casas + '\0' (+ folga)
#define SUM_OUTPUT_CAP(d) ((size_t) (d) + 24)



/*
 * Corpo do programa. Roda em todos os ranks; só o rank 0 lê argumentos,
 * imprime e calcula. Todos os ranks retornam o mesmo código, para que
 * nenhum saia antes do MPI_Finalize nem fique esperando numa coletiva.
 */
static int run(int argc, char *argv[], int rank, int size) {

    // Padrao: quantidade de CPUs disponiveis (sobrescrito por --threads)
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    num_threads = ncpus > 0 ? (int) ncpus : 1;

    /*
     * Parâmetros: só o rank 0 interpreta os argumentos (assim --help e as
     * mensagens de erro saem uma vez só) e distribui o resultado a todos.
     * params = {status, N, threads, precision, run_serial}; status segue o
     * contrato de parse_args (0 continua, 1 --help, -1 erro).
     */
    long long params[5] = {0, 0, 0, 0, 0};
    if (rank == 0) {
        params[0] = parse_args(argc, argv, &N, &num_threads, &precision, &run_serial);
        params[1] = N;
        params[2] = num_threads;
        params[3] = precision;
        params[4] = run_serial;
    }
    MPI_Bcast(params, 5, MPI_LONG_LONG, 0, MPI_COMM_WORLD);

    if (params[0] != 0) return params[0] > 0 ? 0 : 1; // todos saem juntos
    N          = params[1];
    num_threads = (int) params[2];
    precision  = params[3];
    run_serial = (int) params[4];

    if (rank == 0) {
        printf("Tamanho: %lld | Processos: %d | Threads: %d | Precisao: %lld | Serial: %s\n\n",
               N, size, num_threads, precision, run_serial ? "sim" : "nao");

        if (run_serial) {
            double start = MPI_Wtime();
            long double result = harmonic_sum_serial(N);
            double end = MPI_Wtime();

            printf("Soma de %lld números de Harmonica: %.*Lf\n", N, (int) precision, result);
            printf("Tempo serializado: %f\n\n", end - start);
            fflush(stdout);
        }
    }

    // Soma de precisão arbitrária (divisão longa dígito a dígito).
    // só o rank 0 calcula (sum ainda não divide o trabalho entre ranks);
    // o resultado (rc) é enviado a todos para eles terminarem com o mesmo código.
    int rc = 0;
    if (rank == 0) {
        size_t cap = SUM_OUTPUT_CAP(precision);
        char *precise = malloc(cap);
        if (precise == NULL) {
            fprintf(stderr, "Falha ao alocar %zu bytes para o resultado\n", cap);
            rc = 1;
        } else {
            double start = MPI_Wtime();
            if (sum(precise, cap, (long unsigned int) precision, (long unsigned int) N) != 0) {
                fprintf(stderr, "Falha ao calcular a soma de precisão arbitrária\n");
                rc = 1;
            } else {
                double end = MPI_Wtime();
                printf("Soma de %lld números de Harmonica (precisa): %s\n", N, precise);
                printf("Tempo total (precisa): %f\n", end - start);
            }
            free(precise);
        }
    }
    MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return rc;
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

    int rc = run(argc, argv, rank, size);

    MPI_Finalize();
    return rc;
}


// Calcula H(n) com d casas decimais e grava em output (capacidade cap).
// Retorna 0 em caso de sucesso, -1 se cap for insuficiente.
int sum (char *output, const size_t cap, const long unsigned int d, const long unsigned int n) {

    if (cap < SUM_OUTPUT_CAP(d)) return -1;

    long unsigned int *digits = calloc(d + 11, sizeof(long unsigned int));
    if (digits == NULL) return -1;

    /*
        Para d grande (evita estouro de pilha com VLA):
        alocar digits no heap com calloc, que já devolve a memória zerada
    
        Obs.: as cópias privadas criadas pelo reduction(+:digits[:d + 11]) ficam
        na pilha de cada thread, mesmo com digits no heap. Se ainda estourar,
        precisa aumentar OMP_STACKSIZE (ex.: export OMP_STACKSIZE=64M).
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



double harmonic_sum_serial(long long int n) {
    double serial_sum = 0.0;
    for (long long int i = 1; i <= n; i++) {
        serial_sum += 1.0 / i;
    }
    return serial_sum;
}