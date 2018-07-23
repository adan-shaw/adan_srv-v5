#include "inet_srv.h"
#include "threadpool.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/epoll.h>

#include <unistd.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>

#ifdef _printf
#else
//可变参数宏定义(支持格式化输入参数)
#define _printf(args...) printf(args)
#endif

//全局变量声明
extern threadpool_t pool;
extern inet_srv_t _inet_srv;

//统计信息操作函数
extern void add_err_count(void);
extern void add_accept_count(void);
extern void add_send_count(int _size);
extern void add_recv_count(int _size);
extern void add_killed_count(void);


//启动epoll 服务
//无锁, 只启动一次
bool start_inet_srv(inet_srv_t* pinet_srv){
  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
  //创建监听socket 部分
  int sfd = socket(sock_proto, sock_type, 0);
	if(sfd == -1){
		_printf("socket() fail, errno = %d\n", errno);
		return false;
	}

	//设置接收缓冲区
	int opt_val = sock_buf_recv;
	if(setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_revbuf() fail, errno = %d\n", errno);
		close(sfd);
		return false;
	}
	//设置发送缓冲区
	opt_val = sock_buf_send;
	if(setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_sndbuf() fail, errno = %d\n", errno);
		close(sfd);
		return false;
	}

	//设置关闭时如果有数据未发送完, 等待n 秒再关闭socket
	struct linger plinger;
	plinger.l_linger = sock_close_timeout;
	plinger.l_onoff = sock_close_timeout_onoff;
	if(setsockopt(sfd, SOL_SOCKET, SO_LINGER, &plinger, sizeof(struct linger)) == -1){
		_printf("set_sockopt_linger() fail, errno = %d\n", errno);
		close(sfd);
		return false;
	}

	//设置地址重用
	opt_val = true;
	if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_reuseaddr() fail, errno = %d\n", errno);
		close(sfd);
		return false;
	}
  
  //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
  //创建epoll fd 部分
  int epoll_fd = epoll_create(epoll_fd_max);
  if(epoll_fd == -1){
	  _printf("epoll_create() fail, errno: %d\n",errno);
    close(sfd);
		return false;
  }
  //设置监听epoll_fd 为异步fd 模式
  opt_val = fcntl(epoll_fd, F_SETFL, fcntl(epoll_fd, F_GETFD, 0)|O_NONBLOCK);
  if(opt_val == -1){
    _printf("epoll_fd %d set blocking fail, errno: %d\n", epoll_fd, errno);
    close(sfd);
    close(epoll_fd);
		return false;
  }

  //************************************************
  //执行监听
	struct sockaddr_in addr;
	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_addr.s_addr = inet_addr(srv_ip);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(srv_port);
  //bind
  opt_val = bind(sfd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
  if(opt_val == -1){
    _printf("socket %d bind fail, errno: %d\n", sfd, errno);
    close(sfd);
    close(epoll_fd);
    return false;
  }
  //start listen
  opt_val = listen(sfd, BACKLOG);
  if(opt_val == -1){
    _printf("socket %d listen fail, errno: %d\n", sfd, errno);
    close(sfd);
    close(epoll_fd);
    return false;
  }
  
  //加入是将sfd 和listen_ev 兴趣加入到epoll_fd 中
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLET; //读+边缘触发ET
  ev.data.fd = sfd;              //将listen_fd 加入
  opt_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev);
  if(opt_val == -1){
    _printf("epoll_ctl() socket %d fail, errno: %d\n", sfd, errno);
    close(sfd);
    close(epoll_fd);
    return false;
  }
  
  _inet_srv.cur_fds++;//epoll fds +1
  _inet_srv.fd_epoll = epoll_fd;//全局变量继承值
  _inet_srv.sfd_li = sfd;
  return true;
}

