/*
 * sock_cli_once_loop.cpp
 *
 *  Created on: Jul 15, 2018
 *      Author: adan
 */


#include <stdio.h>

void help(void){
printf(" \
单功能模块, shell 传入参数必须是: \nsock_cli_once <测试次数> <server ip> <service port> <发送到srv 的数据>\n\n \
测试次数为正整数, 数量不限, 必须大于0 次\n \
不接受异常参数, 该版本不会自动解析dns 来获取port\n \
结果: 根据发送数据样板, 循环发/收/close 一次, 收到数据自动打印出来\n\n\n");
}

#ifdef _printf
#else
//可变参数宏定义(支持格式化输入参数)
#define _printf(args...) printf(args)
#endif

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

//socket 选项约定参数
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

//设置关闭时如果有数据未发送完, 等待3s 再关闭socket
#define sock_close_timeout 3

//********************************************
//全局变量
unsigned int count;
unsigned int tmp;
#define ipaddr_len_max 64
char srv_ip[ipaddr_len_max];
unsigned srv_port;


//互交一次!!
#define err_test_count 32 //如果测试次数过大, 错误超过32 次自动结束
unsigned int right_count;
unsigned int error_count;
#define strlen_max_time 64
char start_time[strlen_max_time];
char end_time[strlen_max_time];
#define send_data_size_max 2048
char sock_rw_buf[send_data_size_max];
char sock_rw_buf_backup[send_data_size_max];

bool test_once(char* send_data_buf);//执行自动化互交once 的函数


#include <unistd.h>
#include <fcntl.h>
int main(int argc, char** argv){
  if(argc != 5){
    help();
    printf("\n目前参数个数为::<%d>\n\n", argc);
    return 0;
  }
  /*
  //[[ atoi() demo !!]]
  printf("%i\n", atoi(" -123junk"));
	printf("%i\n", atoi("0"));
	printf("%i\n", atoi("junk"));      // 无可进行的转换
	printf("%i\n", atoi("2147483648"));// 在int 范围外(int 越界)
	//-123
	//0
	//0
	//-2147483648
	*/
  count = atoi(argv[1]);
  assert(count != 0);

  memset(&srv_ip, '\0', ipaddr_len_max);
  assert(argv[2] != NULL);
  strncpy(srv_ip,argv[2],ipaddr_len_max);

  srv_port = atoi(argv[3]);
  assert(srv_port != 0);

  memset(&sock_rw_buf, '\0', send_data_size_max);
  assert(argv[4] != NULL);
  strncpy(sock_rw_buf,argv[4],send_data_size_max);
  strncpy(sock_rw_buf_backup,argv[4],send_data_size_max);


  //输入值检测ok ..
  //printf("count = %d, tmp = %d, is_sync = %d\n", count, tmp, is_sync);

  //单次socket 循环互交开始
  memset(&start_time, '\0', strlen_max_time);
  memset(&end_time, '\0', strlen_max_time);
  time_t xtime = time(NULL);
  strncpy(start_time,ctime(&xtime),strlen_max_time);
  int i = 0;
  for(;i < count;i++){
    strncpy(sock_rw_buf,sock_rw_buf_backup,send_data_size_max);
    if(test_once(sock_rw_buf))
    	right_count++;
    else{
    	error_count++;
      if(error_count > err_test_count){
      	_printf("\n\n\n测试的错误次数已经超过32 次, 程序自动结束\n\n");
        break;
      }
    }
  }
  xtime = time(NULL);
  strncpy(end_time,ctime(&xtime),strlen_max_time);

  //打印测试结果
  printf("\n\n<<test report>>:\n \
server ip addr: %s\n \
   server port: %d\n \
start at: %s \
  end at: %s \
 test count: %d\n \
right count: %d\n \
error count: %d\n \
thank you\n\n\n",srv_ip, srv_port, start_time, end_time, count, right_count, error_count);

  return 0;
}


//互交一次!!
bool test_once(char* send_data_buf){
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
	plinger.l_onoff = true;
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


  //执行connect()
	struct sockaddr_in addr;
	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_addr.s_addr = inet_addr(srv_ip);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(srv_port);
  if(connect(sfd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) == -1){
		_printf("connect() fail, errno = %d\n", errno);
		close(sfd);
		return false;
  }

  //执行发送操作
  ssize_t len = send(sfd, send_data_buf, sizeof(sock_rw_buf), 0);//flags=0 阻塞
	if(len == -1){
		_printf("send() fail, errno = %d\n", errno);
		shutdown(sfd,2);
		close(sfd);
		return false;//socket 错误
	}
	else if(len == 0){//对端已经close
		_printf("each other closed already when sfd = %d sending data...\n", sfd);
		shutdown(sfd,2);
		close(sfd);
		return true;
	}
	else if(len > 0)
		_printf("send data: %s\n", send_data_buf);

    
  //接收数据(肯定要同步, 否则没有数据可以显示, 但是有recv_timeout 限制)
	memset(&sock_rw_buf, '\0', sizeof(sock_rw_buf));
	len = recv(sfd, &sock_rw_buf, sizeof(sock_rw_buf), 0);//flags=0 阻塞
	if(len == -1){
		_printf("recv() fail, errno = %d\n", errno);
		shutdown(sfd,2);
		close(sfd);
		return false;//socket 错误
	}
	else if(len == 0){//对端已经close
		_printf("each other closed already when sfd = %d recving data...\n", sfd);
		shutdown(sfd,2);
		close(sfd);
		return true;
	}
	else if(len > 0)
		_printf("recv data: %s\n", send_data_buf);

	shutdown(sfd,2);
	close(sfd);
	return true;
}
