//文件名: server/stcp_server.c
//
//描述: 这个文件包含STCP服务器接口实现. 
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"

//声明tcbtable为全局变量
server_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.


int getCurrentTime1();
void* sendThread(void* servertcb);
void* sendBuf_timer(void* servertcb);

void stcp_server_init(int conn) 
{	
	sip_conn=conn;
	pthread_t pthd;
	if(pthread_create(&pthd, NULL, seghandler, (void *)0)!= 0){
		perror("Thread creation problem");
		exit(2);
	}
 	return;
}

// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_server_sock(unsigned int server_port) 
{
	int i=0;
	while(tcbtable[i]!=NULL) i++;
	if(i==MAX_TRANSPORT_CONNECTIONS) return -1;
	tcbtable[i] =  (server_tcb_t*)malloc(sizeof(server_tcb_t));
	tcbtable[i]->server_nodeID = topology_getMyNodeID();
	tcbtable[i]->server_portNum = server_port;
	tcbtable[i]->client_nodeID = 0;
	tcbtable[i]->client_portNum = 0;
	tcbtable[i]->state = CLOSED;
	//send
	tcbtable[i]->next_seqNum = 0;
	tcbtable[i]->sendBufHead = NULL;
	tcbtable[i]->sendBufunSend = NULL;
	tcbtable[i]->sendBufTail = NULL;
	tcbtable[i]->unAck_segNum = 0;
	tcbtable[i]->send_bufMutex=(pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tcbtable[i]->send_bufMutex,NULL);
	//rec
	tcbtable[i]->recvBuf = (char *)malloc(RECEIVE_BUF_SIZE);
	tcbtable[i]->usedBufLen=0;
	tcbtable[i]->rec_bufMutex=(pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tcbtable[i]->rec_bufMutex,NULL);
	return i;
}

// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后启动定时器进入忙等待直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
int stcp_server_accept(int sockfd) 
{
	tcbtable[sockfd]->state = LISTENING;
	while(tcbtable[sockfd]->state != CONNECTED) usleep(10);
	return 0;
}

//send function
int stcp_server_send(int sockfd, void* data, unsigned int length) 
{
	if(length <= MAX_SEG_LEN){
		server_tcb_t* tmp = tcbtable[sockfd];
		seg_t tmpseg;
		tmpseg.header.dest_port = tmp->client_portNum;
		tmpseg.header.src_port = tmp->server_portNum;
		tmpseg.header.length = length+sizeof(stcp_hdr_t);
		tmpseg.header.type = DATA;
		tmpseg.header.seq_num = tmp->next_seqNum;
		tmpseg.header.ack_num = 0;
		tmpseg.header.rcv_win = 0;
		tmpseg.header.checksum = 0;
		memcpy(tmpseg.data, (char *)data,length);
		tmpseg.header.checksum = checksum(&tmpseg);
		
		segBuf_t* p = (segBuf_t*)malloc(sizeof(segBuf_t));
		p->seg = tmpseg;
		p->sendTime = 0;
		p->next = NULL;
		pthread_mutex_lock(tmp->send_bufMutex);
		tmp->next_seqNum += length;
		if(tmp->sendBufHead == NULL){
			tmp->sendBufHead = p;
			tmp->sendBufTail = p;
			tmp->sendBufunSend = p;
			//启动sendBuf_timer
			pthread_t phd;
			pthread_create(&phd,NULL,sendBuf_timer,(void*)tmp);
			//启动sendThread
			pthread_t phd2;
			pthread_create(&phd2,NULL,sendThread,(void*)tmp);
		}
		else{
			tmp->sendBufTail->next = p;
			tmp->sendBufTail = p;
			if(tmp->sendBufunSend == NULL){//特殊情况，说明之前没有未发送的数据
				tmp->sendBufunSend = p;
			}
		}
		pthread_mutex_unlock(tmp->send_bufMutex);
	}
	else{//最多MAX_SEG_LEN，大于的要 递归地分成两部分进行发送
		char substr[MAX_SEG_LEN];
		strncpy(substr, (char *)data, MAX_SEG_LEN);
		stcp_server_send(sockfd, (void *)substr, MAX_SEG_LEN);
		stcp_server_send(sockfd, (void *)((char *)data+MAX_SEG_LEN), length-MAX_SEG_LEN);
	}
	return 1;
}

