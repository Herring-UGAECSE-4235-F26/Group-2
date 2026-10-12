#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#define REPEAT 1000000
#define Q32_SCALE 4294967296.0
#define Q16_SCALE 65536.0

extern float sqrtWithC(float input);
extern float sqrtWithASM(float input);
extern uint32_t fixed_point_sqrt(uint64_t input);
extern float quake_sqrt(float input);

volatile double result_sink; // Prevent compiler from removing calculations

//Converting a decimal into Q32.32 fixed point
uint64_t decimal_to_q32(double input) {
    return (uint64_t)(input * Q32_SCALE);
}

//Converting into Q16.16 fixed point into decimal
double q16_to_decimal(uint32_t input) {
    return (double)input / Q16_SCALE;
}

//Testing all square root methods
void test_sqrt(double number) {
    clock_t start, end;
    double result1 = 0;
    double result2 = 0;
    double result3 = 0;
    double result4 = 0;

    double time1, time2, time3, time4;
    uint64_t q32_input = decimal_to_q32(number);
    float quake_input = (float)number;
    uint32_t fixed_result = 0;

    //###############Method 1##################
    start = clock();
    for (int i = 0; i < REPEAT; i++) {
        result1 = sqrtWithC((float)number);
        result_sink = result1;
    }
    end = clock();
    time1 = ((double)(end - start) / CLOCKS_PER_SEC) * 1000000000.0 / REPEAT; // Convert to nanoseconds

    //###############Method 2##################
    start = clock();
    for (int i = 0; i < REPEAT; i++) {
        result2 = sqrtWithASM((float)number);
        result_sink = result2;
    }
    end = clock();
    time2 = ((double)(end - start) / CLOCKS_PER_SEC) * 1000000000.0 / REPEAT; // Convert to nanoseconds
    //Method 3
    start = clock();
    for (int i = 0; i < REPEAT; i++) {
        fixed_result = fixed_point_sqrt(q32_input);
        result_sink = fixed_result;
    }
    end = clock();
    time3 = ((double)(end - start) / CLOCKS_PER_SEC) * 1000000000.0 / REPEAT; // Convert to nanoseconds
    result3 = q16_to_decimal(fixed_result);

    //Method 4
    start = clock();
    for (int i = 0; i < REPEAT; i++) {
        result4 = quake_sqrt(quake_input);
        result_sink = result4;
    }
    end = clock();
    time4 = ((double)(end - start) / CLOCKS_PER_SEC) * 1000000000.0 / REPEAT; // Convert to nanoseconds
    result4 = result4; 

    printf("\nInput: %f\n", number);
    printf("Method 1: sqrt() = %f, Time = %f ns\n", result1, time1);
    printf("Method 2: sqrt() = %f, Time = %f ns\n", result2, time2);
    printf("Method 3: fixed_point_sqrt() = %f, Time = %f ns\n", result3, time3);
    printf("Method 4: quake_sqrt() = %f, Time = %f ns\n", result4, time4);
}


int main(void) {
    double a, b, c;
    char again = 'y';
    while (again == 'y' || again == 'Y') {
        //integer between 500000 and 1000000
        do{
            printf("Enter a number between 500000 and 1000000: ");
            if (scanf("%lf", &a) != 1) {
                return 1; // Exit if input is not a number
            }

        } while (a < 500000 || a > 1000000 || floor(a) != a); // Ensure the number is an integer

        //integer between 0 and 1
        do{
            printf("Enter a number between 0 and 1: ");
            if (scanf("%lf", &b) != 1) {
                return 1; // Exit if input is not a number
            }

        } while (b < 0 || b > 1); // Ensure the number is in the range [0, 1]
    
        //large number with decimal between 500000 and 1000000
        do{
            printf("Enter a large number with decimal between 500000 and 1000000: ");
            if (scanf("%lf", &c) != 1) {
                return 1; // Exit if input is not a number
            }

        } while (c < 500000 || c > 1000000 || fabs(c - floor(c)) < 0.001); // Ensure the number is in the range [500000, 1000000]
        
        printf("between 500000 and 1000000");
        test_sqrt(a);
        printf("between 0 and 1");
        test_sqrt(b);
        printf("between 500000 and 1000000 with decimal");
        test_sqrt(c);

        printf("Do you want to test other numbers? (y/n): ");
        if (scanf(" %c", &again) != 1) {
            return 1; // Exit if input is not a valid character
        }
    }

    return 0;


}