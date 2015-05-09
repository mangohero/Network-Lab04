//�ļ���: common/constants.h

//����: ����ļ�����STCPЭ��ʹ�õĳ���

//��������: 2013��

#ifndef CONSTANTS_H
#define CONSTANTS_H


/*******************************************************************/
//��������
/*******************************************************************/

//����STCP����֧�ֵ����������. ���TCB��Ӧ����MAX_TRANSPORT_CONNECTIONS����Ŀ.
#define MAX_TRANSPORT_CONNECTIONS 10
//���γ���
//MAX_SEG_LEN = 1500 - sizeof(seg header) - sizeof(ip header)
#define MAX_SEG_LEN  1464
//���ݰ���ʧ��Ϊ10%
#define PKT_LOSS_RATE 0.1
//SYN_TIMEOUTֵ, ��λΪ����
#define SYN_TIMEOUT 500000000
//FIN_TIMEOUTֵ, ��λΪ����
#define FIN_TIMEOUT 500000000
//stcp_client_connect()�е����SYN�ش�����
#define SYN_MAX_RETRY 5
//stcp_client_disconnect()�е����FIN�ش�����
#define FIN_MAX_RETRY 5
//������CLOSEWAIT��ʱֵ, ��λΪ��
#define CLOSEWAIT_TIMEOUT 5
//sendBuf_timer�̵߳���ѯ���, ��λΪ����
#define SENDBUF_POLLING_INTERVAL 500000000
//STCP�ͻ�����stcp_server_recv()������ʹ�����ʱ��������ѯ���ջ�����, �Լ���Ƿ������������ȫ������, ��λΪ��.
#define RECVBUF_POLLING_INTERVAL 1
//stcp_server_accept()����ʹ�����ʱ������æ�ȴ�TCB״̬ת��, ��λΪ����
#define ACCEPT_POLLING_INTERVAL 500000000
//���ջ�������С
#define RECEIVE_BUF_SIZE 1000000
//���ݶγ�ʱֵ, ��λΪ����
#define DATA_TIMEOUT 500000
//GBN���ڴ�С
#define GBN_WINDOW 10



/*******************************************************************/
//�ص��������
/*******************************************************************/

#define MAXNAME 100
#define MAXITEM 100
#define MAXHOST 4

//����˿ں������ص������нڵ�֮��Ļ���, ��Ӧ���޸���Ϊһ�����ֵ�Ա��������ͬѧ�����÷�����ͻ
#define CONNECTION_PORT 8901

//����˿ں���SON���̴�, ����SIP��������, ��Ӧ���޸���Ϊһ�����ֵ�Ա��������ͬѧ�����÷�����ͻ
#define SON_PORT 8902

//���SIP�������ݳ���: 1500 - sizeof(sip header)
#define MAX_PKT_LEN 1488 

/*******************************************************************/
//��������
/*******************************************************************/
//�ص�����֧�ֵ����ڵ��� 
#define MAX_NODE_NUM 10

//���·�ɱ���� 
#define MAX_ROUTINGTABLE_SLOTS 10

//��������·����ֵ, ��������ڵ�Ͽ�������, ����֮�����·����ֵ����INFINITE_COST
#define INFINITE_COST 999

//SIP���̴�����˿ڲ��ȴ�����STCP���̵�����, ��Ӧ���޸���Ϊһ�����ֵ�Ա��������ͬѧ�����÷�����ͻ.
#define SIP_PORT 8903

//���ǹ㲥�ڵ�ID. ���SON���̴�SIP���̴����յ�һ��Ŀ��ڵ�IDΪBROADCAST_NODEID�ı���, ��Ӧ�ý��ñ��ķ��͸����������ھ�
#define BROADCAST_NODEID 9999

//·�ɸ��¹㲥���, ����Ϊ��λ
#define ROUTEUPDATE_INTERVAL 5


#endif
