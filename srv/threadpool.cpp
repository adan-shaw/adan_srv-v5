#include "threadpool.h"
#include "inet_srv.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>


//线程函数(主要就是询问链队列有没有任务, 没有就休眠)
void *thread_routine(void *arg){
  _printf("thread %d is starting\n", pthread_self());
  threadpool_t *pool = (threadpool_t *)arg;
  
  while(1){
    bool timeout = false;
    assert(pthread_mutex_lock(&pool->ready.pmutex) == 0);//访问线程池之前需要加锁
    pool->idle++;//空闲线程数量+1
    
    //等待队列有任务到来 or 等待线程池销毁通知
    struct timespec abstime;
    while(pool->first == NULL && !pool->quit){
      //否则线程阻塞等待
      clock_gettime(CLOCK_REALTIME, &abstime);//获取从当前时间
      abstime.tv_sec += wait_mission_timeout;//加上等待任务的时间
      
      //该函数会自动解锁, 允许其他线程访问, 当被唤醒时, 又会自动加锁
      int status = pthread_cond_timedwait(&pool->ready.pcond, &pool->ready.pmutex, &abstime); 
      if(status == ETIMEDOUT){
        timeout = true;
        break;
      }
    }
    
    pool->idle--;//空闲线程数量-1
    
    //检查任务链队列
    if(pool->first != NULL){
      task_t *t = pool->first;//取出等待队列最前的任务-->[移除]并[执行]任务
      pool->first = t->next;
      

      assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);

      void* ret_data = t->run(t->arg);//执行任务函数
      io_info_t* pio_info = (io_info_t*)ret_data;
      if(pio_info == NULL){//socket io 错误, 返回值已经被释放
        free(t);
        continue;
      }
      //socket io 正确
      if(pio_info->is_rw == _is_read){//读(释放返回结果)
        _printf("%s\n", pio_info->buf);//直接的简单打印
        int sfd_acc = pio_info->sfd;//先保存对方的sfd_acc 值
        memset(pio_info, '\0', sizeof(io_info_t));
        pio_info->sfd = sfd_acc;
        pio_info->is_rw = _is_write;
        strncpy(pio_info->buf, "got you baby", io_data_buf_max);
        pio_info->data_len = strlen(pio_info->buf) + 1;
        if(_send(pio_info) != NULL)//不应该直接操作_send, 还是需要扔进任务队列里面
        	free(ret_data);//读-->释放任务函数返回值
      }
      else{//写
      	free(t->arg);//写-->释放任务函数传入参数
      }
      free(t);//执行完任务释放任务载体(任务载体, 任务函数参数, 任务函数返回值, 三个释放)
      //******************************************************
      continue;//这句重新加锁多余的...直接下一次任务更快
    }
    
    //退出线程池 && 任务队列已经为空
    if(pool->quit && pool->first == NULL){
      pool->counter--;//当前工作的线程数-1(ps: 空闲线程数上面已经减少了, 这里不需要重复-1)
      
      //若线程池中没有线程, 通知等待线程[主线程]全部任务已经完成(主线程中有一个等待锁住了, 和这里是互嵌)
      if(pool->counter == 0){
        assert(pthread_cond_signal(&pool->ready.pcond) == 0);
      }
      
      assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);
      break;
    }
    
    if(timeout == true){
      pool->counter--;//当前工作的线程数-1
      assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);
      break;
    }
    
    //assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);
  }
  
  _printf("thread %d already quit\n", pthread_self());
  return NULL;
}


//线程池初始化
bool threadpool_init(threadpool_t *pool, int threads){
  int status = pthread_mutex_init(&pool->ready.pmutex, NULL);
  if(status != 0){
    _printf("pthread_mutex_init() fail, errno: %d\n",errno);
    return false;//初始化互斥量失败, 返回失败原因
  }
  status = pthread_cond_init(&pool->ready.pcond, NULL);
  if(status != 0){
    _printf("pthread_cond_init() fail, errno: %d\n",errno);
    assert(pthread_mutex_destroy(&pool->ready.pmutex) == 0);
    return false;
  }

  pool->first = NULL;
  pool->last =NULL;
  pool->counter = 0;
  pool->idle = 0;
  pool->max_threads = threads;
  pool->quit = false;
  return true;
}


//增加一个任务到线程池
void threadpool_add_task(threadpool_t *pool, void *(*run)(void *arg), void *arg){
  //产生一个新的任务
  task_t *newtask = (task_t *)malloc(sizeof(task_t));
  newtask->run = run;
  newtask->arg = arg;
  newtask->next = NULL;//最后一个任务的next 总是NULL
  
  assert(pthread_mutex_lock(&pool->ready.pmutex) == 0);//线程池的状态被多个线程共享, 操作前需要加锁
  
  if(pool->first == NULL)
    pool->first = newtask;//如果是空队列, first mission = this
  else
    pool->last->next = newtask;//如果不是空队列, 替换掉上一个last mission
    
  pool->last = newtask;//更新自己为最后一个任务
  
  //线程池中有线程空闲, 唤醒
  if(pool->idle > 0){
    assert(pthread_cond_signal(&pool->ready.pcond) == 0);
  }
  //当前线程池中线程个数没有达到设定的最大值, 创建一个新的线程
  else if(pool->counter < pool->max_threads){
    pthread_t tid;
    if(pthread_create(&tid, NULL, thread_routine, pool) == 0){
		  if(pthread_detach(tid) != 0)
			  _printf("pthread_detach() fail, errno: %d\n",errno);
		  
      pool->counter++;
    }
  }
  //结束，访问
  assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);
}

//线程池销毁
bool threadpool_destroy(threadpool_t *pool){
  if(pool->quit){
    _printf("threadpool_destroy() already quit\n");
    return false;
  }
  
  assert(pthread_mutex_lock(&pool->ready.pmutex) == 0);//加锁
  pool->quit = true;//拉高销毁标记
  
  //线程池中线程个数大于0
  if(pool->counter > 0){
    if(pool->idle > 0)
      assert(pthread_cond_broadcast(&pool->ready.pcond) == 0);
    
    //正在执行任务的线程, 等待他们结束任务
    while(pool->counter){
      assert(pthread_cond_wait(&pool->ready.pcond, &pool->ready.pmutex) == 0);
    }
  }
  assert(pthread_mutex_unlock(&pool->ready.pmutex) == 0);
  
  //既然所有线程都结束了, 这里开始清除锁资源
  int status = pthread_mutex_destroy(&pool->ready.pmutex);
  if(status != 0){
    _printf("pthread_mutex_destroy() fail, errno: %d\n",errno);
    return false;
  }
  
  status = pthread_cond_destroy(&pool->ready.pcond);
  if(status != 0){
    _printf("pthread_cond_destroy() fail, errno: %d\n",errno);
    return false;
  }
  return true;
}
