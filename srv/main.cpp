#include "threadpool.h"
#include "inet_srv.h"

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define check_epoll_fail_out 32
#define check_epoll_slot_time 5000 //没有私有栈可用时, 等待5ms
#define pth_pool_count 8
#define test_count 10000

//全局变量
threadpool_t pool;
inet_srv_t _inet_srv;

//统计信息:
pthread_mutex_t history_mutex;//统计信息锁
unsigned int history_err_count = 0; //过去的错误
unsigned int history_accept = 0; //过去接受过的人数
unsigned int history_send = 0; //过去发送出去的数据总量
unsigned int history_recv = 0; //过去接收到的数据总量
unsigned int history_killed = 0; //过去关闭过的客户端

//统计信息操作函数
void add_err_count(void);
void add_accept_count(void);
void add_send_count(int _size);
void add_recv_count(int _size);
void add_killed_count(void);



//任务函数前置声明
void* mytask(void *arg);//线程池测试任务函数



int main(void){
  //初始化统计数据锁
  int tmp = pthread_mutex_init(&history_mutex, NULL);
  if(tmp != 0){
    _printf("pthread_mutex_init() fail, errno: %d\n",errno);
    return -1;
  }
  //初始化socket 服务
  tmp = init_inet_srv_t(&_inet_srv);
  if(!tmp){
    _printf("init_inet_srv_t() fail\n");
    return -1;
  }
  //初始化线程池
  tmp = threadpool_init(&pool, pth_pool_count);
  if(!tmp){
    _printf("threadpool_init() fail\n");
    return -1;
  }


  //*********************************************************************
  //线程池测试部分
  //*********************************************************************
  //利用ptmalloc 的内存重用效率, 来减少线程的重复创建...
  //内存分配可以靠ptmalloc 重用, 但是线程创建, 每次都需要内核操作, 登记...
  //两害取其轻
  int i = 0;
  for(; i < test_count; i++){
    int *arg = (int*)malloc(sizeof(int));//任务函数的<参数内存分配>
    *arg = i;
    threadpool_add_task(&pool, mytask, arg);//将<任务函数指针>,<任务函数参数指针> 推入线程池
  }
  //*********************************************************************
  
  
  //*********************************************************************
  //网络框架运行部分(阻塞, 建议你弄个子线程来跑这玩儿...后期再说)
  //*********************************************************************
  int check_epoll_fail_count = 0;
  while(_inet_srv.is_working){
    if(check_epoll()){
      ;//暂时不知道要做什么
    }
    else{//check_epoll() 失败, 检查epoll 失败
      add_err_count();
      check_epoll_fail_count++;
      if(check_epoll_fail_count >= check_epoll_fail_out){
        assert(pthread_mutex_lock(&_inet_srv._mutex) == 0);//加锁
        _inet_srv.is_working = false;//check epoll 错误到达了极限
        assert(pthread_mutex_unlock(&_inet_srv._mutex) == 0);
      }
    }
  
    if(pool.quit){
      assert(pthread_mutex_lock(&_inet_srv._mutex) == 0);//加锁
      _inet_srv.is_working = false;//线程池意外终止了, socket 服务也跟着终止
      assert(pthread_mutex_unlock(&_inet_srv._mutex) == 0);
    }

    //每5 ms 检索一次
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = check_epoll_slot_time;
    assert(nanosleep(&req, NULL) == 0);
  }
  //*********************************************************************
  

  
  tmp = threadpool_destroy(&pool);//销毁线程池
  if(!tmp){
    _printf("threadpool_destroy() fail\n");
    return -1;
  }
  tmp = kill_inet_srv_t(&_inet_srv);//销毁socket 服务
  if(!tmp){
    _printf("kill_inet_srv_t() fail\n");
    return -1;
  }
  tmp = pthread_mutex_destroy(&history_mutex);//销毁统计数据锁
  if(tmp != 0){
    _printf("pthread_mutex_destroy() fail, errno: %d\n",errno);
    return -1;
  }
  return 0;
}




//任务函数(这个模型的缺点: 如果任务函数陷入阻塞, 这个线程就一直占用着线程位置, 但是却不能再服务器了)
//        做个安全检索, 阻塞超过8 秒, 强制清楚一个阻塞线程???
void* mytask(void *arg){
  printf("thread %d is working on task %d\n", pthread_self(), *(int*)arg);
  //sleep(10);//demo: 如果陷入了阻塞, 这个线程就不能用了.
              //这时只有用pthread_cancel() 函数强制退出阻塞线程了...
  //sleep(1);
  free(arg);
  return NULL;
}


//统计信息操作函数
void add_err_count(void){
  assert(pthread_mutex_lock(&history_mutex) == 0);
  history_err_count++;
  assert(pthread_mutex_unlock(&history_mutex) == 0);
}
void add_accept_count(void){
  assert(pthread_mutex_lock(&history_mutex) == 0);
  history_accept++;
  assert(pthread_mutex_unlock(&history_mutex) == 0);
}
void add_send_count(int _size){
  assert(pthread_mutex_lock(&history_mutex) == 0);
  history_send++;
  assert(pthread_mutex_unlock(&history_mutex) == 0);
}
void add_recv_count(int _size){
  assert(pthread_mutex_lock(&history_mutex) == 0);
  history_recv++;
  assert(pthread_mutex_unlock(&history_mutex) == 0);
}
void add_killed_count(void){
  assert(pthread_mutex_lock(&history_mutex) == 0);
  history_killed++;
  assert(pthread_mutex_unlock(&history_mutex) == 0);
}
