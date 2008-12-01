/* 
 * Written by Josh Dybnis and released to the public domain, as explained at
 * http://creativecommons.org/licenses/publicdomain
 */
#include <stdio.h>
#include <pthread.h>

#include "CuTest.h"

#include "common.h"
#include "runtime.h"
#include "map.h"
#include "lwt.h"

#define ASSERT_EQUAL(x, y) CuAssertIntEquals(tc, x, y)

typedef struct worker_data {
    int id;
    CuTest *tc;
    map_t *map;
    int *wait;
} worker_data_t;

static map_type_t map_type_;

// Test some basic stuff; add a few keys, remove a few keys
void basic_test (CuTest* tc) {

    map_t *map = map_alloc(map_type_);

    ASSERT_EQUAL( 0,              map_count(map)            );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_add(map,"a",2,10)     );
    ASSERT_EQUAL( 1,              map_count(map)            );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_add(map,"b",2,20)     );
    ASSERT_EQUAL( 2,              map_count(map)            );
    ASSERT_EQUAL( 20,             map_get(map,"b",2)        );
    ASSERT_EQUAL( 10,             map_set(map,"a",2,11)     );
    ASSERT_EQUAL( 20,             map_set(map,"b",2,21)     );
    ASSERT_EQUAL( 2,              map_count(map)            );
    ASSERT_EQUAL( 21,             map_add(map,"b",2,22)     );
    ASSERT_EQUAL( 11,             map_remove(map,"a",2)     );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"a",2)        );
    ASSERT_EQUAL( 1,              map_count(map)            );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_remove(map,"a",2)     );
    ASSERT_EQUAL( 21,             map_remove(map,"b",2)     );
    ASSERT_EQUAL( 0,              map_count(map)            );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_remove(map,"b",2)     );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_remove(map,"c",2)     );
    ASSERT_EQUAL( 0,              map_count(map)            );
    
    ASSERT_EQUAL( DOES_NOT_EXIST, map_add(map,"d",2,40)     );
    ASSERT_EQUAL( 40,             map_get(map,"d",2)        );
    ASSERT_EQUAL( 1,              map_count(map)            );
    ASSERT_EQUAL( 40,             map_remove(map,"d",2)     );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"d",2)        );
    ASSERT_EQUAL( 0,              map_count(map)            );

    ASSERT_EQUAL( DOES_NOT_EXIST, map_replace(map,"d",2,10) );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"d",2)        );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_set(map,"d",2,40)     );
    ASSERT_EQUAL( 40,             map_replace(map,"d",2,41) );
    ASSERT_EQUAL( 41,             map_get(map,"d",2)        );
    ASSERT_EQUAL( 41,             map_remove(map,"d",2)     );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"d",2)        );
    ASSERT_EQUAL( 0,              map_count(map)            );

    ASSERT_EQUAL( DOES_NOT_EXIST, map_replace(map,"b",2,20) );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"b",2)        );

    // In the end, all entries should be removed
    ASSERT_EQUAL( DOES_NOT_EXIST, map_set(map,"b",2,20)     );
    ASSERT_EQUAL( 20,             map_replace(map,"b",2,21) );
    ASSERT_EQUAL( 21,             map_get(map,"b",2)        );
    ASSERT_EQUAL( 21,             map_remove(map,"b",2)     );
    ASSERT_EQUAL( DOES_NOT_EXIST, map_get(map,"b",2)        );
    ASSERT_EQUAL( 0,              map_count(map)            );

    map_free(map);

    // In a quiecent state; it is safe to free.
    rcu_update();
}

void *simple_worker (void *arg) {
    worker_data_t *wd = (worker_data_t *)arg;
    map_t *map = wd->map;
    CuTest* tc = wd->tc;
    uint64_t d = wd->id;
    int iters = map_type_ == MAP_TYPE_LIST ? 100 : 1000000;

    SYNC_ADD(wd->wait, -1);
    do { } while (*((volatile worker_data_t *)wd)->wait); // wait for all workers to be ready

    for (int j = 0; j < 10; ++j) {
        for (int i = d; i < iters; i+=2) {
            char key[10];
            sprintf(key, "k%u", i); 
            TRACE("t0", "test map_add() iteration (%llu, %llu)", j, i);
            CuAssertIntEquals_Msg(tc, key, DOES_NOT_EXIST, map_add(map, key, strlen(key)+1, d+1) );
            rcu_update();
        }
        for (int i = d; i < iters; i+=2) {
            char key[10];
            sprintf(key, "k%u", i); 
            TRACE("t0", "test map_remove() iteration (%llu, %llu)", j, i);
            CuAssertIntEquals_Msg(tc, key, d+1, map_remove(map, key, strlen(key)+1) );
            rcu_update();
        }
    }
    return NULL;
}

// Do some simple concurrent testing
void simple_add_remove (CuTest* tc) {

    pthread_t thread[2];
    worker_data_t wd[2];
    int wait = 2;
    map_t *map = map_alloc(map_type_);

    // In 2 threads, add & remove even & odd elements concurrently
    int i;
    for (i = 0; i < 2; ++i) {
        wd[i].id = i;
        wd[i].tc = tc;
        wd[i].map = map;
        wd[i].wait = &wait;
        int rc = nbd_thread_create(thread + i, i, simple_worker, wd + i);
        if (rc != 0) { perror("nbd_thread_create"); return; }
    }
    for (i = 0; i < 2; ++i) {
        pthread_join(thread[i], NULL);
    }

    // In the end, all members should be removed
    ASSERT_EQUAL( 0, map_count(map) );

    // In a quiecent state; it is safe to free.
    map_free(map);
}


void *inserter_worker (void *arg) {
    //pthread_t thread[NUM_THREADS];

    //map_t *map = map_alloc(map_type_);
    return NULL;
}

// Concurrent insertion
void concurrent_insert (CuTest* tc) {
}

int main (void) {

    nbd_init();
    //lwt_set_trace_level("h0");

    map_type_t map_types[] = { MAP_TYPE_LIST, MAP_TYPE_SKIPLIST, MAP_TYPE_HASHTABLE };
    for (int i = 0; i < sizeof(map_types)/sizeof(*map_types); ++i) {
        map_type_ = map_types[i];

        // Create and run test suite
        CuString *output = CuStringNew();
        CuSuite* suite = CuSuiteNew();

        SUITE_ADD_TEST(suite, basic_test);
        SUITE_ADD_TEST(suite, simple_add_remove);

        CuSuiteRun(suite);
        CuSuiteDetails(suite, output);
        printf("%s\n", output->buffer);
    }

    return 0;
}
