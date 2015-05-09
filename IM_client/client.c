#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../common/constants.h"
#include "../topology/topology.h"
#include "../client/stcp_client.h"

#define CLIENTPORT1 80
#define CLIENTPORT2 81
#define CLIENTPORT3 82

#define SERVERPORT1 88
#define SERVERPORT2 89
#define SERVERPORT3 90

//在连接到SIP进程后, 等待1秒, 让服务器启动.
#define STARTDELAY 1

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

	//你需要编写这里的代码.
	close(sip_conn);
}



#define YY_MSG_HEADER_LENGTH 45
#define MAX_MSG_NUM 100 /*maximum of the messages stored!*/
#define SERV_PORT 6666  /*server's tcp port*/
#define SERV_ADDR "192.168.93.131" /*server's ip address*/


/*The client has two thread,one to receive messages,another to carry out orders from the user!*/


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


int sockfd;
struct YY_MSG send_msg,rec_msg;
struct YY_MSG history_msgs[MAX_MSG_NUM]; /*store the history messages received from other clients*/
int first_msg=0;  /*the oldest history message's index in the matrix of history_msgs*/
int msg_num=0;	  /*history messages' quantity number*/
char buffer[1024];
char blacklist[50][20]; /*store the users added to the blacklist*/
int blacklistnum=0;	/*number of users in blacklsit*/
char usrname[20];	/*the client's user's user name*/
char wait=0x00;	   /*the variable is used for handle_msg thread to wake up handle_main thread*/
pthread_mutex_t wait_mutex; /*mutex for wait*/
pthread_cond_t wait_cond;   /*cond for handle_msg thread to wake up handle_main_thread*/

