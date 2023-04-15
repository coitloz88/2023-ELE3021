#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	int ret_val;
	ret_val = myfunction(argv[1]);
	printf(1, "Return value: 0x%x\n", ret_val);
	exit();
}
