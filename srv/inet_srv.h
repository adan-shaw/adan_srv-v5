#include <pthread.h>
//epoll 相关定义
#define	epoll_fd_max 40960
#define epoll_wait_timeout 1 //1 s 超时

//socket 相关定义(选项约定参数)
//********************************************
//socket 协议簇类型
#define sock_proto AF_INET
//socket 类型(第三参数为0, 默认跟从创建...)
//ps: 反正都是业务性的socket, 搞这么复杂也没用)
#define sock_type SOCK_STREAM

//接收缓冲区 = 64*1024 = 65536 = 64kb
#define sock_buf_recv 4096
//发送缓冲区
#define sock_buf_send 4096

//设置关闭时如果有数据未发送完, 等待1s 再关闭socket(优雅关闭 控制)
#define sock_close_timeout 1 //1s 优雅关闭
#define sock_close_timeout_onoff true //1 = on, 0 = off

#define io_data_buf_max 4096 //4kb 的数据缓冲区

#define srv_ip "127.0.0.1"
#define srv_port 6666
#define BACKLOG 1024 //监听队列最大长度 1024

#define _is_read 0//接收 = 读 = 0
#define _is_write 1//发送 = 写 = 1

//********************************************




//服务器监听主体info
typedef struct inet_srv{
  unsigned int sfd_li;   //监听sfd
  unsigned int fd_epoll; //epoll fd
  int cur_fds;           //epoll 中的fd 计数 (也是当前online 的人数)
  bool is_working;       //socket 服务是否正常工作
  
  pthread_mutex_t _mutex;//自带访问控制互斥量
}inet_srv_t; 

//单词io 所需的info[统一为异步操作]
typedef struct io_info{
  int sfd;//目标sfd
  char buf[io_data_buf_max];//io 缓冲区(虽然这个结构体都是new 的, 但是规范性new 可以提高重用率, 而且recv 时肯定需要预先设置缓冲区大小)
  bool is_rw;//读写区分, 0 = 读, 1 = 写
  int data_len;
}io_info_t;


//启动epoll 服务
bool start_inet_srv(inet_srv_t* pinet_srv);

//询问epoll 是否有io 时间, 如果有, 全部收集到私有栈中
bool check_epoll(void);

//执行一次send
void* _send(void *arg);

//执行一次recv
void* _recv(void *arg);

//关闭socket
void sock_close(int sfd, bool _shutdown);


//初始化inet_srv_t
bool init_inet_srv_t(inet_srv_t* pinet_srv);
//清除inet_srv_t
bool kill_inet_srv_t(inet_srv_t* pinet_srv);
//隐藏静态函数--初始化io_info_t
//static void init_io_info_t(io_info_t* pio_info);

