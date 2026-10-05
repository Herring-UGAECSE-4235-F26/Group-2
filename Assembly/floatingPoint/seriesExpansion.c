#include <stdio.h>

extern float exp_n(float x, int n);
extern float n_factorial(int n);

int main() {
    float x;
    float e_x = 1.0; // The series starts with 1

    printf("Calculate e^x. Enter x: ");
    scanf("%f", &x);

    // Calculate out to the first 8 iterations of the fractional terms
    for (int i = 1; i <= 8; i++) {
        float numerator = exp_n(x, i);
        float denominator = n_factorial(i);
        e_x += (numerator / denominator);
    }

    printf("e^x = %f\n", e_x);

    return 0;
}