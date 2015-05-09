
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat. 
nbr_cost_entry_t* nbrcosttable_create()
{
	int nbnum = topology_getNbrNum();
	nbr_cost_entry_t* rec=(nbr_cost_entry_t*)malloc(sizeof(nbr_cost_entry_t)*nbnum);
	memset(rec,0,sizeof(nbr_cost_entry_t)*nbnum);
	char buf[1024];
	char *p,*q,*k;
	FILE *file = fopen("../topology/topology.dat", "r");
	if(file == NULL){
		return NULL;
	}
	char hostname[64];
	gethostname(hostname, 64);
	int i = 0;
	while( fgets(buf, 1024 , file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		q = strtok(NULL, "\t\b\n\v\r ");
		k = strtok(NULL,"\t\b\n\v\r ");
		if(strcmp(p,hostname)==0)
		{
			rec[i].nodeID = topology_getNodeIDfromname(q);
			rec[i].cost =  atoi(k);
			i++;
		}
		if(strcmp(q,hostname)==0)
		{
			rec[i].nodeID = topology_getNodeIDfromname(p);
			rec[i].cost = atoi(k);
			i++;
		}
	}
	fclose(file);	
	return rec;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
	free(nct);  
	return;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
	int nbnum = topology_getNbrNum();
	int i;
	for(i=0;i<nbnum;i++)
		if(nct[i].nodeID==nodeID) return nct[i].cost;
	return INFINITE_COST;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	printf("nbrcosttable: begin:\n");
  	int nbnum = topology_getNbrNum();
	int i;
	for(i=0;i<nbnum;i++)
		printf("NodeID:%d, cost:%d\n",nct[i].nodeID,nct[i].cost);
	printf("end\n");
	return;
}
