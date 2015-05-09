//
// 文件名: seg.c

// 描述: 这个文件包含用于发送和接收STCP段的接口sip_sendseg() and sip_rcvseg(), 及其支持函数的实现. 
//
// 创建日期: 2013年
//

#include "seg.h"
#include "stdio.h"

//
//
//  用于客户端和服务器的SIP API 
//  =======================================
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: sip_sendseg()和sip_recvseg()是由网络层提供的服务, 即SIP提供给STCP.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回1,否则返回-1.
int sip_sendseg(int sip_conn,  int dest_nodeID, seg_t* segPtr)
{
	char start[2] = "!&",end[2]="!#";
 	if(send(sip_conn,&start,2,0)!=-1 && send(sip_conn,(char *)&dest_nodeID,sizeof(int),0)!=-1 && send(sip_conn,(char *)segPtr,segPtr->header.length,0)!=-1&&send(sip_conn,&end,2,0)!=-1)
 		return 1;
	return -1;
}

// 通过重叠网络(在本实验中，是一个TCP连接)接收STCP段. 我们建议你使用recv()一次接收一个字节.
// 你需要查找"!&", 然后是seg_t, 最后是"!#". 这实际上需要你实现一个搜索的FSM, 可以考虑使用如下所示的FSM.
// SEGSTART1 -- 起点 
// SEGSTART2 -- 接收到'!', 期待'&' 
// SEGRECV -- 接收到'&', 开始接收数据
// SEGSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 这里的假设是"!&"和"!#"不会出现在段的数据部分(虽然相当受限, 但实现会简单很多).
// 你应该以字符的方式一次读取一个字节, 将数据部分拷贝到缓冲区中返回给调用者.
//
// 注意: 还有一种处理方式可以允许"!&"和"!#"出现在段首部或段的数据部分. 具体处理方式是首先确保读取到!&，然后
// 直接读取定长的STCP段首部, 不考虑其中的特殊字符, 然后按照首部中的长度读取段数据, 最后确保以!#结尾.
//
// 注意: 在你剖析了一个STCP段之后,  你需要调用seglost()来模拟网络中数据包的丢失. 
// 在sip_recvseg()的下面是seglost(seg_t* segment)的代码.
//
// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 
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

#define SEGSTART1 1
#define SEGSTART2 2
#define SEGSTOP1 3
#define SEGSTOP2 4

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int sip_recvseg(int sip_conn,int* src_nodeID, seg_t* segPtr)
{
	int state = SEGSTART1;
	char buf[1];
	int seglen=0;
	while(recv(sip_conn,&buf,1,0) >= 1)
	{
		switch(buf[0])
		{
			case '!':if(state==SEGSTART1) state =SEGSTART2;
				 else if(state==SEGSTOP1) state = SEGSTOP2;
				 break;
			case '&':if(state==SEGSTART2)
				 {
					if(readn(sip_conn,(char *)src_nodeID,sizeof(int))==-1) return -1;
					if(readn(sip_conn,(char *)segPtr,sizeof(stcp_hdr_t))==-1) return -1;
				 	seglen = segPtr->header.length-sizeof(stcp_hdr_t);
				 	if(seglen!=0)
				 		if(readn(sip_conn,(char *)&segPtr->data,seglen)==-1) return -1;
				 	state = SEGSTOP1;
				 }
				 else if(state == SEGSTOP2) state = SEGSTOP1;
				 break;
			case '#':if(state == SEGSTOP2) 
					if(seglost(segPtr)==1 ) return sip_recvseg(sip_conn,src_nodeID,segPtr);
					else if(checkchecksum(segPtr)!=1) return sip_recvseg(sip_conn,src_nodeID,segPtr);
					else return 1;
				 else if(state == SEGSTART2) state = SEGSTART1;
				 break;
			default: if(state == SEGSTART2) state = SEGSTART1;
				else if(state == SEGSTOP2) state = SEGSTOP1 ;break;
		}
	}
	return -1;
}


//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
	int state = SEGSTART1;
	char buf[1];
	int seglen=0;
	while(recv(stcp_conn,&buf,1,0) >= 1)
	{
		switch(buf[0])
		{
			case '!':if(state==SEGSTART1) state =SEGSTART2;
				 else if(state==SEGSTOP1) state = SEGSTOP2;
				 break;
			case '&':if(state==SEGSTART2)
				 {
					if(readn(stcp_conn,(char *)dest_nodeID,sizeof(int))==-1) return -1;
					if(readn(stcp_conn,(char *)segPtr,sizeof(stcp_hdr_t))==-1) return -1;
				 	seglen = segPtr->header.length-sizeof(stcp_hdr_t);
				 	if(seglen!=0)
				 		if(readn(stcp_conn,(char *)&segPtr->data,seglen)==-1) return -1;
				 	state = SEGSTOP1;
				 }
				 else if(state == SEGSTOP2) state = SEGSTOP1;
				 break;
			case '#':if(state == SEGSTOP2) 
					 return 1;
				 else if(state == SEGSTART2) state = SEGSTART1;
				 break;
			default: if(state == SEGSTART2) state = SEGSTART1;
				else if(state == SEGSTOP2) state = SEGSTOP1 ;break;
		}
	}
	return -1;
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
 	char start[2] = "!&",end[2]="!#";
 	if(send(stcp_conn,&start,2,0)!=-1 && send(stcp_conn,(char *)&src_nodeID,sizeof(int),0)!=-1 && send(stcp_conn,(char *)segPtr,segPtr->header.length,0)!=-1&&send(stcp_conn,&end,2,0)!=-1)
 		return 1;
	return -1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.

int seglost(seg_t* segPtr) {
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		//50%可能性丢失段
		if(rand()%2==0) {
			printf("seg lost!!!\n");
     			 return 1;
		}
		//50%可能性是错误的校验和
		else {
			//获取数据长度
			printf("seg error!\n");
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
			//获取要反转的随机位
			int errorbit = rand()%(len*8);
			//反转该比特
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			return 0;
		}
	}
	return 0;
}



//这个函数计算指定段的校验和.
//校验和覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
	int size=segment->header.length;
	unsigned short* p=(unsigned short*)segment;
	unsigned int sum=0;
	while(size>1)
	{
		sum+=*p++;
		size-=2;

	}
	if(size)
		sum+=*(unsigned char*)p;
	sum=(sum>>16)+(sum&0xffff);
	sum+=(sum>>16);
  	return ~sum;
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1
int checkchecksum(seg_t* segment)
{

	int size=segment->header.length;
	unsigned short* p=(unsigned short*)segment;
	unsigned int sum=0;
	while(size>1)
	{
		sum+=*p++;
		size-=2;
	}
	if(size)
		sum+=*(unsigned char*)p;
	sum=(sum>>16)+(sum&0xffff);
	sum+=(sum>>16);
	unsigned short rec=~sum;
 	if(rec==0) return 1;
	return -1;
}
