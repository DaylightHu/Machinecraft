#include <stdio.h>
#include <stdbool.h>

// 0air 1iron 2dust 3torch 4source
struct Block{
    int x, y, z;
    int id;
    int power;
};

int calculate_power(struct Block neighbor){
    if (neighbor.id == 4){
        return 15;
    }
    else if(neighbor.id == 2){
        if (neighbor.power > 0){
            return neighbor.power - 1;
        } else{
            return 0;
        }
    }
    else {
        return 0;
    }
}
        
int main(void){
    struct Block source = {0};
    source.id = 4;
    source.x = 0;

    struct Block dust0 = {0};
    dust0.id = 2;
    dust0.x = 1;

    struct Block dust1 = {0};
    dust1.id = 2;
    dust1.x = 2;

    printf("开始时d0: %d\n", dust0.power);
    printf("开始时d1: %d\n", dust1.power);

    dust0.power = calculate_power(source);
    dust1.power = calculate_power(dust0);
    printf("通电时d0: %d\n", dust0.power);
    printf("通电时d1: %d\n", dust1.power);

    source.id = 0;
    
    dust0.power = calculate_power(source);
    dust1.power = calculate_power(dust0);
    printf("电源拆掉时d0: %d\n", dust0.power);
    printf("电源拆掉时d1: %d\n", dust1.power);
    return 0;
}