//询问epoll 是否有io 时间, 如果有, 全部收集到私有栈中
//加锁, 但只有监听主线程独占
static bool addto_epoll(int sfd_acc);
bool check_epoll(void){
  static struct epoll_event evs[epoll_fd_max];	//40960 个epoll 感兴趣事件
  
  int tmp;
  //获取epoll 中被触发的fd,返回到evs 事件数组容器中
  int wait_fds = epoll_wait(_inet_srv.fd_epoll, evs, _inet_srv.cur_fds, epoll_wait_timeout);//阻塞方式:第四参数为-1
  if(wait_fds > 0){
    //用循环体-处理-所有被触发的fd
    for(tmp = 0;tmp < wait_fds;tmp++){
      //如果是listen fd 发生事件
      if(evs[tmp].data.fd == _inet_srv.sfd_li && _inet_srv.cur_fds < epoll_fd_max){
        //接受客户端
        int sfd_acc = accept(_inet_srv.sfd_li, NULL, NULL);
        if(sfd_acc == -1){
          add_err_count();
          _printf("accept() fail, errno: %d\n", errno);
          continue;
        }
        else{//添加client 到epoll 中
          if(!addto_epoll(sfd_acc)){
            //addto_epoll() 错误
            add_err_count();
            _printf("addto_epoll() socket %d fail, errno: %d\n", sfd_acc, errno);
            shutdown(sfd_acc,2);
            close(sfd_acc);
            continue;
          }
          else{//addto_epoll() 正常
            assert(pthread_mutex_lock(&_inet_srv._mutex) == 0);//加锁
            ++_inet_srv.cur_fds;//epoll 句柄计数+1
            assert(pthread_mutex_unlock(&_inet_srv._mutex) == 0);
            add_accept_count();
            continue;
          }
        }
      }
      else{//如果是client fd 事件触发(可以是client fd 已经关闭, 可以是client socket 错误, 可以是client socket 有io 数据可读, 将io 任务抛给线程池函数去工作...)
        //如果对方直接把网线, socket 就是变成尸体, 在你的服务器上面恶心你...
        //所以设置socket 保活可以避免这个问题, 虽然不知道对方是否已经脱离你的逻辑节奏,
        //但是至少知道对方是否已经掉线
        if(evs[tmp].events == EPOLLIN){
          int *arg = (int*)malloc(sizeof(int));//任务函数的<参数内存分配>
          *arg = evs[tmp].data.fd;
          threadpool_add_task(&pool, _recv, arg);//将<任务函数指针>,<任务函数参数指针> 推入线程池
          continue;
        }
        if(evs[tmp].events == EPOLLIN + EPOLLRDHUP){//对端正常关闭
          printf("evs[tmp].events == EPOLLRDHUP\n\n");
          sock_close(evs[tmp].data.fd, false);//对端已经关闭, 就不能shutdown() ??
          continue;
        }
        if(evs[tmp].events == EPOLLIN + EPOLLHUP){//对端异常关闭
          printf("evs[tmp].events == EPOLLHUP\n\n");
          sock_close(evs[tmp].data.fd, false);
          continue;
        }
        if(evs[tmp].events == EPOLLIN + EPOLLERR){//对端发生错误
          printf("evs[tmp].events == EPOLLERR\n\n");
          sock_close(evs[tmp].data.fd, false);
          continue;
        }
        //未知错误
        add_err_count();
        _printf("epoll_wait() unknow error, sfd_acc = %d, errno: %d\n", evs[tmp].data.fd, errno);
        _printf("EPOLLRDHUP = %d,,EPOLLHUP = %d,, EPOLLERR = %d,,EPOLLIN = %d,,evs[tmp].events = %d,,evs[tmp].data.fd = %d\n\n", EPOLLRDHUP, EPOLLHUP, EPOLLERR, EPOLLIN, evs[tmp].events, evs[tmp].data.fd);
      }
    }
  }//if epoll_wait end
  else{//epoll_wait timeout
    if(wait_fds == 0)
      return true;
    //epoll_wait 异常错误
    printf("epoll_wait() fail, errno: %d\n", errno);
    return false;//这有这里会返回失败
  }
  return true;
}