/*add the user in buffer to blacklist*/
void add_to_blacklist()
{
	if(blacklistnum>=10)
		printf("The blacklist's length arrives to the max num!No space");
	else
	{
		strncpy(blacklist[blacklistnum++],buffer,20);
		printf("%s has been added to blacklist!\n",buffer);
	}
}
/*remove the user in buffer from blacklist*/
void delete_from_blacklist()
{
	int i,j=0;
	for(i=0;i<blacklistnum;i++)
	{
		if(strncmp(blacklist[i],buffer,20)==0)
		{
			for(j=i;j<blacklistnum;j++)
				strncpy(blacklist[j],blacklist[j+1],20);
			blacklistnum--;
			printf("%s has been removed from blacklist!\n",buffer);
			return;
		}
	}
	printf("%s is not exist in the blacklist!\n",buffer);
}
/*judge the user is in the blacklist or not*/
bool in_blacklist(char *p)
{
	int i;
	for(i=0;i<blacklistnum;i++)
		if(strncmp(blacklist[i],p,20)==0)
			return true;
	return false;
}
/*thread function which's job is to handle the messages reveived from the server*/
void *handle_msg()
{
	struct YY_MSG rec_buf;
	memset(&rec_buf,0,YY_MSG_HEADER_LENGTH+1024);
	int len;
	while( stcp_client_recv(sockfd,&len,sizeof(int)) && stcp_client_recv(sockfd,&rec_buf,len) )
	{
		if(strncmp(rec_buf.protocol_name,"YYMG",4)!=0)
			continue;
		if(rec_buf.service == 0x16)
		{	/*If the message is not control message,then save it to the history messages!Not include messages sended by the users in the blacklist*/
			if(in_blacklist(rec_buf.send_usr))
				continue;
			int i=0;
			char *p=(char *)&history_msgs[(first_msg+msg_num)%MAX_MSG_NUM];
			char *q=(char *)&rec_buf;
			for(i;i<YY_MSG_HEADER_LENGTH+strlen(rec_buf.msg_content);i++)
				p[i]=q[i];
			if(msg_num==MAX_MSG_NUM)
				first_msg++;
			if(msg_num<MAX_MSG_NUM);
				msg_num++;
			printf("%s(to %s):%s\n",rec_buf.send_usr,rec_buf.rec_usr,rec_buf.msg_content);
			memset(&rec_buf.msg_content,0,1024);
		}
		else	/*If the message is control message,then save it to rec_msg,and set the wait rec_msg.service,then wake up the handle_main thread*/
  		{	int i=0;
			char *p=(char *)&rec_msg;
			char *q=(char *)&rec_buf;
			for(i;i<YY_MSG_HEADER_LENGTH+1024;i++)
			p[i]=q[i];
			pthread_mutex_lock(&wait_mutex);
			wait=rec_msg.service;
			pthread_cond_signal(&wait_cond);
			pthread_mutex_unlock(&wait_mutex);
			memset(&rec_buf.msg_content,0,1024);
		}
	}
	pthread_exit(NULL);
}
/*thread function which's job is to carry out the order from the user*/
void *handle_main()
{
	system("clear");
	printf("Welcome to The Chatting program!User name:%s\n1.query the online users\n2.send message to someone\n3.send message to everyone\n4.View messages\n5.add someone to blacklist\n6.remove someone from blacklist\n7.View blacklist!\n(c)cls,(#)exit\n===================================================\n",usrname);
				
	while(true)
	{
		gets(buffer);
		if(strlen(buffer)!=1)
		{
			printf("input error!\n");
		}
		else if(buffer[0]=='1'){ /*query the online users*/
			send_msg.service = 0X02;
			int len=YY_MSG_HEADER_LENGTH;
			stcp_client_send(sockfd,&len,sizeof(int));
			stcp_client_send(sockfd,&send_msg,len);			
			pthread_mutex_lock(&wait_mutex);	
			while(wait!=0x13)
				pthread_cond_wait(&wait_cond,&wait_mutex);
			wait=0x00;
			pthread_mutex_unlock(&wait_mutex);
			unsigned char online_users = (unsigned char)rec_msg.msg_content[0];
			char *users=rec_msg.msg_content+1;
			int i;
			printf("Online users:\n");
			for(i=0;i<online_users;i++)
				printf("User name: %s\n",users+i*20);
		}
		else if(buffer[0]=='2') 
		{	/*send messages to someone!*/
			bool success=false;
			send_msg.service = 0x03;
			printf("Please enter the receive user's name:");
			while(!success)
			{
				gets(buffer);
				if(strlen(buffer)>20)
				{
					printf("The user name inputed is too long!input again:\n");
					continue;
				}
				else
				{
					bool illegal=false;
					int i=0;
					for(i;i<strlen(buffer);i++)
					{
						if(!(('a'<=buffer[i]&& 'z'>=buffer[i]) ||('A'<=buffer[i] && 'Z' >=buffer[i])||('0'<=buffer[i] && '9'>=buffer[i])) )
						{
							printf("The input is illegal!input again:\n");
							illegal=true;
							break;
						}
					}
					if(illegal)
						continue;
				}
				strcpy(send_msg.rec_usr,buffer);
				/*query whether the user is online*/
				int len=YY_MSG_HEADER_LENGTH;
				stcp_client_send(sockfd,&len,sizeof(int));
				stcp_client_send(sockfd,&send_msg,len);
				pthread_mutex_lock(&wait_mutex);
				while(wait!=0x14 && wait!=0x15)
				pthread_cond_wait(&wait_cond,&wait_mutex);
				wait=0x00;
				pthread_mutex_unlock(&wait_mutex);
				if(rec_msg.service == 0x15){
					success=true;
					break;
				}
				else{
					printf("The user is not exist or not online!\n");
					break;
				}
			}
			if(success){
				printf("Iuput the message content:\n");
				gets(send_msg.msg_content);
				send_msg.service = 0x04;
				int len=YY_MSG_HEADER_LENGTH+strlen(send_msg.msg_content);
				stcp_client_send(sockfd,&len,sizeof(int));
				stcp_client_send(sockfd,&send_msg,len);
				printf("The message has been sent out!\n");
			}
		}
		else if(buffer[0]=='3')		
		{	/*send messages to everyone!*/
			send_msg.service = 0x05;
			printf("Iuput the message content:\n");
			gets(send_msg.msg_content);
			strcpy(send_msg.rec_usr,"everyone");
			int len = YY_MSG_HEADER_LENGTH+strlen(send_msg.msg_content);
			stcp_client_send(sockfd,&len,sizeof(int));
			stcp_client_send(sockfd,&send_msg,len);
			printf("The message has been sent out!\n");
		}
		else if(buffer[0]=='4')
		{	/*View the history messages!*/
			if(msg_num==0)
				printf("No history messages!\n");
			else {
				printf("The history messages :\n");
				int i;
				for(i=0;i<msg_num;i++)
				{
					struct YY_MSG *msg=&history_msgs[(i+first_msg)%MAX_MSG_NUM];
					printf("%s(to %s):%s\n",msg->send_usr,msg->rec_usr,msg->msg_content);
				}
			}
		}
		else if(buffer[0]=='5')
		{	/*add someone to blacklist*/
			bool success=false;
			send_msg.service = 0x03;
			printf("Please input the user's name:");
			while(!success)
			{
				gets(buffer);
				if(strlen(buffer)>20)
				{
					printf("The user name inputed is too long!input again:\n");
					continue;
				}
				else
				{
					bool illegal=false;
					int i=0;
					for(i;i<strlen(buffer);i++)
					{
						if(!(('a'<=buffer[i]&& 'z'>=buffer[i]) ||('A'<=buffer[i] && 'Z' >=buffer[i])||('0'<=buffer[i] && '9'>=buffer[i])) )
						{
							printf("The input is illegal!input again:\n");
							illegal=true;
							continue;
						}
					}
					if(illegal)
						continue;
				}
				strcpy(send_msg.rec_usr,buffer);
				int len = YY_MSG_HEADER_LENGTH;
				stcp_client_send(sockfd,&len,sizeof(int));
				stcp_client_send(sockfd,&send_msg,len);
				pthread_mutex_lock(&wait_mutex);
				while(wait!=0x14 && wait!=0x15)
				pthread_cond_wait(&wait_cond,&wait_mutex);
				wait=0x00;
				pthread_mutex_unlock(&wait_mutex);
				if(rec_msg.service == 0x15){
					success=true;
					break;
				}
				else{
					printf("The user is not exist or not online!\n");
					break;
				}
			}
			add_to_blacklist();
		}
		else if(buffer[0]=='6'){
			/*remove someone from blacklist*/
			printf("Please input the user's name:");
			gets(buffer);
			delete_from_blacklist();
		}
		else if(buffer[0]=='7')
		{
			/*View blacklist*/
			if(blacklistnum==0) printf("NO ONE!\n");
			else {
				printf("blacklist:\n");
				int i=0;
				for(i;i<blacklistnum;i++)
					printf("%s",blacklist[i]);
			}
		}
		else if(buffer[0]=='#'){
			exit(0);
		}
		else if(buffer[0]=='c'){
			system("clear");
			printf("Welcome to The Chatting program!User name:%s\n1.query the online users\n2.send message to someone\n3.send message to everyone\n4.View messages\n5.add someone to blacklist\n6.remove someone from blacklist\n7.View blacklist!\n(c)cls,(#)exit\n===================================================\n",usrname);
		}
		else printf("input error!\n");
	}
	pthread_exit(NULL);
}
int main(int argc, char **argv)
{

	//用于丢包率的随机数种子
	srand(time(NULL));

	//连接到SIP进程并获得TCP套接字描述符	
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("fail to connect to the local SIP process\n");
		exit(1);
	}

	//初始化stcp客户端
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
	int number;
	printf("Enter client number to connect:");
	scanf("%d",&number);
	if(number ==1 ){
		sockfd = stcp_client_sock(CLIENTPORT1);
		if(sockfd<0) {
			printf("fail to create stcp client sock");
			exit(1);
		}
		if(stcp_client_connect(sockfd,server_nodeID,SERVERPORT1)<0) {
			printf("fail to connect to stcp server\n");
			exit(1);
		}
		printf("client connected to server, client port:%d, server port %d\n",CLIENTPORT1,SERVERPORT1);
	}
	else if(number ==2 ){
		sockfd = stcp_client_sock(CLIENTPORT2);
		if(sockfd<0) {
			printf("fail to create stcp client sock");
			exit(1);
		}
		if(stcp_client_connect(sockfd,server_nodeID,SERVERPORT2)<0) {
			printf("fail to connect to stcp server\n");
			exit(1);
		}
		printf("client connected to server, client port:%d, server port %d\n",CLIENTPORT2,SERVERPORT2);
	}
	else if(number ==3 ){
		sockfd = stcp_client_sock(CLIENTPORT3);
		if(sockfd<0) {
			printf("fail to create stcp client sock");
			exit(1);
		}
		if(stcp_client_connect(sockfd,server_nodeID,SERVERPORT3)<0) {
			printf("fail to connect to stcp server\n");
			exit(1);
		}
		printf("client connected to server, client port:%d, server port %d\n",CLIENTPORT3,SERVERPORT3);
	}
	else
	{	
		printf("client number error!\n");
		exit(1);	
	}
	pthread_t msgthd,mainthd;
	pthread_attr_t attr;

	pthread_mutex_init(&wait_mutex,NULL);
	pthread_cond_init(&wait_cond,NULL);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
	memset(&send_msg,0,YY_MSG_HEADER_LENGTH+1024);
	memset(&rec_msg,0,YY_MSG_HEADER_LENGTH+1024);
	send_msg.protocol_name[0]='Y';
	send_msg.protocol_name[1]='Y';
	send_msg.protocol_name[2]='M';
	send_msg.protocol_name[3]='G';
	bool login=false;
	system("clear");
	printf("Welcome to NJUCS Instant Messenger Demo Program!\nPlease input Your user name to login(The name's length shouled be less than 20 letters;The name only contains letters and digits!)\n(c)cls,(#)exit\n");
	send_msg.service = 0X01;
	while(!login)
	{		
		//main menu
		char a;
		scanf("%c",&a);
		gets(buffer);
		if(strcmp(buffer,"#")==0)
			break;
		else if(strcmp(buffer,"c")==0)
		{
			system("clear");
			printf("Welcome to NJUCS Instant Messenger Demo Program!\nPlease input Your user name to login(The name's length shouled be less than 20 letters;The name only contains letters and digits!)\n(c)cls,(#)exit\n");
			continue;
		}
		else if(strlen(buffer)>20)
		{
			printf("The inputed name is too long!input again:\n");
			continue;
		}
		else
		{
			bool illegal=false;
			int i=0;
			for(i;i<strlen(buffer);i++)
			{
				if(!(('a'<=buffer[i]&& 'z'>=buffer[i]) ||('A'<=buffer[i] && 'Z' >=buffer[i])||('0'<=buffer[i] && '9'>=buffer[i])) )
				{
					printf("The input is illegal!input again:\n");
					illegal=true;
					break;
				}
			}
			if(illegal)
				continue;
		}
		strcpy(send_msg.send_usr,buffer);
		/*send the message to log in!*/
		int len = YY_MSG_HEADER_LENGTH;
		stcp_client_send(sockfd,&len,sizeof(int));
		stcp_client_send(sockfd,&send_msg,len);
		stcp_client_recv(sockfd,&len,sizeof(int));
		stcp_client_recv(sockfd,&rec_msg,len);
		switch(rec_msg.service)
		{
			case 0x11:/*the user is aleady exist*/
				printf("Sorry, The user %s already exists!input another:\n",rec_msg.send_usr);break;
			case 0x12:/*log in success!*/
				strncpy(usrname,buffer,20);
				if(pthread_create(&mainthd, &attr, handle_main, NULL )!= 0)
				{
					perror("Thread creation problem");
					exit(-1);
				}
				if(pthread_create(&msgthd, &attr, handle_msg, NULL)!= 0)
				{
					perror("Thread creation problem");
					exit(-1);
				}
				pthread_join(mainthd,NULL);
				pthread_join(msgthd,NULL);
				login=true;
				break;
			default: printf("message error!error code:0x%02x",rec_msg.service);break;
		}
	}
	pthread_attr_destroy(&attr);
	pthread_mutex_destroy(&wait_mutex);
	pthread_cond_destroy(&wait_cond);
	pthread_exit(NULL);
	disconnectToSIP(sip_conn);
}















