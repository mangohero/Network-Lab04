#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "../common/constants.h"
#include "../server/stcp_server.h"



#define CLIENTPORT1 80
#define CLIENTPORT2 81
#define CLIENTPORT3 82

#define SERVERPORT1 88
#define SERVERPORT2 89
#define SERVERPORT3 90


//这个函数连接到本地SIP进程的端口SIP_PORT. 如果TCP连接失败, 返回-1. 连接成功, 返回TCP套接字描述符, STCP将使用该描述符发送段.
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

//这个函数断开到本地SIP进程的TCP连接. 
void disconnectToSIP(int sip_conn) {

	close(sip_conn);
	
}


#define YY_MSG_HEADER_LENGTH 45
#define MAX_CLIENT 50 /*maximum number of the client*/

/*definition of struct YY_MSG,the client and the server exchange data through this struct!*/
#pragma pack(1)
struct YY_MSG
{
	char protocol_name[4];
	unsigned char service;
	char send_usr[20];
	char rec_usr[20];
	char msg_content[1024];
};

/*definition of client's struct*/
struct client
{
	/*client's usrname*/
	char usrname[20];
	/*The socket for the client*/
	int sockfd;
	/*if the client is online,set online flag true,or not online,set false*/
	bool online;
	/*the flag for the data space ,if the data space is used now,set used=true*/
	bool used;
	/*the thread for the client*/
	pthread_t pthd;
};

/*data space to store the client's data*/
struct client clients[MAX_CLIENT];
/*mutex for online_users*/
pthread_mutex_t count_mutex;
/*count for online users*/
int online_users = 0;


/*Get a free client space,return it's index*/
int getAClient()
{
	int i;
	for(i=0;i<MAX_CLIENT;i++)
		if(clients[i].used==false)
			return i;
	return -1;
}

/*get the user's index in the matrix clients*/
int user_online(char *usr)
{
	int i;
	for(i=0;i<MAX_CLIENT;i++)
		if(clients[i].used ==true && clients[i].online == true && strncmp(clients[i].usrname,usr,20)==0)
			return i;
	return -1;
}

void *handle_client(void *);
int main(int argc,char **argv)
{
	
	//用于丢包率的随机数种子
	srand(time(NULL));

	//连接到SIP进程并获得TCP套接字描述符
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}

	printf("Collected to sip!\n");
	//初始化STCP服务器
	stcp_server_init(sip_conn);

	int sockfd;
	int i=1;
	while(i<=3){
		//accept a connection
		if(i==1) sockfd= stcp_server_sock(SERVERPORT1);
		else if(i==2)sockfd= stcp_server_sock(SERVERPORT2);
		else if(i==3)sockfd= stcp_server_sock(SERVERPORT3);
		stcp_server_accept(sockfd);
		int k=getAClient();
		while(k==-1){
			 k=getAClient();
				sleep(1);
		}
		clients[k].sockfd=sockfd;
		clients[k].used=true;
		/*create a thread for the client !*/
		if(pthread_create(&clients[k].pthd, NULL, handle_client, (void *)&clients[k])!= 0)
			perror("Thread creation problem");
		printf("Create a thread for the client,receiving request.....\n");
		i++;
	}
	while(1)
		sleep(100);
	pthread_mutex_destroy(&count_mutex);
	pthread_exit(NULL);
}


