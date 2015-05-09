//文件名: client/stcp_client.c
//
//描述: 这个文件包含STCP客户端接口实现 
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "stcp_client.h"
#include "../common/seg.h"

//声明tcbtable为全局变量
client_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的TCP连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
void stcp_client_init(int conn) 
{
 	sip_conn = conn;
	pthread_t phd;
	pthread_create(&phd,NULL,seghandler,(void*)0);
}
int getCurrentTime1();
void* sendThread(void* clienttcb);
// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_client_sock(unsigned int client_port) 
{
	int i;
	for(i=0 ; i < MAX_TRANSPORT_CONNECTIONS ;i++){
		if(tcbtable[i] == NULL){
			tcbtable[i] = (client_tcb_t*)malloc(sizeof(client_tcb_t));
			tcbtable[i]->server_nodeID = 0;
			tcbtable[i]->server_portNum = 0;
			tcbtable[i]->client_nodeID = topology_getMyNodeID();
			tcbtable[i]->client_portNum = client_port;
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
	}
	return -1;
}

// 这个函数用于连接服务器. 它以套接字ID, 服务器节点ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器节点ID和服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	int num=0;
	client_tcb_t* tmp = tcbtable[sockfd];
	tmp->server_portNum = server_port;
	tmp->server_nodeID=nodeID;
	tmp->state = SYNSENT;
	seg_t tmpseg;
	tmpseg.header.src_port = tmp->client_portNum;
	tmpseg.header.dest_port = tmp->server_portNum;
	tmpseg.header.length = sizeof(stcp_hdr_t);
	tmpseg.header.type = SYN;
	tmpseg.header.seq_num = 0;
	tmpseg.header.ack_num = 0;
	tmpseg.header.rcv_win = 0;
	tmpseg.header.checksum = 0;
	tmpseg.header.checksum = checksum(&tmpseg);

	sip_sendseg(sip_conn,nodeID,&tmpseg);
	//num++;
	usleep(SYN_TIMEOUT/1000);
	while(num<SYN_MAX_RETRY && tmp->state == SYNSENT){
		//定时器
		sip_sendseg(sip_conn,nodeID, &tmpseg);
		num++;
		usleep(SYN_TIMEOUT/1000);
	}
	if(num == SYN_MAX_RETRY)
	{
		tmp->state = CLOSED;
		return -1;
	}
	else 
		return 1;
}

// 发送数据给STCP服务器. 这个函数使用套接字ID找到TCB表中的条目.
// 然后它使用提供的数据创建segBuf, 将它附加到发送缓冲区链表中.
// 如果发送缓冲区在插入数据之前为空, 一个名为sendbuf_timer的线程就会启动.
// 每隔SENDBUF_ROLLING_INTERVAL时间查询发送缓冲区以检查是否有超时事件发生. 
// 这个函数在成功时返回1，否则返回-1. 
// stcp_client_send是一个非阻塞函数调用.
// 因为用户数据被分片为固定大小的STCP段, 所以一次stcp_client_send调用可能会产生多个segBuf
// 被添加到发送缓冲区链表中. 如果调用成功, 数据就被放入TCB发送缓冲区链表中, 根据滑动窗口的情况,
// 数据可能被传输到网络中, 或在队列中等待传输.
int stcp_client_send(int sockfd, void* data, unsigned int length) 
{
	if(length <= MAX_SEG_LEN){
		client_tcb_t* tmp = tcbtable[sockfd];
		seg_t tmpseg;
		tmpseg.header.src_port = tmp->client_portNum;
		tmpseg.header.dest_port = tmp->server_portNum;
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
		stcp_client_send(sockfd, (void *)substr, MAX_SEG_LEN);
		stcp_client_send(sockfd, (void *)((char *)data+MAX_SEG_LEN), length-MAX_SEG_LEN);
	}
	return 1;
}


//rec function

int stcp_client_recv(int sockfd, void* buf, unsigned int length) 
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

// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN段给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
int stcp_client_disconnect(int sockfd) 
{
	int num=0;
	client_tcb_t* tmp = tcbtable[sockfd];
	tmp->state = FINWAIT;
	seg_t tmpseg;
	tmpseg.header.src_port = tmp->client_portNum;
	tmpseg.header.dest_port = tmp->server_portNum;
	tmpseg.header.length = sizeof(stcp_hdr_t);
	tmpseg.header.type = FIN;
	tmpseg.header.seq_num = tmp->next_seqNum;
	tmpseg.header.ack_num = 0;
	tmpseg.header.rcv_win = 0;
	tmpseg.header.checksum = 0;
	tmpseg.header.checksum = checksum(&tmpseg);

	sip_sendseg(sip_conn,tmp->server_nodeID, &tmpseg);
	//num++;
	usleep(SYN_TIMEOUT/1000);
	while(num < FIN_MAX_RETRY && tmp->state == FINWAIT){
		//定时器
		sip_sendseg(sip_conn,tmp->server_nodeID,&tmpseg);
		num++;
		usleep(SYN_TIMEOUT/1000);
	}
	if(num == FIN_MAX_RETRY){
		tmp->state = CLOSED;
		return -1;
	}
	else return 1;
}

