// Compile with Makefile: run `make` (and `make clean` to remove the binary)
// Compile manually without Makefile: gcc -Wall -Wextra -pthread -o A3 A3.c
// Run: ./A3 [num_students] (e.g., ./A3 5, or ./A3 to default to 5 students)

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#define NUM_CHAIRS 3

int students_waiting = 0;
int ta_sleeping = 1;
int chair_queue[NUM_CHAIRS];
int next_seat = 0;
int next_teach = 0;

// mutex for making any changes to available chairs/ta students, used by both students and TA
pthread_mutex_t update_status;

// semaphores: 
// help_done: array of semaphores (one per student) so the TA signals completion to the exact student helped
// student_waiting: counts waiting students and alerts/wakes the TA
sem_t *help_done;
sem_t student_waiting;

// we define two void* funcs to define the ta_behaviour and student_behaviour as the two types of existent threads
// these funcs will run on an infinite while true loop
void* ta_behaviour(void* ta_id) {
    (void)ta_id;
    while(1) {
        // If there are no remaining students waiting for now, we say we are sleeping
        pthread_mutex_lock(&update_status);
        if (students_waiting == 0) {
            ta_sleeping = 1;
            printf("[TA] sees no students waiting in the hallway. (Sleeping now)\n");
        }
        pthread_mutex_unlock(&update_status);

        // wait for the student waiting alert, and become busy
        sem_wait(&student_waiting);

        // call student into office using mutex to free a waiting seat
        pthread_mutex_lock(&update_status);
        int student_id = chair_queue[next_teach];
        next_teach = (next_teach + 1) % NUM_CHAIRS;
        students_waiting--;
        printf("[TA] is helping Student %d now. (Hallway chairs still occupied: %d/%d)\n", student_id, students_waiting, NUM_CHAIRS);
        pthread_mutex_unlock(&update_status);

        // 'simulate' helping, i.e., sleep random num of seconds from 1 - 3
        sleep((rand() % 3) + 1);

        // we are done helping this specific student, signal their individual completion semaphore
        printf("[TA] Finished helping Student %d.\n", student_id);
        sem_post(&help_done[student_id - 1]);
    }
    return NULL;
}

void* student_behaviour(void* student_id) {
    int id = (int)(intptr_t)student_id;

    while(1) {
        // per assignment, students alternate programming and seeking help
        // we always start by sleeping, and when we cant acquire help
        // sleep on the next loop for 1 - 5 seconds, before looking for help again
        printf("[Student %d] is programming.\n", id);
        sleep((rand() % 5) + 1);

        // student shows up, check seat status w mutex lock
        pthread_mutex_lock(&update_status);

        // we have an empty seat, grab a seat and release the mutex
        if (students_waiting < NUM_CHAIRS) {
            chair_queue[next_seat] = id;
            next_seat = (next_seat + 1) % NUM_CHAIRS;
            students_waiting++;

            // if ta is sleeping we wake them up, otherwise we sit in the hallway chair
            if (ta_sleeping) {
                ta_sleeping = 0;
                printf("[Student %d] sees the TA sleeping and wakes them up for help.\n", id);
            } else {
                printf("[Student %d] takes a seat in the hallway. (Waiting students: %d/%d)\n", id, students_waiting, NUM_CHAIRS);
            }
            pthread_mutex_unlock(&update_status);

            // when possible, will alert as next student in line
            sem_post(&student_waiting);

            // wait on this student's own semaphore until the TA finishes their session
            sem_wait(&help_done[id - 1]);
            printf("[Student %d] received help from the TA and returns to programming.\n", id);
        } else {
            // make sure to unlock if we continued, and notify we tried and failed to sit
            printf("[Student %d] found no available chairs (%d/%d full) and will come back later.\n", id, NUM_CHAIRS, NUM_CHAIRS);
            pthread_mutex_unlock(&update_status);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // seed time for random time programming and helping durations
    srand(time(NULL));

    // if we get a proper number, we use that, otherwise we fall back to default 5 students
    int num_students = (argc > 1) ? atoi(argv[1]) : 5;
    if (num_students <= 0) {
        num_students = 5;
    }

    // create ta thread, and n student threads
    pthread_t ta_thread;
    pthread_t* student_threads = malloc(sizeof(pthread_t) * num_students);
    if (student_threads == NULL) {
        perror("malloc");
        return 1;
    }

    // allocate array of completion semaphores (one per student)
    help_done = malloc(sizeof(*help_done) * num_students);
    if (help_done == NULL) {
        perror("malloc");
        free(student_threads);
        return 1;
    }

    // initialize mutex and semaphores, throw error if there was a problem
    if (pthread_mutex_init(&update_status, NULL)) {
        perror("Mutex update_status init failed");
        return 1;
    }
    if (sem_init(&student_waiting, 0, 0) != 0) {
        perror("Semaphore student_waiting init failed");
        return 1;
    }
    for (int i = 0; i < num_students; i++) {
        if (sem_init(&help_done[i], 0, 0) != 0) {
            perror("Semaphore initialization failed");
            return 1;
        }
    }

    // create single ta thread, and n student threads
    pthread_create(&ta_thread, NULL, ta_behaviour, NULL);
    for (int i = 0; i < num_students; i++) {
        pthread_create(&student_threads[i], NULL, student_behaviour, (void *)(intptr_t)(i + 1));
    }

    // wait for ta thread, as well as all n student threads
    pthread_join(ta_thread, NULL);
    for (int i = 0; i < num_students; i++) {
        pthread_join(student_threads[i], NULL);
    }

    // destroy mutex and semaphores when we are done
    pthread_mutex_destroy(&update_status);
    sem_destroy(&student_waiting);
    for (int i = 0; i < num_students; i++) {
        sem_destroy(&help_done[i]);
    }
    free(help_done);
    free(student_threads);

    return 0;
}
