#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result);

int main()
{
    double x[N] = {0, M_PI/6., M_PI/3., 0.134};
    double res[N];

    sinx_taylor(N, 3, x, res);

    for (int i = 0; i < N; i++) {
        printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
        printf("sin(%.2f) = %f\n\n", x[i], sin(x[i]));
    }

    return 0;
}

// ----------------------------------------------
// 자식 프로세스가 테일러 급수로 sin(x) 계산
// ----------------------------------------------
void sinx_taylor(int num_elements, int terms, double* x, double* result)
{
    int fd[2 * N];  // 각 프로세스마다 pipe 두 개씩

    for (int i = 0; i < num_elements; i++) {
        pipe(fd + 2 * i);

        pid_t pid = fork();

        if (pid == 0) {
            // 자식 프로세스: 결과 계산 후 부모에 전송
            close(fd[2 * i]);  // 읽기 닫기

            double val = x[i];
            double sum = val; // 첫 항 (x)
            double power = val; 
            double fact = 1.0;
            int sign = -1;

            for (int n = 1; n < terms; n++) {
                power *= val * val;
                fact *= (2 * n) * (2 * n + 1);
                sum += sign * (power / fact);
                sign *= -1;
            }

            // 결과 전송
            write(fd[2 * i + 1], &sum, sizeof(double));
            close(fd[2 * i + 1]);
            exit(0);
        } else {
            // 부모 프로세스
            close(fd[2 * i + 1]); // 쓰기 닫기
        }
    }

    // 부모가 모든 자식 결과 받기
    for (int i = 0; i < num_elements; i++) {
        read(fd[2 * i], &result[i], sizeof(double));
        close(fd[2 * i]);
        wait(NULL);
    }
}

