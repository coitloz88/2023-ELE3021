#include "types.h"
#include "stat.h"
#include "user.h"

// for int 129, 130 instruction

int main(int argc, char *argv[]){
    __asm__("int $129");
	printf(1, "locked in userprogram by using trap");
    __asm__("int $130");
    sleep(300);
    exit();
}
