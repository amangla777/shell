// bounded-buffer.cc
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { MaxSize = 10 };

class BoundedBuffer {
  int _queue[MaxSize];
  int _head;
  int _tail;
  pthread_mutex_t _mutex;
  sem_t _emptySem;
  sem_t _fullSem;

public:
  BoundedBuffer();
  ~BoundedBuffer();
  void enqueue(int val);
  int dequeue();
};

BoundedBuffer::BoundedBuffer() {
  _head = 0;
  _tail = 0;
  // initialize mutex
  pthread_mutex_init(&_mutex, nullptr);
  // empty slots = MaxSize, full slots = 0
  sem_init(&_emptySem, 0, MaxSize);
  sem_init(&_fullSem,  0, 0);
}

BoundedBuffer::~BoundedBuffer() {
  pthread_mutex_destroy(&_mutex);
  sem_destroy(&_emptySem);
  sem_destroy(&_fullSem);
}

void BoundedBuffer::enqueue(int val) {
  // wait for an empty slot
  sem_wait(&_emptySem);

  // lock the buffer for exclusive access
  pthread_mutex_lock(&_mutex);
  _queue[_tail] = val;
  _tail = (_tail + 1) % MaxSize;
  pthread_mutex_unlock(&_mutex);

  // signal that there is one more full slot
  sem_post(&_fullSem);
}

int BoundedBuffer::dequeue() {
  // wait for a full slot
  sem_wait(&_fullSem);

  // lock the buffer to remove an item
  pthread_mutex_lock(&_mutex);
  int val = _queue[_head];
  _head = (_head + 1) % MaxSize;
  pthread_mutex_unlock(&_mutex);

  // signal that there is one more empty slot
  sem_post(&_emptySem);

  return val;
}

struct ThreadArgs {
  BoundedBuffer* queue;
  int n;
};

void* producerThread(void* a) {
  auto* args = static_cast<ThreadArgs*>(a);
  printf("Running producer thread %d times\n", args->n);
  for (int i = 0; i < args->n; i++) {
    args->queue->enqueue(i);
  }
  return nullptr;
}

void* consumerThread(void* a) {
  auto* args = static_cast<ThreadArgs*>(a);
  int lastVal = 0;
  printf("Running consumer thread %d times\n", args->n);
  for (int i = 0; i < args->n; i++) {
    int val = args->queue->dequeue();
    assert(val == lastVal);
    lastVal++;
  }
  return nullptr;
}

int main(int argc, char** argv) {
  pthread_t t1, t2;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  ThreadArgs threadArgs;
  threadArgs.queue = new BoundedBuffer();
  threadArgs.n = 1000000;

  pthread_create(&t1, &attr, producerThread, &threadArgs);
  pthread_create(&t2, &attr, consumerThread, &threadArgs);

  pthread_join(t1, nullptr);
  pthread_join(t2, nullptr);

  printf("Semaphores passed\n");

  delete threadArgs.queue;
  return 0;
}
