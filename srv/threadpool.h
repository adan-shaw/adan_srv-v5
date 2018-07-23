/*
鸣谢: https://www.cnblogs.com/yangang92/p/5485868.html
作者: 渣码农
声明: 本版本为改进版...
 */

#include <stdbool.h>
#include <pthread.h>


//弱点1: 任务函数不能阻塞(这个可以修复)
//弱点2: 执行任务是无序执行的, 顺序不可控(这个难以修复)
#ifdef _printf
#else
#define _printf(args...) printf(args)
#endif


//没有任务的时候, 任务线程阻塞等待时间
#define wait_mission_timeout 2


//<线程池的访问状态控制>
typedef struct condition{
  pthread_mutex_t pmutex;//互斥量
  pthread_cond_t pcond;//条件变量
}condition_t;


//封装线程池中, <单个任务>的信息载体
typedef struct task{
  void *(*run)(void *args); //函数指针, 需要执行的任务
  void *arg;                //函数参数指针
  struct task *next;        //下一个任务信息载体
}task_t;


//<线程池>信息载体
typedef struct threadpool{
  condition_t ready;   //线程池的访问状态控制
  task_t *first;       //任务队列中第一个任务(链型队列)
  task_t *last;        //任务队列中最后一个任务
  int counter;         //线程池中已有线程数
  int idle;            //线程池中空闲线程数
  int max_threads;     //线程池最大线程数
  bool quit;           //是否退出标志
}threadpool_t;


//线程池初始化
bool threadpool_init(threadpool_t *pool, int threads);

//往线程池中加入任务
void threadpool_add_task(threadpool_t *pool, void *(*run)(void *arg), void *arg);

//摧毁线程池
bool threadpool_destroy(threadpool_t *pool);

