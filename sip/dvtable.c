
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create()
{
  	int nbnum = topology_getNbrNum();
	int num = nbnum+1;
	int N = topology_getNodeNum();
	dv_t* rec = (dv_t*)malloc(sizeof(dv_t)*num);
	memset(rec,0,sizeof(dv_t)*num);
	int i=0;
	int *allNode = topology_getNodeArray();
	int *nbNode = topology_getNbrArray();
	for(i=1;i<num;i++)
	{
		rec[i].nodeID = nbNode[i-1];
		rec[i].dvEntry = (dv_entry_t*)malloc(sizeof(dv_entry_t)*N);
		memset(rec[i].dvEntry,0,sizeof(dv_entry_t)*N);
		int j;
		for(j=0;j<N;j++)
			{
				rec[i].dvEntry[j].nodeID = allNode[j];
				if(nbNode[i-1] == allNode[j])
					rec[i].dvEntry[j].cost = 0;
				else 
					rec[i].dvEntry[j].cost = INFINITE_COST;
			}
	}
	int myid =  topology_getMyNodeID();
	rec[0].nodeID = myid;
	rec[0].dvEntry = (dv_entry_t*)malloc(sizeof(dv_entry_t)*N);
	memset(rec[0].dvEntry,0,sizeof(dv_entry_t)*N);
	int j;
	for(j=0;j<N;j++)
	{
		rec[0].dvEntry[j].nodeID = allNode[j];
		if(allNode[j] == myid)
			rec[0].dvEntry[j].cost = 0;
		else 
			rec[0].dvEntry[j].cost = INFINITE_COST;
	}
	char buf[1024];
	char *p,*q,*k;
	FILE *file = fopen("../topology/topology.dat", "r");
	if(file == NULL){
		return NULL;
	}
	char hostname[64];
	gethostname(hostname, 64);
	while( fgets(buf, 1024 , file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		q = strtok(NULL, "\t\b\n\v\r ");
		k = strtok(NULL,"\t\b\n\v\r ");
		if(strcmp(p,hostname)==0)
		{
			int nodeID = topology_getNodeIDfromname(q);
			for(j=0;j<N;j++) if(rec[0].dvEntry[j].nodeID == nodeID) rec[0].dvEntry[j].cost = atoi(k);
		}
		if(strcmp(q,hostname)==0)
		{
			int nodeID = topology_getNodeIDfromname(p);
			for(j=0;j<N;j++) if(rec[0].dvEntry[j].nodeID == nodeID) rec[0].dvEntry[j].cost = atoi(k);
		}
	}
	free(nbNode);
	free(allNode);
	fclose(file);
	return rec;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
	int num = topology_getNbrNum()+1;
	int i;
	for(i=0;i<num;i++)
		free(dvtable[i].dvEntry);
	free(dvtable);
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
	int num = topology_getNbrNum()+1;
	int N = topology_getNodeNum();
	int i,j;
	for(i=0;i<num;i++)
		if(dvtable[i].nodeID == fromNodeID)
		{
			for(j=0;i<N;j++)
				if(dvtable[i].dvEntry[j].nodeID==toNodeID)
					{
						dvtable[i].dvEntry[j].cost = cost;
						return 1;
					}
		}
	return -1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
	int num = topology_getNbrNum()+1;
	int N = topology_getNodeNum();
	int i,j;
	for(i=0;i<num;i++)
		if(dvtable[i].nodeID == fromNodeID)
		{
			for(j=0;i<N;j++)
				if(dvtable[i].dvEntry[j].nodeID==toNodeID)
						return dvtable[i].dvEntry[j].cost;
		}
        return INFINITE_COST;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
	printf("dvtable: begin:\n");
	int num = topology_getNbrNum()+1;
	int N = topology_getNodeNum();
	int i,j;
	for(i=0;i<num;i++)
		for(j=0;j<N;j++)
			printf("FromNodeID :%d, ToNodeID: %d, Cost: %d\n",dvtable[i].nodeID,dvtable[i].dvEntry[j].nodeID,dvtable[i].dvEntry[j].cost);
	return;
}
