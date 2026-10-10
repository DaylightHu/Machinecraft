#include <stdio.h>
#include <stdbool.h>
// 0air 1iron 2dust 3torch 4source
struct Block{
    int x, y, z;
    int id;
    int power;
    bool is_lit;
    int torch_dir;
    int wire_connections;
};

int main(void){
    struct Block source = {0};
    source.id = 4;
    source.x = 0;

    struct Block dust = {0};
    dust.id = 2;
    dust.x = 1;

    printf("开始时: %d\n", dust.power);
    
    if (source.id == 4){
         dust.power = 15;
    } else{
         dust.power = 0;
    }
    printf("通电时: %d\n", dust.power);

    source.id = 0;
    if (source.id == 4){
        dust.power = 15;
    } else{
        dust.power = 0;
    }
    printf("电源拆掉时: %d\n", dust.power);
    return 0;
}
