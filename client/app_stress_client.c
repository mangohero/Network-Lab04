//�ļ���: client/app_stress_client.c
//
//����: ����ѹ�����԰汾�Ŀͻ��˳������. �ͻ����������ӵ�����SIP����, Ȼ��������stcp_client_init()��ʼ��STCP�ͻ���. 
//��ͨ������stcp_client_sock()��stcp_client_connect()�����׽��ֲ����ӵ�������.
//Ȼ������ȡ�ļ�sendthis.txt�е��ı�����, ���ļ��ĳ��Ⱥ��ļ����ݷ��͸�������. ����һ��ʱ���, �ͻ��˵���stcp_client_disconnect()�Ͽ���������������.
//���,�ͻ��˵���stcp_client_close()�ر��׽��ֲ��Ͽ�������SIP���̵�����.

//��������: 2013��1��

//����: ��

//���: STCP�ͻ���״̬

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/constants.h"
#include "../topology/topology.h"
#include "stcp_client.h"

//����һ������, ʹ�ÿͻ��˶˿ں�87�ͷ������˿ں�88. 
#define CLIENTPORT1 87
#define SERVERPORT1 88

//�����ӵ�SIP���̺�, �ȴ�1��, �÷���������.
#define STARTDELAY 1
//�ڷ����ļ���, �ȴ�5��, Ȼ��ر�����.
#define WAITTIME 500

//����������ӵ�����SIP���̵Ķ˿�SIP_PORT. ���TCP����ʧ��, ����-1. ���ӳɹ�, ����TCP�׽���������, STCP��ʹ�ø����������Ͷ�.
int connectToSIP() {

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
	servaddr.sin_port = htons(SIP_PORT);

	//Connection of the client to the socket
	if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))<0){
		perror("Problem in connecting to the server");
		return -1;
	}
	return sockfd;
}

//��������Ͽ�������SIP���̵�TCP����. 
void disconnectToSIP(int sip_conn) {

	//����Ҫ��д����Ĵ���.
	close(sip_conn);
}

int main() {
	//���ڶ����ʵ����������
	srand(time(NULL));

	//���ӵ�SIP���̲����TCP�׽���������	
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("fail to connect to the local SIP process\n");
		exit(1);
	}

	//��ʼ��stcp�ͻ���
	stcp_client_init(sip_conn);
	sleep(STARTDELAY);

	char hostname[50];
	printf("Enter server name to connect:");
	scanf("%s",hostname);
	int server_nodeID = topology_getNodeIDfromname(hostname);
	if(server_nodeID == -1) {
		printf("host name error!\n");
		exit(1);
	} else {
		printf("connecting to node %d\n",server_nodeID);
	}

	//�ڶ˿�87�ϴ���STCP�ͻ����׽���, �����ӵ�STCP�������˿�88.
	int sockfd = stcp_client_sock(CLIENTPORT1);
	if(sockfd<0) {
		printf("fail to create stcp client sock");
		exit(1);
	}
	if(stcp_client_connect(sockfd,server_nodeID,SERVERPORT1)<0) {
		printf("fail to connect to stcp server\n");
		exit(1);
	}
	printf("client connected to server, client port:%d, server port %d\n",CLIENTPORT1,SERVERPORT1);
	
	//��ȡsendthis.txt�ļ�����, ��������������ȡ�ļ��е�����
	FILE *f;
	f = fopen("Oliver Twist.txt","r");
	assert(f!=NULL);
	fseek(f,0,SEEK_END);
	int fileLen = ftell(f);
	fseek(f,0,SEEK_SET);
	char *buffer = (char*)malloc(fileLen);
	fread(buffer,fileLen,1,f);
	fclose(f);
	//���ȷ����ļ�����, Ȼ���������ļ�.
	stcp_client_send(sockfd,&fileLen,sizeof(int));
    stcp_client_send(sockfd, buffer, fileLen);
	free(buffer);
	//�ȴ�һ��ʱ��, Ȼ��ر�����.
	sleep(WAITTIME);

	if(stcp_client_disconnect(sockfd)<0) {
		printf("fail to disconnect from stcp server\n");
		exit(1);
	}
	if(stcp_client_close(sockfd)<0) {
		printf("fail to close stcp client\n");
		exit(1);
	}
	
	//�Ͽ���SIP����֮�������
	disconnectToSIP(sip_conn);
}
