// Multiplies two N x N matrices the textbook way. A is read across rows,
// but B is read down columns, jumping N * 8 bytes each step.
#include <stdio.h>

#define N 64
static double A[N][N], B[N][N], C[N][N];

int main(void) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            A[i][j] = i + j;
            B[i][j] = i - j;
        }

    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            double sum = 0;
            for (int k = 0; k < N; k++) sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }

    printf("%f\n", C[N / 2][N / 2]);
    return 0;
}
