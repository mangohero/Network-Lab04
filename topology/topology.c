//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2013年

#include "topology.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <arpa/inet.h>

#include "../common/constants.h"

#define MAX_LENGTH 1024

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.

int topology_getNodeIDfromname(char* hostname){
	char buf[MAX_LENGTH];
	char *p;
	FILE *file = fopen("../topology/ip_info.dat", "r");
	if(file == NULL){
		return -1;
	}
	while( fgets(buf, MAX_LENGTH, file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		if(strcmp(p,hostname)==0)
		{
			p = strtok(NULL, "\t\b\n\v\r ");
			fclose(file);
			return (ntohl((int)(inet_addr(p)))&0x0ff);
		}
		p = strtok(NULL,"\t\b\n\v\r ");
	}
	fclose(file);
	return -1;
}
//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr){
	return (htonl((int)addr->s_addr) & 0x0ff);
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID(){
	char hostname[MAX_LENGTH];
	gethostname(hostname, MAX_LENGTH);
	return topology_getNodeIDfromname(hostname);
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum(){
	char buf[MAX_LENGTH];
	char *p,*q;
	FILE *file = fopen("../topology/topology.dat", "r");
	if(file == NULL){
		return -1;
	}
	char hostname[MAX_LENGTH];
	gethostname(hostname, MAX_LENGTH);
	int nbnum = 0;
	while( fgets(buf, MAX_LENGTH, file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		q = strtok(NULL, "\t\b\n\v\r ");
		if(strcmp(p,hostname)==0 || strcmp(q,hostname)==0)
			nbnum++;
		p = strtok(NULL,"\t\b\n\v\r ");
	}
	fclose(file);
	return nbnum;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum(){
	int num = 0;
	char buf[MAX_LENGTH];
	FILE *file = fopen("../topology/ip_info.dat", "r");
	if(file == NULL)
		return -1;
	while(fgets(buf, MAX_LENGTH, file)) num++;
	fclose(file);
	return num;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
int* topology_getNodeArray(){
	int num = topology_getNodeNum();
	int *nodeID = (int *)malloc(sizeof(int)*num);
	memset(nodeID, 0, sizeof(int)*num);
	int i = 0;
	char buf[MAX_LENGTH];
	FILE *file = fopen("../topology/ip_info.dat", "r");
	if(file == NULL)
		return NULL;
	char *p;
	while( fgets(buf, MAX_LENGTH, file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		p = strtok(NULL,"\t\b\n\v\r ");
		nodeID[i++] = (ntohl((int)(inet_addr(p)))&0x0ff);
	}
	fclose(file);
	return nodeID;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.  
int* topology_getNbrArray(){
	char buf[MAX_LENGTH];
	char *p,*q;
	FILE *file = fopen("../topology/topology.dat", "r");
	if(file == NULL){
		return -1;
	}
	char hostname[MAX_LENGTH];
	gethostname(hostname, MAX_LENGTH);
	int num = topology_getNbrNum();
	int *nodeID = (int *)malloc(sizeof(int)*num);
	memset(nodeID, 0, sizeof(int)*num);
	int i = 0;
	while( fgets(buf, MAX_LENGTH, file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		q = strtok(NULL, "\t\b\n\v\r ");
		if(strcmp(p,hostname)==0)
			nodeID[i++] = topology_getNodeIDfromname(q);
		if(strcmp(q,hostname)==0)
			nodeID[i++] = topology_getNodeIDfromname(p);
		p = strtok(NULL,"\t\b\n\v\r ");
	}
	fclose(file);
	return nodeID;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID){

	FILE *file = fopen("../topology/topology.dat", "r");
	if(file == NULL)
		return -1;
	char *p,*q;
	char buf[MAX_LENGTH];
	while( fgets(buf, MAX_LENGTH, file) ){
		p = strtok(buf, "\t\b\n\v\r ");
		q = strtok(NULL, "\t\b\n\v\r ");
		if(topology_getNodeIDfromname(p) == fromNodeID && topology_getNodeIDfromname(q) == toNodeID)
		{
			fclose(file);
			p=strtok(NULL,"\t\b\n\v\r ");
			return atoi(p);
		}
		p = strtok(NULL,"\t\b\n\v\r ");
	}
	fclose(file);
	return INFINITE_COST;
}
