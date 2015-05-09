//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2013年

#include "neighbortable.h"
#include "../common/constants.h"
#include "../topology/topology.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "stdio.h"

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
in_addr_t getIPFromID(int nodeID)
{
	 char s[20];
	 sprintf(s,"114.212.190.%d",nodeID);
	 return inet_addr(s);
}
nbr_entry_t* nt_create()
{
	nbr_entry_t *nt = (nbr_entry_t *)malloc(sizeof(nbr_entry_t)*MAX_NODE_NUM);
	memset(nt, 0, sizeof(nbr_entry_t)*MAX_NODE_NUM);
	int nbnum = topology_getNbrNum();
	int *nodeID = topology_getNbrArray();
	int i;
	for(i=0; i<nbnum; i++){
		nt[i].nodeID = nodeID[i];
		nt[i].nodeIP = getIPFromID(nodeID[i]);
	}
	free(nodeID);	
	return nt;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
 	int nbnum = topology_getNbrNum();
	int i;
	for(i=0;i<nbnum;i++)
	{	
		if(nt[i].conn!=0)
		  close(nt[i].conn);
	}
	free(nt);
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
 	int nbnum = topology_getNbrNum();
	int i;
	for(i=0;i<nbnum;i++)
		if(nt[i].nodeID == nodeID)
		{
			nt[i].conn = conn;
			return 1;
		}
  	return -1;
}