// 关闭STCP客户
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd) {
	if(tcbtable[sockfd]!=NULL && tcbtable[sockfd]->state!=CLOSED)
	{
		free(tcbtable[sockfd]->rec_bufMutex);
		free(tcbtable[sockfd]->send_bufMutex);
		free(tcbtable[sockfd]->recvBuf);
		free(tcbtable[sockfd]);
		tcbtable[sockfd]=NULL;
	 	return -1;
	}
	printf("Disconnect with a server,server port:%d   client port:%d\n",tcbtable[sockfd]->server_portNum,tcbtable[sockfd]->client_portNum);
	free(tcbtable[sockfd]->rec_bufMutex);
	free(tcbtable[sockfd]->send_bufMutex);
	free(tcbtable[sockfd]->recvBuf);
	free(tcbtable[sockfd]);
	tcbtable[sockfd]=NULL;
	return 0;
}

// 处理进入段的线程
//
// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明重叠网络连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg) {

	seg_t seg_data;
	segBuf_t* p;
	int i;
	int ack_num;
	int nodeID;
	while(sip_recvseg(sip_conn,&nodeID,&seg_data)!=-1){
		i=0;
		unsigned int temp;
		while(tcbtable[i]!=NULL && tcbtable[i]->server_portNum!=seg_data.header.src_port && i<MAX_TRANSPORT_CONNECTIONS)i++;
		if(i >= MAX_TRANSPORT_CONNECTIONS)  return;
		switch(seg_data.header.type){
		case SYNACK:
			if(tcbtable[i]->state == SYNSENT) tcbtable[i]->state=CONNECTED;
			break;
		case FINACK:
			if(tcbtable[i]->state == FINWAIT) tcbtable[i]->state=CLOSED;
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
		case DATA:
			if(tcbtable[i]->state==CONNECTED)
			{
				if(seg_data.header.seq_num==tcbtable[i]->expect_seqNum)
				{
					int len=seg_data.header.length-sizeof(stcp_hdr_t);
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
				sip_sendseg(sip_conn,tcbtable[i]->server_nodeID,&seg_data);	
			} 
			 break;
		default:
			printf("server wrong\n");break;
	}
	}
  return 0;
}


// 这个线程持续轮询发送缓冲区以触发超时事件. 如果发送缓冲区非空, 它应一直运行.
// 如果(当前时间 - 第一个已发送但未被确认段的发送时间) > DATA_TIMEOUT, 就发生一次超时事件.
// 当超时事件发生时, 重新发送所有已发送但未被确认段. 当发送缓冲区为空时, 这个线程将终止.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* sendBuf_timer(void* clienttcb)
{
	while(((client_tcb_t*)clienttcb)->sendBufHead != NULL){
		segBuf_t* p = ((client_tcb_t*)clienttcb)->sendBufHead;
		segBuf_t* q= ((client_tcb_t*)clienttcb)->sendBufunSend;
		if(p!=q && getCurrentTime1()-p->sendTime > (DATA_TIMEOUT / 1000000) ){
			pthread_mutex_lock( ((client_tcb_t*)clienttcb)->send_bufMutex );
			((client_tcb_t*)clienttcb)->sendBufunSend = ((client_tcb_t*)clienttcb)->sendBufHead;
			((client_tcb_t*)clienttcb)->unAck_segNum=0;
			pthread_mutex_unlock( ((client_tcb_t*)clienttcb)->send_bufMutex );
		}
		usleep(SENDBUF_POLLING_INTERVAL/1000);
	}
	return;
}
void* sendThread(void* clienttcb){
	client_tcb_t* tmp = (client_tcb_t*)clienttcb;
	//发送到GBN_WINDOW为止
	while(1){
		if(tmp->unAck_segNum <= GBN_WINDOW && tmp->sendBufunSend != NULL)
		{
			pthread_mutex_lock( ((client_tcb_t*)clienttcb)->send_bufMutex );
			tmp->sendBufunSend->sendTime = getCurrentTime1();
			sip_sendseg(sip_conn, tmp->server_nodeID,&tmp->sendBufunSend->seg);
			tmp->sendBufunSend = tmp->sendBufunSend->next;
			tmp->unAck_segNum++;
			pthread_mutex_unlock( ((client_tcb_t*)clienttcb)->send_bufMutex );
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

