#include <pthread.h>
#include <stdio.h>

void* saudar(void* arg) {
    printf("Olá de dentro da Thread!\n");
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, saudar, NULL);
    pthread_join(thread, NULL);
    printf("Thread principal finalizada.\n");
    return 0;
}
