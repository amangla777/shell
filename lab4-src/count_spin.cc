#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

volatile unsigned long lock = 0;

// atomic test‑and‑set: sets *lock=1, returns previous value
unsigned long 
test_and_set(volatile unsigned long * lockptr)
{
    unsigned long oldval = 1;
    asm volatile("xchgq %1,%0"
                 : "=r"(oldval)
                 : "m"(*lockptr), "0"(oldval)
                 : "memory");
    return oldval;
}

// spin‑lock with yield
void
my_spin_lock( volatile unsigned long * lockptr )
{
    // spin until we successfully flip lock from 0→1
    while ( test_and_set(lockptr) == 1 ) {
        // give other threads a chance
        pthread_yield();
    }
}

// release the lock
void
my_spin_unlock( volatile unsigned long * lockptr )
{
    *lockptr = 0;
}

long count = 0;

// worker increments under our spin‑lock
void*
increment( void * arg )
{
    long ntimes = (long)arg;
    for ( long i = 0; i < ntimes; i++ ) {

        my_spin_lock(&lock);
        count = count + 1;
        my_spin_unlock(&lock);

    }
    return NULL;
}

int main( int argc, char ** argv )
{
    long n = 10000000;
    pthread_t t1, t2;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    printf("Start Test. Final count should be %ld\n", 2 * n );

    // spawn two threads
    pthread_create(&t1, &attr, increment, (void*)n);
    pthread_create(&t2, &attr, increment, (void*)n);

    // wait for them
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if ( count != 2 * n ) {
        printf("\n****** Error. Final count is %ld\n", count );
        printf("****** It should be %ld\n", 2 * n );
    }
    else {
        printf("\n>>>>>> O.K. Final count is %ld\n", count );
    }

    return 0;
}
