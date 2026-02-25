#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void my_handler(int sig) {
    printf("Получен сигнал %d\n", sig);
}

int main() {

    pid_t pid = getpid();
    fprintf(stdout, "PID: %d. Ожидаю сигналы...\n", pid);

    for(int i = 1; i < 65; i++){
    	if((i != 17) & (i != 9) & (i != 19) & (i != 32) & (i != 33))
	    if(signal(i, my_handler) == SIG_ERR) fprintf(stderr, "Нельзя отслеживать %d", i);
    }

    signal(17, my_handler);
        
    
    while(1) {
        sleep(1);
    }
    
    return 0;
}
