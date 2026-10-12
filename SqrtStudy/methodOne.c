#include <stdio.h>
#include <stdbool.h>
#include <math.h>

void getInput(double *x) {

    printf("Enter a number for which to calculate the square root: ");
    scanf("%lf", x);
}

int main(){

    while(true){
        double inputDouble=0.0;

        getInput(&inputDouble);

        printf("Square root of %f is %f\n", inputDouble, (double)sqrt(inputDouble));

        printf("Calculate another? (1 for yes, 0 for no): ");
        int another;
        scanf("%d", &another);
        if (!another) break;
    }

}
