#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
// #include <omp.h>

int N = 1000;

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + (double) tv.tv_usec * 1e-6;
}

double harmonic_sum_serial(int n) {
    double sum = 0;
    for (int i = 1; i <= n; i++) {
        sum += 1.0 / i;
    }
    return sum;
}

int main(int argc, char *argv[]) {

    double start = get_time();
    double result = harmonic_sum_serial(N);
    double end = get_time();

    printf("Soma de %d números de Harmonica: %f\n", N, result);
    printf("Tempo serializado: %f\n", end - start);

    return 0;
}