// 接收来自STCP客户端的数据. 这个函数每隔RECVBUF_POLLING_INTERVAL时间
// 就查询接收缓冲区, 直到等待的数据到达, 它然后存储数据并返回1. 如果这个函数失败, 则返回-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int length) 
{
	while(1)
	{
		pthread_mutex_lock(tcbtable[sockfd]->rec_bufMutex);
		if(tcbtable[sockfd]->usedBufLen<length)
		{
			pthread_mutex_unlock(tcbtable[sockfd]->rec_bufMutex);
			sleep(RECVBUF_POLLING_INTERVAL);
		}
		else{
			pthread_mutex_unlock(tcbtable[sockfd]->rec_bufMutex);
			 break;
		}
	}
	pthread_mutex_lock(tcbtable[sockfd]->rec_bufMutex);
	memcpy((char *)buf,tcbtable[sockfd]->recvBuf,length);
	memcpy(tcbtable[sockfd]->recvBuf,tcbtable[sockfd]->recvBuf+length,tcbtable[sockfd]->usedBufLen-length);
	tcbtable[sockfd]->usedBufLen-=length;
	pthread_mutex_unlock(tcbtable[sockfd]->rec_bufMutex);
	return 1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_server_close(int sockfd) 
{
	if(tcbtable[sockfd]!=NULL && tcbtable[sockfd]->state!=CLOSED)
	{
		free(tcbtable[sockfd]->rec_bufMutex);
		free(tcbtable[sockfd]->send_bufMutex);
		free(tcbtable[sockfd]->recvBuf);
		free(tcbtable[sockfd]);
		tcbtable[sockfd]=NULL;
	 	return -1;
	}
	printf("Disconnect with a client,server port:%d   client port:%d\n",tcbtable[sockfd]->server_portNum,tcbtable[sockfd]->client_portNum);
	free(tcbtable[sockfd]->rec_bufMutex);
	free(tcbtable[sockfd]->send_bufMutex);
	free(tcbtable[sockfd]->recvBuf);
	free(tcbtable[sockfd]);
	tcbtable[sockfd]=NULL;
	return 0;
}

// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环, 
// 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.

void *clock_for_close(void* arg);

void* seghandler(void* arg) 
{
	seg_t seg_data;
	segBuf_t* p;
	int i;
	int ack_num;
	int nodeID;
	while(sip_recvseg(sip_conn,&nodeID,&seg_data)!=-1)
	{
		i=0;
		unsigned int temp;
		while(tcbtable[i]!=NULL && tcbtable[i]->server_portNum != seg_data.header.dest_port && i<MAX_TRANSPORT_CONNECTIONS)
			i++;
		if(i>=MAX_TRANSPORT_CONNECTIONS) return;
		switch(seg_data.header.type)
		{
			case SYN:printf("Accept a SYN message!\n");
				 temp=seg_data.header.src_port;
				 seg_data.header.src_port=seg_data.header.dest_port;
				 seg_data.header.dest_port=temp;
				 seg_data.header.type = SYNACK;
				 seg_data.header.checksum=0;
				 seg_data.header.length=sizeof(stcp_hdr_t);
				 seg_data.header.checksum=checksum(&seg_data);
				 printf("Send a SYNACK message!\n");
				 if(tcbtable[i]->state==LISTENING) 
				 {
					tcbtable[i]->client_portNum = temp;
					tcbtable[i]->state = CONNECTED;
					tcbtable[i]->client_nodeID=nodeID;
					tcbtable[i]->expect_seqNum=seg_data.header.seq_num;
				 }
				 sip_sendseg(sip_conn,tcbtable[i]->client_nodeID,&seg_data);
				 break;
			case FIN:printf("Accept a FIN message!\n");
				 temp=seg_data.header.src_port;
				 seg_data.header.src_port=seg_data.header.dest_port;
				 seg_data.header.dest_port=temp;
			         seg_data.header.type = FINACK;
				 seg_data.header.checksum=0;
				 seg_data.header.length=sizeof(stcp_hdr_t);
				 seg_data.header.checksum=checksum(&seg_data);
				 printf("Send a FINACK message!\n");
				 sip_sendseg(sip_conn,tcbtable[i]->client_nodeID,&seg_data);
				 if(tcbtable[i]->state==CONNECTED)
				 {
				   tcbtable[i]->state = CLOSEWAIT;
				   pthread_t pthd;
				   if(pthread_create(&pthd, NULL, clock_for_close, (void *)i)!= 0){
					perror("Thread creation problem");
					exit(2);
				   }
				 }
				 break;
			case DATA:
				if(tcbtable[i]->state==CONNECTED)
				  {
					if(seg_data.header.seq_num==tcbtable[i]->expect_seqNum)
					{
						int len=seg_data.header.length-sizeof(stcp_hdr_t),j;
						pthread_mutex_lock(tcbtable[i]->rec_bufMutex);
						memcpy((char *)tcbtable[i]->recvBuf+tcbtable[i]->usedBufLen,(char *)&seg_data.data,len);
						tcbtable[i]->usedBufLen+=len;
						tcbtable[i]->expect_seqNum+=len;
						pthread_mutex_unlock(tcbtable[i]->rec_bufMutex);
						
					}
					temp=seg_data.header.src_port;
				 	seg_data.header.src_port=seg_data.header.dest_port;
					seg_data.header.dest_port=temp;
					seg_data.header.type = DATAACK;
					seg_data.header.ack_num = tcbtable[i]->expect_seqNum;	
					seg_data.header.checksum=0;
					seg_data.header.length=sizeof(stcp_hdr_t);
				        seg_data.header.checksum=checksum(&seg_data);
					sip_sendseg(sip_conn,tcbtable[i]->client_nodeID,&seg_data);	
				  } 
				 break;
			case DATAACK:
				if(tcbtable[i]->state == CONNECTED){
					p = tcbtable[i]->sendBufHead;
					ack_num = seg_data.header.ack_num;
					//释放空间
					pthread_mutex_lock(tcbtable[i]->send_bufMutex);
					while(tcbtable[i]->unAck_segNum > 0 && p != NULL && p->seg.header.seq_num < ack_num ){
						tcbtable[i]->sendBufHead = tcbtable[i]->sendBufHead->next;
						free(p);
						p = tcbtable[i]->sendBufHead;
						tcbtable[i]->unAck_segNum--;
					}
					pthread_mutex_unlock(tcbtable[i]->send_bufMutex);
				}
				break;
			default: printf("error type!\n");break;
		}
	}
	return 0;
}

void *clock_for_close(void* arg) {
	usleep(FIN_TIMEOUT/1000);
	tcbtable[(int)arg]->state=CLOSED;
}

void* sendBuf_timer(void* servertcb)
{
	while(((server_tcb_t*)servertcb)->sendBufHead != NULL){
		segBuf_t* p = ((server_tcb_t*)servertcb)->sendBufHead;
		segBuf_t* q= ((server_tcb_t*)servertcb)->sendBufunSend;
		if(p!=q && getCurrentTime1()-p->sendTime > (DATA_TIMEOUT / 1000000) ){
			pthread_mutex_lock( ((server_tcb_t*)servertcb)->send_bufMutex );
			((server_tcb_t*)servertcb)->sendBufunSend = ((server_tcb_t*)servertcb)->sendBufHead;
			((server_tcb_t*)servertcb)->unAck_segNum=0;
			pthread_mutex_unlock( ((server_tcb_t*)servertcb)->send_bufMutex );
		}
		usleep(SENDBUF_POLLING_INTERVAL/1000);
	}
	return;
}

void* sendThread(void* servertcb){
	server_tcb_t* tmp = (server_tcb_t*)servertcb;
	//发送到GBN_WINDOW为止
	while(1){
		if(tmp->unAck_segNum <= GBN_WINDOW && tmp->sendBufunSend != NULL)
		{
			pthread_mutex_lock( ((server_tcb_t*)servertcb)->send_bufMutex );
			tmp->sendBufunSend->sendTime = getCurrentTime1();
			sip_sendseg(sip_conn, tmp->client_nodeID,&tmp->sendBufunSend->seg);
			tmp->sendBufunSend = tmp->sendBufunSend->next;
			tmp->unAck_segNum++;
			pthread_mutex_unlock( ((server_tcb_t*)servertcb)->send_bufMutex );
		}
		else if(tmp->sendBufunSend == NULL && tmp->sendBufHead==NULL) break;
	}
	return ;
}
int getCurrentTime1(){
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

