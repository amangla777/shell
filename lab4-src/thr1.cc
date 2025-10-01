#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static const int ITERS = 1000;

void* printA(void*) { for(int i=0;i<ITERS;i++){ printf("A"); usleep(1000); } return nullptr; }
void* printB(void*) { for(int i=0;i<ITERS;i++){ printf("B"); usleep(1000); } return nullptr; }
void* printC(void*) { for(int i=0;i<ITERS;i++){ printf("C"); usleep(1000); } return nullptr; }
void* printD(void*) { for(int i=0;i<ITERS;i++){ printf("D"); usleep(1000); } return nullptr; }
void* printE(void*) { for(int i=0;i<ITERS;i++){ printf("E"); usleep(1000); } return nullptr; }

int main() {
  pthread_t t1,t2,t3,t4,t5;
  pthread_create(&t1, nullptr, printA, nullptr);
  pthread_create(&t2, nullptr, printB, nullptr);
  pthread_create(&t3, nullptr, printC, nullptr);
  pthread_create(&t4, nullptr, printD, nullptr);   // new!
  pthread_create(&t5, nullptr, printE, nullptr);   // new!

  pthread_join(t1, nullptr);
  pthread_join(t2, nullptr);
  pthread_join(t3, nullptr);
  pthread_join(t4, nullptr);
  pthread_join(t5, nullptr);

  printf("\n");  // end with a newline to flush the buffer
  return 0;
}