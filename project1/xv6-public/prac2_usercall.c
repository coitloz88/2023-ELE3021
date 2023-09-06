#include "types.h"
#include "stat.h"
#include "user.h"

// for int 128 instruction

int main(int argc, char *argv[]){
    __asm__("int $128");
}
