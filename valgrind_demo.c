#include <stdlib.h>
int main(void){
    char *p=malloc(10);
    p[0]='A';free(p);
    p[0]='B'; /* deliberate use-after-free for Valgrind */
    char *q=malloc(100);q[0]='X';(void)q; /* deliberate leak */
    return 0;
}
