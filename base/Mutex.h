#pragma once
#include <pthread.h>
#include <unistd.h>

class Mutex{
public:
    explicit Mutex(){
        pthread_mutex_init(&mutex_, NULL);
    }

    void lock(){
        pthread_mutex_lock(&mutex_);
    }

    void unlock(){
        pthread_mutex_unlock(&mutex_);
    }

    ~Mutex(){
        lock();
        pthread_mutex_destroy(&mutex_);
    }

    pthread_mutex_t* get(){
        return &mutex_;
    }

private:
    pthread_mutex_t mutex_;
};


class MutexLockGuard{
public:
    explicit MutexLockGuard(Mutex& mutex): mutex_(mutex){
        mutex_.lock();
    }

    ~MutexLockGuard(){
        mutex_.unlock();
    }
    
private:
    Mutex& mutex_;

};