void *handle_client(void *clientarg)
{
	/*msg_buf is used to store the data receive from the client and the data to send to the client!*/
	struct YY_MSG msg_buf;
	/*get the client,cli point to the client*/
	struct client *cli=(struct client*)clientarg;
	memset(&msg_buf,0,YY_MSG_HEADER_LENGTH+1024);
	int i,k,toclient,len;
	while(stcp_server_recv(cli->sockfd,&len,sizeof(int)) && stcp_server_recv(cli->sockfd,&msg_buf,len))
	{
		if(strncmp("YYMG",msg_buf.protocol_name,4)!=0)
		{
			printf("NOT A YYMG MESSENGER!");
			continue;
		}
		switch(msg_buf.service)
		{
			case 0x01: /*The client log in!*/
				if(user_online(msg_buf.send_usr) != -1)
				{
					msg_buf.service = 0x11;
					stcp_server_send(cli->sockfd,&len,sizeof(int));
					stcp_server_send(cli->sockfd,&msg_buf,len);
				}
				else
				{
					strcpy(cli->usrname,msg_buf.send_usr);
					printf("%s login!   ",cli->usrname);
					printf("%s usr[0]\n",clients[0].usrname);
					pthread_mutex_lock(&count_mutex);
					cli->online=true;
					online_users++;
					printf("online users:%d\n",online_users);
					pthread_mutex_unlock(&count_mutex);
					msg_buf.service = 0x12;
					stcp_server_send(cli->sockfd,&len,sizeof(int));
					stcp_server_send(cli->sockfd,&msg_buf,len);
				}
				break;
			case 0x02: /*The client querys the online users*/
				msg_buf.service = 0x13;
				msg_buf.msg_content[0]=(unsigned char)online_users;
				k=0;
				for(i=0;i<MAX_CLIENT;i++)
					if(clients[i].used == true && clients[i].online == true)
						{
							printf("%s\n",clients[i].usrname);
							strcpy(msg_buf.msg_content+1+20*k,clients[i].usrname);
							k++;
						};
				len =YY_MSG_HEADER_LENGTH+1+20*online_users;
				stcp_server_send(cli->sockfd,&len,sizeof(int));
				stcp_server_send(cli->sockfd,&msg_buf,len);
				break;
			case 0x03:/*The client querys the online state of the user*/
				if(user_online(msg_buf.rec_usr)==-1)
				{
					msg_buf.service = 0x14;
					stcp_server_send(cli->sockfd,&len,sizeof(int));
					stcp_server_send(cli->sockfd,&msg_buf,len);
				}
				else
				{
					msg_buf.service = 0x15;
					stcp_server_send(cli->sockfd,&len,sizeof(int));
					stcp_server_send(cli->sockfd,&msg_buf,len);
				}
				 break;
			case 0x04:/*The client sends messages to someone*/
				toclient=user_online(msg_buf.rec_usr);
				if(toclient ==-1)
				{
					msg_buf.service = 0x16;
					strcpy(msg_buf.send_usr,"System");
					strcpy(msg_buf.msg_content,"Message sends failed!Because the user has loged out!");
					len = YY_MSG_HEADER_LENGTH+strlen(msg_buf.msg_content);
					stcp_server_send(cli->sockfd,&len,sizeof(int));
					stcp_server_send(cli->sockfd,&msg_buf,len);
				}
				else
				{
					msg_buf.service = 0x16;
					stcp_server_send(clients[toclient].sockfd,&len,sizeof(int));
					stcp_server_send(clients[toclient].sockfd,&msg_buf,len);
				}
				break;
			case 0x05:/*The client sends messages to everyone*/
				msg_buf.service = 0x16;
				for(i=0;i<MAX_CLIENT;i++)
					if(clients[i].used == true && clients[i].online == true && clients[i].sockfd!=cli->sockfd)
					{
						stcp_server_send(clients[i].sockfd,&len,sizeof(int));
						stcp_server_send(clients[i].sockfd,&msg_buf,len);
					}
				break;
			default: printf("message error!error code:%02x",msg_buf.service);break;
				
								
		}
	}
	/*if the client is online,when it log out,we should set its online flag false,and set online_users minus 1!*/
	if(cli->online == true)
	{
		printf("%s logout!  ",cli->usrname);
		pthread_mutex_lock(&count_mutex);
		cli->online=false;
		online_users--;
		printf("online users:%d\n",online_users);
		pthread_mutex_unlock(&count_mutex);
	}
	cli->used=false;
	pthread_exit(NULL);
}






















