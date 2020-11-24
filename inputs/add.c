#include<stdlib.h>
#include<stdio.h>
int foo(){
    int k = 100;
    int array[10] = {0,};
    array[5] = k;
    return 0;
}

void bar(){
    char array[10];
    for(int i=0;i<10;i++){
        array[i] = i;
    }
    return;
}

void test_malloc(){
    int* p = (int*)malloc(sizeof(int)*100);
    for(int i=0;i<100;i++){
        p[i] = i;
    }
    free(p);
    return;
}

int main(int argc, char** argv){
    bar();
    return 0;
}
