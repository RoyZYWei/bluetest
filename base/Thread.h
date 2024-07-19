#pragma once
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <functional>
#include <iostream>

class Thread{
public:
    typedef std::function<void()> ThreadFunc;
    explicit Thread(const ThreadFunc& func) : func_(func){
        
    }

    ~Thread(){
        pthread_detach(thread_);
    }

    void start(){
        pthread_create(&thread_, NULL, startThread, this);
    }

    static void* startThread(void* arg){
        std::cout << "begin thread" << std::endl;
        Thread* thread = static_cast<Thread*>(arg);
        thread->func_();
        return NULL;
    }

private:
    pthread_t thread_;
    ThreadFunc func_;

};