//文件名: son/son.c
//
//描述: 这个文件实现SON进程 
//SON进程首先连接到所有邻居, 然后启动listen_to_neighbor线程, 每个该线程持续接收来自一个邻居的进入报文, 并将该报文转发给SIP进程. 
//然后SON进程等待来自SIP进程的连接. 在与SIP进程建立连接之后, SON进程持续接收来自SIP进程的sendpkt_arg_t结构, 并将接收到的报文发送到重叠网络中. 
//
//创建日期: 2013年

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "son.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//你应该在这个时间段内启动所有重叠网络节点上的SON进程
#define SON_START_DELAY 15

/**************************************************************/
//声明全局变量
/**************************************************************/

//将邻居表声明为一个全局变量 
nbr_entry_t* nt; 
//将与SIP进程之间的TCP连接声明为一个全局变量
int sip_conn; 

/**************************************************************/
//实现重叠网络函数
/**************************************************************/

// 这个线程打开TCP端口CONNECTION_PORT, 等待节点ID比自己大的所有邻居的进入连接,
// 在所有进入连接都建立后, 这个线程终止. 
void* waitNbrs(void* arg) {
	int listenfd,client_sockfd;
	socklen_t clilen;
	struct sockaddr_in cliaddr,servaddr;
	if((listenfd = socket(AF_INET,SOCK_STREAM,0))<0){
		perror("Problem in creating the socket");
		exit(2);
	}
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port=htons(CONNECTION_PORT);

	//bind the socket
	if((bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))) == -1)
	{
		perror("Cannot bind address");
		exit(0);
	}
	const int on = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	listen(listenfd,1);
	int conn_num=0;
	int nbnum = topology_getNbrNum();
	int my_id = topology_getMyNodeID();
	int i;
	for(i=0;i<nbnum;i++) if(nt[i].nodeID>my_id) conn_num++;
	while(conn_num>0){
		clilen = sizeof(cliaddr);
		//accept a connection
		if((client_sockfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen)) < 0)
		{
			perror("Cannot accept clients request");
			continue;
		}
		else
		{
			int nodeid=topology_getNodeIDfromip(&cliaddr.sin_addr);
			nt_addconn(nt, nodeid,client_sockfd);
			printf("Connect son, node id:%d\n",nodeid);
			conn_num--;
		}
	}
}

// 这个函数连接到节点ID比自己小的所有邻居.
// 在所有外出连接都建立后, 返回1, 否则返回-1.
int connectNbrs() {
	int nbnum = topology_getNbrNum();
	int myID = topology_getMyNodeID();
	int i;
	for(i=0;i<nbnum;i++)
		if(myID>nt[i].nodeID)
		{
				int sockfd;
				struct sockaddr_in servaddr;
				//Create a socket for the SON
				if((sockfd=socket (AF_INET, SOCK_STREAM, 0))<0){
				perror("Problem in creating the socket");
				return -1;
				}
				//Set the servaddr
				memset(&servaddr, 0, sizeof(servaddr));
				servaddr.sin_family = AF_INET;
				servaddr.sin_addr.s_addr = nt[i].nodeIP;
				servaddr.sin_port = htons(CONNECTION_PORT);

				//Connection of the client to the socket
				if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))<0){
					perror("Problem in connecting to the server");
					return -1;
				}
				nt[i].conn = sockfd;
		}
	return 1;
}

//每个listen_to_neighbor线程持续接收来自一个邻居的报文. 它将接收到的报文转发给SIP进程.
//所有的listen_to_neighbor线程都是在到邻居的TCP连接全部建立之后启动的. 
void* listen_to_neighbor(void* arg) {
	int i = *((int *)arg);
	free((int *)arg);
	sip_pkt_t pkt;
	while(recvpkt(&pkt,nt[i].conn)!=-1)
		forwardpktToSIP(&pkt, sip_conn);
	return 0;
}

//这个函数打开TCP端口SON_PORT, 等待来自本地SIP进程的进入连接. 
//在本地SIP进程连接之后, 这个函数持续接收来自SIP进程的sendpkt_arg_t结构, 并将报文发送到重叠网络中的下一跳. 
//如果下一跳的节点ID为BROADCAST_NODEID, 报文应发送到所有邻居节点.
void waitSIP() {
	//你需要编写这里的代码.
	int listenfd;
	socklen_t clilen;
	struct sockaddr_in cliaddr,servaddr;
	//create a socket for the socket
	//if socket<0 there was an error in the creation of the socket 
	if((listenfd = socket(AF_INET,SOCK_STREAM,0))<0){
		perror("Problem in creating the socket");
		exit(2);
	}

	//preparation of the socket address
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port=htons(SON_PORT);

	//bind the socket
	if((bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))) == -1)
	{
		perror("Cannot bind address");
		exit(0);
	}

	//set SO_REUSEADDR
	const int on = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));


	//listen to the socket by creating a connection queue,then wait for clients
	listen(listenfd,1);
	clilen = sizeof(cliaddr);
	//accept a connection
	if((sip_conn= accept(listenfd, (struct sockaddr*)&cliaddr, &clilen)) < 0)
	{
		perror("Cannot accept clients request");
		return;
	}
	sendpkt_arg_t argpkt;
	int nbnum = topology_getNbrNum();
	int i;
	while(getpktToSend(&argpkt.pkt,&argpkt.nextNodeID,sip_conn)!=-1)
	{
		if(argpkt.nextNodeID==BROADCAST_NODEID)
		{
			for(i=0;i<nbnum;i++)
				sendpkt(&argpkt.pkt,nt[i].conn);
		}
		else
		{
			for(i=0;i<nbnum;i++)
				if(nt[i].nodeID == argpkt.nextNodeID) sendpkt(&argpkt.pkt,nt[i].conn);
		}
	}
}

//这个函数停止重叠网络, 当接收到信号SIGINT时, 该函数被调用.
//它关闭所有的连接, 释放所有动态分配的内存.
void son_stop() {
	//你需要编写这里的代码.
	nt_destroy(nt);
	close(sip_conn);
}

int main() {
	//启动重叠网络初始化工作
	printf("Overlay network: Node %d initializing...\n",topology_getMyNodeID());	

	//创建一个邻居表
	nt = nt_create();
	//将sip_conn初始化为-1, 即还未与SIP进程连接
	sip_conn = -1;
	
	//注册一个信号句柄, 用于终止进程
	signal(SIGINT, son_stop);

	//打印所有邻居
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay network: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//启动waitNbrs线程, 等待节点ID比自己大的所有邻居的进入连接
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//等待其他节点启动
	sleep(SON_START_DELAY);
	
	//连接到节点ID比自己小的所有邻居
	connectNbrs();

	//等待waitNbrs线程返回
	pthread_join(waitNbrs_thread,NULL);	

	//此时, 所有与邻居之间的连接都建立好了
	
	//创建线程监听所有邻居
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay network: node initialized...\n");
	printf("Overlay network: waiting for connection from SIP process...\n");

	//等待来自SIP进程的连接
	waitSIP();
}
