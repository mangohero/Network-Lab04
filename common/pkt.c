// 文件名: common/pkt.c
// 创建日期: 2013年

#include "pkt.h"
#include "stdio.h"

static int readn(int fd, char *bp, size_t len)
{
	int cnt;
	int rc;
	cnt = len;
	while ( cnt > 0 )
	{
		rc = recv( fd, bp, cnt, 0 );
		if ( rc < 0 )	
			return -1;
		if ( rc == 0 )	
			return len - cnt;
		bp += rc;
		cnt -= rc;
	}
	return len;
}


// son_sendpkt()由SIP进程调用, 其作用是要求SON进程将报文发送到重叠网络中. SON进程和SIP进程通过一个本地TCP连接互连.
// 在son_sendpkt()中, 报文及其下一跳的节点ID被封装进数据结构sendpkt_arg_t, 并通过TCP连接发送给SON进程. 
// 参数son_conn是SIP进程和SON进程之间的TCP连接套接字描述符.
// 当通过SIP进程和SON进程之间的TCP连接发送数据结构sendpkt_arg_t时, 使用'!&'和'!#'作为分隔符, 按照'!& sendpkt_arg_t结构 !#'的顺序发送.
// 如果发送成功, 返回1, 否则返回-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
	char start[2] = "!&",end[2]="!#";
 	if(send(son_conn,&start,2,0)!=-1 && send(son_conn,(char *)&nextNodeID,sizeof(int),0)!=-1 && send(son_conn,(char *)pkt,pkt->header.length,0)!=-1&&send(son_conn,&end,2,0)!=-1)
 		return 1;
	return -1;
}

// son_recvpkt()函数由SIP进程调用, 其作用是接收来自SON进程的报文. 
// 参数son_conn是SIP进程和SON进程之间TCP连接的套接字描述符. 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.

#define PKTSTART1 1
#define PKTSTART2 2
#define PKTSTOP1 3
#define PKTSTOP2 4
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
 	int state = PKTSTART1;
	char buf[1];
	int siplen=0;
	while(recv(son_conn,&buf,1,0) >= 1)
	{
		switch(buf[0])
		{
			case '!':if(state==PKTSTART1) state =PKTSTART2;
				 else if(state==PKTSTOP1) state = PKTSTOP2;
				 break;
			case '&':if(state==PKTSTART2)
				 {
					if(readn(son_conn,(char *)pkt,sizeof(sip_hdr_t))==-1) return -1;
				 	siplen = pkt->header.length-sizeof(sip_hdr_t);
				 	if(siplen!=0)
				 		if(readn(son_conn,(char *)&pkt->data,siplen)==-1) return -1;
				 	state = PKTSTOP1;
				 }
				 else if(state == PKTSTOP2) state = PKTSTOP1;
				 break;
			case '#':if(state == PKTSTOP2) 
					return 1;
				 else if(state == PKTSTART2) state = PKTSTART1;
				 break;
			default: if(state == PKTSTART2) state = PKTSTART1;
				else if(state == PKTSTOP2) state = PKTSTOP1 ;break;
		}
	}
	return -1;
}

// 这个函数由SON进程调用, 其作用是接收数据结构sendpkt_arg_t.
// 报文和下一跳的节点ID被封装进sendpkt_arg_t结构.
// 参数sip_conn是在SIP进程和SON进程之间的TCP连接的套接字描述符. 
// sendpkt_arg_t结构通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收sendpkt_arg_t结构, 返回1, 否则返回-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
 	int state = PKTSTART1;
	char buf[1];
	int siplen=0;
	while(recv(sip_conn,&buf,1,0) >= 1)
	{
		switch(buf[0])
		{
			case '!':if(state==PKTSTART1) state =PKTSTART2;
				 else if(state==PKTSTOP1) state = PKTSTOP2;
				 break;
			case '&':if(state==PKTSTART2)
				 {
					if(readn(sip_conn,(char *)nextNode,sizeof(int))==-1)return -1;
					if(readn(sip_conn,(char *)pkt,sizeof(sip_hdr_t))==-1) return -1;
				 	siplen = pkt->header.length-sizeof(sip_hdr_t);
				 	if(siplen!=0)
				 		if(readn(sip_conn,(char *)&pkt->data,siplen)==-1) return -1;
				 	state = PKTSTOP1;
				 }
				 else if(state == PKTSTOP2) state = PKTSTOP1;
				 break;
			case '#':if(state == PKTSTOP2) 
					return 1;
				 else if(state == PKTSTART2) state = PKTSTART1;
				 break;
			default: if(state == PKTSTART2) state = PKTSTART1;
				else if(state == PKTSTOP2) state = PKTSTOP1 ;break;
		}
	}
	return -1;
}

// forwardpktToSIP()函数是在SON进程接收到来自重叠网络中其邻居的报文后被调用的. 
// SON进程调用这个函数将报文转发给SIP进程. 
// 参数sip_conn是SIP进程和SON进程之间的TCP连接的套接字描述符. 
// 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn)
{
	char start[2] = "!&",end[2]="!#";
 	if(send(sip_conn,&start,2,0)!=-1 &&send(sip_conn,(char *)pkt,pkt->header.length,0)!=-1&&send(sip_conn,&end,2,0)!=-1)
 		return 1;
	return -1;
}

// sendpkt()函数由SON进程调用, 其作用是将接收自SIP进程的报文发送给下一跳.
// 参数conn是到下一跳节点的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居节点之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.

int sendpkt(sip_pkt_t* pkt, int conn)
{
	char start[2] = "!&",end[2]="!#";
 	if(send(conn,&start,2,0)!=-1 &&send(conn,(char *)pkt,pkt->header.length,0)!=-1&&send(conn,&end,2,0)!=-1)
 		return 1;
	return -1;
}

// recvpkt()函数由SON进程调用, 其作用是接收来自重叠网络中其邻居的报文.
// 参数conn是到其邻居的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.

int recvpkt(sip_pkt_t* pkt, int conn)
{
 	int state = PKTSTART1;
	char buf[1];
	int siplen=0;
	while(recv(conn,&buf,1,0) >= 1)
	{
		switch(buf[0])
		{
			case '!':if(state==PKTSTART1) state =PKTSTART2;
				 else if(state==PKTSTOP1) state = PKTSTOP2;
				 break;
			case '&':if(state==PKTSTART2)
				 {
					if(readn(conn,(char *)pkt,sizeof(sip_hdr_t))==-1) return -1;
				 	siplen = pkt->header.length-sizeof(sip_hdr_t);
				 	if(siplen!=0)
				 		if(readn(conn,(char *)&pkt->data,siplen)==-1) return -1;
				 	state = PKTSTOP1;
				 }
				 else if(state == PKTSTOP2) state = PKTSTOP1;
				 break;
			case '#':if(state == PKTSTOP2) 
					return 1;
				 else if(state == PKTSTART2) state = PKTSTART1;
				 break;
			default: if(state == PKTSTART2) state = PKTSTART1;
				else if(state == PKTSTOP2) state = PKTSTOP1 ;break;
		}
	}
	return -1;
}