//执行一次send(传入io_info_t 实体变量, 返回0/1)
//无锁, 仅socket 发送错误时, 清除时调用sock_close() 会自动上锁
void* _send(void *arg){
  io_info_t* pio_info = (io_info_t*)arg;
  ssize_t len = send(pio_info->sfd, pio_info->buf, pio_info->data_len, 0);
  if(len == -1){
  	switch(errno){
  		case EBADF://参数sfd 无效, 不是socket 文件描述符
  			_printf("send() fail,  errno = EBADF = %d\n", errno);
  			break;
  		case EFAULT: //参数中有一指针指向无法存取的内存空间
  			_printf("send() fail,  errno = EFAULT = %d\n", errno);
  			break;
  		case EINTR: //被信号所中断
  			_printf("send() fail,  errno = EINTR = %d\n", errno);
				break;
  		case EAGAIN: //socket fd 资源暂时不可用, 继续申请使用资源
  			_printf("send() fail,  errno = EAGAIN = %d\n", errno);
  			break;
  		case ENOBUFS: //系统的缓冲内存不足
  			_printf("send() fail,  errno = ENOBUFS = %d\n", errno);
  			break;
  		case EINVAL: //传给系统调用的参数不正确
  			_printf("send() fail,  errno = EINVAL = %d\n", errno);
  			break;
  		default:
  			_printf("send() fail,  errno = <default> = %d\n", errno);
  			break;
  	}
    free(arg);
    return NULL;//socket 错误
  }
	else if(len == 0){//对端已经close
		_printf("each other socket closed already when sfd = %d sending data...\n", pio_info->sfd);
    //_printf("pio_info->buf = %s, errno = %d, pio_info->data_len = %d\n",pio_info->buf,errno,pio_info->data_len);
		//free(arg);
		return (void*)true;
	}
  _printf("send data: %s, data len  :%d\n\n", pio_info->buf, len);
  //free(arg);
  add_send_count(len);
  return (void*)true;
}

//执行一次recv(传入sfd, 返回io_info_t 实体变量)
//无锁, 仅socket 接收错误时, 清除时调用sock_close() 会自动上锁
void* _recv(void *arg){
  int sfd_acc = *(int*)arg;
  free(arg);//释放任务函数参数
  io_info_t* pio_tmp = (io_info_t*)malloc(sizeof(io_info_t));
  memset(pio_tmp, '\0', sizeof(io_info_t));
  pio_tmp->sfd = sfd_acc;
  pio_tmp->is_rw = _is_read;
  int len = recv(sfd_acc, pio_tmp->buf, io_data_buf_max, 0);
	if(len == -1){
  	switch(errno){
  		case EBADF://参数sfd 无效, 不是socket 文件描述符
  			_printf("recv() fail,  errno = EBADF = %d\n", errno);
  			break;
  		case EFAULT: //参数中有一指针指向无法存取的内存空间
  			_printf("recv() fail,  errno = EFAULT = %d\n", errno);
  			break;
  		case EINTR: //被信号所中断
  			_printf("recv() fail,  errno = EINTR = %d\n", errno);
  			break;
  		case EAGAIN: //socket fd 资源暂时不可用, 继续申请使用资源
  			_printf("recv() fail,  errno = EAGAIN = %d\n", errno);
  			break;
  		case ENOBUFS: //系统的缓冲内存不足
  			_printf("recv() fail,  errno = ENOBUFS = %d\n", errno);
  			break;
  		case EINVAL: //传给系统调用的参数不正确
  			_printf("recv() fail,  errno = EINVAL = %d\n", errno);
  			break;
  		default:
  			_printf("recv() fail,  errno = <default> = %d\n", errno);
  			break;
  	}
  	free(pio_tmp);
		return NULL;
	}
	else if(len == 0){//对端已经close
		_printf("each other socket closed already when sfd = %d recving data...\n", sfd_acc);
    pio_tmp->data_len = 0;
		return pio_tmp;
	}
  
  pio_tmp->data_len = len;
  add_recv_count(len);
  return pio_tmp;//正常读取
}

//关闭socket
//有锁
void sock_close(int sfd, bool _shutdown){
  assert(pthread_mutex_lock(&_inet_srv._mutex) == 0);//加锁
  //从epoll 中删除
  int tmp = epoll_ctl(_inet_srv.fd_epoll, EPOLL_CTL_DEL, sfd, NULL);
  if(tmp == -1){
    add_err_count();
    _printf("epoll_ctl() EPOLL_CTL_DEL fail, errno: %d\n", errno);
    return;
  }
  --_inet_srv.cur_fds;//维系 cur_fds的正确性
  assert(pthread_mutex_unlock(&_inet_srv._mutex) == 0);//解锁
  //从系统中删除
  if(_shutdown)
    assert(shutdown(sfd,2) == 0);
  assert(close(sfd) != -1);
  add_killed_count();
}


