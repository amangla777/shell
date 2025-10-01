#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

// ---- 1) Declare & initialize the mutex ----
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;

long count = 0;

void increment(long ntimes)
{
    for (int i = 0; i < ntimes; i++) {
        long c;

        // ---- 2) Lock before reading/writing `count` ----
        pthread_mutex_lock(&count_lock);

        c = count;
        c = c + 1;
        count = c;

        pthread_mutex_unlock(&count_lock);
        // -------------------------------------------
    }
}

int main(int argc, char** argv)
{
    long n = 10000000;
    pthread_t t1, t2;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    printf("Start Test.  Final count should be %ld\n", 2 * n);

    // Create threads
    pthread_create(&t1, &attr,
                   (void* (*)(void*))increment,
                   (void*)n);

    pthread_create(&t2, &attr,
                   (void* (*)(void*))increment,
                   (void*)n);

    // Wait until threads finish
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // ---- 3) Destroy the mutex when done (optional) ----
    pthread_mutex_destroy(&count_lock);

    // Check result
    if (count != 2 * n) {
        printf("\n****** Error. Final count is %ld\n", count);
        printf("****** It should be %ld\n", 2 * n);
    }
    else {
        printf("\n>>>>>> O.K.  Final count is %ld\n", count);
    }

    return 0;
}
