#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

/*
 * Lê os argumentos da linha de comando (em qualquer ordem) e grava nos
 * ponteiros recebidos; o que não for informado mantém o valor que já tinha.
 *
 * Retorna:
 *    0  sucesso, o programa deve continuar
 *    1  --help foi pedido e impresso, o programa deve terminar com código 0
 *   -1  argumento inválido (mensagem já impressa), o programa deve terminar com código 1
 */
static int parse_args(int argc, char *argv[], long unsigned int *n, int *threads,
                      long unsigned int *precision, int *run_serial) {
    for (int i = 1; i < argc; i++) {
        long unsigned int v;
        if (strncmp(argv[i], "--size=", 7) == 0) {
            if (!parse_positive(argv[i] + 7, &v)) {
                fprintf(stderr, "Valor invalido para --size: '%s'\n", argv[i] + 7);
                return -1;
            }
            *n = v;
        } else if (strncmp(argv[i], "--threads=", 10) == 0) {
            if (!parse_positive(argv[i] + 10, &v) || v > 65536) {
                fprintf(stderr, "Valor invalido para --threads: '%s'\n", argv[i] + 10);
                return -1;
            }
            *threads = (int) v;
        } else if (strncmp(argv[i], "--precision=", 12) == 0) {
            if (!parse_positive(argv[i] + 12, &v) || v > 100000) {
                fprintf(stderr, "Valor invalido para --precision: '%s'\n", argv[i] + 12);
                return -1;
            }
            *precision = v;
        } else if (strcmp(argv[i], "--no-serial") == 0) {
            *run_serial = 0;
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
            return 1;
        } else {
            fprintf(stderr, "Argumento desconhecido: '%s'\n"
                "Para mais informações, utilize \"--help\"\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

#endif
