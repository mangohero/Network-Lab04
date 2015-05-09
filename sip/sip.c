//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 15

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() { 
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
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(SON_PORT);

	//Connection of the client to the socket
	if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))<0){
		perror("Problem in connecting to the server");
		return -1;
	}
	return sockfd;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
	sip_pkt_t pkt;
	pkt.header.dest_nodeID=BROADCAST_NODEID;
	pkt.header.type=ROUTE_UPDATE;
	pkt.header.src_nodeID = topology_getMyNodeID();
	pkt_routeupdate_t rt;
	int N = topology_getNodeNum();
	int i;
	rt.entryNum = N;
	pkt.header.length = sizeof(sip_hdr_t) + sizeof(int) + N*sizeof(routeupdate_entry_t);
	while(1)
	{
		for(i=0;i<N;i++)
		{
			pthread_mutex_lock(dv_mutex);
			rt.entry[i].nodeID = dv[0].dvEntry[i].nodeID;
			rt.entry[i].cost = dv[0].dvEntry[i].cost;
			pthread_mutex_unlock(dv_mutex);
		}
		memcpy(&pkt.data,&rt,sizeof(int) + N*sizeof(routeupdate_entry_t));
		son_sendpkt(BROADCAST_NODEID, &pkt , son_conn);
		sleep(ROUTEUPDATE_INTERVAL);
	}
  	return 0;
}

//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {
	sip_pkt_t pkt;

	int myNodeID = topology_getMyNodeID();
	int nbnum = topology_getNbrNum();
	int i,j,l;
	pkt_routeupdate_t *rt;
	while(son_recvpkt(&pkt,son_conn)>0) {
		printf("Routing: received a packet from neighbor %d, type:%d\n",pkt.header.src_nodeID,pkt.header.type);
		if(pkt.header.type == SIP && pkt.header.dest_nodeID == myNodeID)
		{
			seg_t segPtr;
			memcpy(&segPtr,&pkt.data,pkt.header.length);
			forwardsegToSTCP(stcp_conn,pkt.header.src_nodeID,&segPtr);		
		}
		else if(pkt.header.type==SIP)
		{
			int nextNodeID;
			pthread_mutex_lock(routingtable_mutex);
			nextNodeID = routingtable_getnextnode(routingtable,pkt.header.dest_nodeID);
			pthread_mutex_unlock(routingtable_mutex);
			son_sendpkt(nextNodeID,&pkt,son_conn);
	
		}
		else if(pkt.header.type == ROUTE_UPDATE)
		{
			/*更新路由信息*/
			for(i=1;i<=nbnum;i++)
			if(dv[i].nodeID == pkt.header.src_nodeID)
			{
				rt = (pkt_routeupdate_t *)&pkt.data;
				for(j=0;j<rt->entryNum;j++)
				{
					pthread_mutex_lock(dv_mutex);
					dv[i].dvEntry[j].nodeID=rt->entry[j].nodeID;	
					dv[i].dvEntry[j].cost=rt->entry[j].cost;
					pthread_mutex_unlock(dv_mutex);
				}
				break;
			}
			for(j=0;j<rt->entryNum;j++)
			{
				for(l=0;l<rt->entryNum;l++)
					if(dv[0].dvEntry[j].nodeID == dv[i].dvEntry[l].nodeID && dv[0].dvEntry[j].cost>(dv[i].dvEntry[l].cost + nbrcosttable_getcost(nct,dv[i].nodeID)))
					{
						pthread_mutex_lock(dv_mutex);
						dv[0].dvEntry[j].cost = dv[i].dvEntry[l].cost + nbrcosttable_getcost(nct,dv[i].nodeID);
						pthread_mutex_unlock(dv_mutex);
						pthread_mutex_lock(routingtable_mutex);
						routingtable_setnextnode(routingtable,dv[0].dvEntry[j].nodeID,dv[i].nodeID);
						pthread_mutex_unlock(routingtable_mutex);
					}	
			}
		}
	}
	close(son_conn);
	son_conn = -1;
	pthread_exit(NULL);


}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	//你需要编写这里的代码.
	close(son_conn);
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() {

	int listenfd,client_sockfd;
	socklen_t clilen;
	struct sockaddr_in cliaddr,servaddr;
	if((listenfd = socket(AF_INET,SOCK_STREAM,0))<0){
		perror("Problem in creating the socket");
		exit(2);
	}
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port=htons(SIP_PORT);

	//bind the socket
	if((bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))) == -1)
	{
		perror("Cannot bind address");
		exit(0);
	}
	const int on = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	listen(listenfd,1);

	clilen = sizeof(cliaddr);
	if((client_sockfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen)) < 0)
	{		
			perror("Cannot accept clients request");
			return;
	}
	printf("Success accept stcp connection!\n");
	stcp_conn = client_sockfd;

	int dest_nodeID;
	seg_t segPtr;
	while(getsegToSend(stcp_conn,&dest_nodeID,&segPtr)!=-1)
	{
		/*获取路由信息，发送给son层*/
		sip_pkt_t pkt;
		pkt.header.src_nodeID=topology_getMyNodeID();
		pkt.header.dest_nodeID=dest_nodeID;
		pkt.header.type = SIP;
		pkt.header.length = segPtr.header.length+sizeof(sip_hdr_t);
		memcpy(&pkt.data,&segPtr,segPtr.header.length);
		int nextNodeID;
		pthread_mutex_lock(routingtable_mutex);
		nextNodeID = routingtable_getnextnode(routingtable,dest_nodeID);
		pthread_mutex_unlock(routingtable_mutex);
		printf("send a sip message to %d, destNodeID:%d\n",nextNodeID,dest_nodeID);
		son_sendpkt(nextNodeID,&pkt,son_conn);
	}
}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, pls wait...\n");

	//初始化全局变量
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	son_conn = -1;
	stcp_conn = -1;

	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程 
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);		
	}
	
	//启动线程处理来自SON进程的进入报文 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("SIP layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP(); 

}