//初始化inet_srv_t
//无锁, 仅调用一次
bool init_inet_srv_t(inet_srv_t* pinet_srv){
  bzero(pinet_srv, sizeof(inet_srv_t));//格式化结构体
  
  //启动socket 服务
  int status = start_inet_srv(pinet_srv);
  if(!status)    //这里虽然有些诡异, 但是鉴于init_inet_srv_t() 只能调用一次, 而且socket 部分可以自动清除
    return false;//这样做比较合理, 虽然看上去不顺眼
  
  //初始化互斥量
  status = pthread_mutex_init(&pinet_srv->_mutex, NULL);
  if(status != 0){
    _printf("pthread_mutex_init() fail, errno: %d\n",errno);
    sock_close(pinet_srv->sfd_li, true);//kill 监听socket
    close(pinet_srv->fd_epoll);//kill epoll fd
    return false;//初始化互斥量失败, 返回失败原因
  }
  
  pinet_srv->is_working = true;
  return true;
}

//清除inet_srv_t
//有锁
bool kill_inet_srv_t(inet_srv_t* pinet_srv){
  assert(pthread_mutex_lock(&pinet_srv->_mutex) == 0);//加锁
  pinet_srv->is_working = false;
  //关闭socket 服务
  sock_close(pinet_srv->sfd_li, true);//kill 监听socket
  close(pinet_srv->fd_epoll);//kill epoll fd
  assert(pthread_mutex_unlock(&pinet_srv->_mutex) == 0);//解锁
  //销毁互斥量
  int status = pthread_mutex_destroy(&pinet_srv->_mutex);
  if(status != 0){
    _printf("pthread_mutex_destroy() fail, errno: %d\n",errno);
    bzero(pinet_srv, sizeof(inet_srv_t));
    return false;
  }
  
  //清空结构体
  bzero(pinet_srv, sizeof(inet_srv_t));
  return true;
}


//隐藏静态函数--初始化io_info_t
//static void init_io_info_t(io_info_t* pio_info){bzero(pio_info, sizeof(io_info_t));}


//添加进epoll
static bool addto_epoll(int sfd_acc){
  //设置接收缓冲区
	int opt_val = sock_buf_recv;
	if(setsockopt(sfd_acc, SOL_SOCKET, SO_RCVBUF, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_revbuf() fail, errno = %d\n", errno);
		return false;
	}
	//设置发送缓冲区
	opt_val = sock_buf_send;
	if(setsockopt(sfd_acc, SOL_SOCKET, SO_SNDBUF, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_sndbuf() fail, errno = %d\n", errno);
		return false;
	}

	//设置关闭时如果有数据未发送完, 等待n 秒再关闭socket
	struct linger plinger;
	plinger.l_linger = sock_close_timeout;
	plinger.l_onoff = sock_close_timeout_onoff;
	if(setsockopt(sfd_acc, SOL_SOCKET, SO_LINGER, &plinger, sizeof(struct linger)) == -1){
		_printf("set_sockopt_linger() fail, errno = %d\n", errno);
		return false;
	}

	//设置地址重用
	opt_val = true;
	if(setsockopt(sfd_acc, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(int)) == -1){
		_printf("set_sockopt_reuseaddr() fail, errno = %d\n", errno);
		return false;
	}
  
  //设置保活
  opt_val = true;
  if(setsockopt(sfd_acc, SOL_SOCKET, SO_KEEPALIVE, &opt_val, sizeof(int)) == -1){
  	_printf("set_sockopt_keepalive() fail, errno = %d\n", errno);
  	return false;
  }

  //加入epoll 中
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR | EPOLLRDHUP; //读+边缘触发ET+关闭通知+错误通知+正常关闭
  ev.data.fd = sfd_acc;          //将sfd_acc 加入到epoll 中
  int xret = epoll_ctl(_inet_srv.fd_epoll, EPOLL_CTL_ADD, sfd_acc, &ev);
  if(xret == -1){
    _printf("epoll_ctl() socket %d fail, errno: %d\n", sfd_acc, errno);
    return false;
  }
  
  return true;
}
