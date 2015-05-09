//�ļ���: client/stcp_client.c
//
//����: ����ļ�����STCP�ͻ��˽ӿ�ʵ�� 
//
//��������: 2013��1��

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

//����tcbtableΪȫ�ֱ���
client_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//������SIP���̵�TCP����Ϊȫ�ֱ���
int sip_conn;

/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
void stcp_client_init(int conn) 
{
 	sip_conn = conn;
	pthread_t phd;
	pthread_create(&phd,NULL,seghandler,(void*)0);
}
int getCurrentTime1();
void* sendThread(void* clienttcb);
// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
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

// ��������������ӷ�����. �����׽���ID, �������ڵ�ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������ڵ�ID�ͷ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
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
		//��ʱ��
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

// �������ݸ�STCP������. �������ʹ���׽���ID�ҵ�TCB���е���Ŀ.
// Ȼ����ʹ���ṩ�����ݴ���segBuf, �������ӵ����ͻ�����������.
// ������ͻ������ڲ�������֮ǰΪ��, һ����Ϊsendbuf_timer���߳̾ͻ�����.
// ÿ��SENDBUF_ROLLING_INTERVALʱ���ѯ���ͻ������Լ���Ƿ��г�ʱ�¼�����. 
// ��������ڳɹ�ʱ����1�����򷵻�-1. 
// stcp_client_send��һ����������������.
// ��Ϊ�û����ݱ���ƬΪ�̶���С��STCP��, ����һ��stcp_client_send���ÿ��ܻ�������segBuf
// ����ӵ����ͻ�����������. ������óɹ�, ���ݾͱ�����TCB���ͻ�����������, ���ݻ������ڵ����,
// ���ݿ��ܱ����䵽������, ���ڶ����еȴ�����.
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
			//����sendBuf_timer
			pthread_t phd;
			pthread_create(&phd,NULL,sendBuf_timer,(void*)tmp);
			//����sendThread
			pthread_t phd2;
			pthread_create(&phd2,NULL,sendThread,(void*)tmp);
		}
		else{
			tmp->sendBufTail->next = p;
			tmp->sendBufTail = p;
			if(tmp->sendBufunSend == NULL){//���������˵��֮ǰû��δ���͵�����
				tmp->sendBufunSend = p;
			}
		}
		pthread_mutex_unlock(tmp->send_bufMutex);
	}
	else{//���MAX_SEG_LEN�����ڵ�Ҫ �ݹ�طֳ������ֽ��з���
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

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
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
		//��ʱ��
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

// �ر�STCP�ͻ�
//
// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
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

// �������ε��߳�
//
// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
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
				//�ͷſռ�
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


// ����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
// ���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �ͷ���һ�γ�ʱ�¼�.
// ����ʱ�¼�����ʱ, ���·��������ѷ��͵�δ��ȷ�϶�. �����ͻ�����Ϊ��ʱ, ����߳̽���ֹ.
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
	//���͵�GBN_WINDOWΪֹ
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

