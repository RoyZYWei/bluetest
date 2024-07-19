#pragma once
#include <pthread.h>
#include <unistd.h>
#include "Mutex.h"
class Condition{
public:
    Condition(Mutex &mutex): mutex_(mutex){
        pthread_cond_init(&cond_, NULL);
    }

    ~Condition(){
        pthread_cond_destroy(&cond_);
    }

    void wait(){
        pthread_cond_wait(&cond_, mutex_.get());
    }

    void signal(){
        pthread_cond_signal(&cond_);
    }

    void broadcast(){
        pthread_cond_broadcast(&cond_);
    }


private:
    pthread_cond_t cond_;
    Mutex& mutex_;
};