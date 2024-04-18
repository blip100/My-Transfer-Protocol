#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define CREATE_SOCKET 1
#define BIND_SOCKET 2
#define SEND_SOCKET 3
#define RECV_SOCKET 4
#define CLOSE_SOCKET 5
#define SOCK_MTP 3
#define SBUFF_SIZE 10
#define RBUFF_SIZE 5
#define BUFFER_EMPTY 0
#define SWND 5
#define RWND 5
#define FREE 0
#define ALLOCATED 1
#define BOUND 2
#define M_TYPE 0
#define A_TYPE 1
#define MAX_SM_ENTRIES 25
#define SHM_SM_KEY 2
#define SHM_SOCKI_KEY 3
#define T 5
#define DROP_PROBABILITY 0.05
#define ENOBUFS 105

typedef struct{
    int base;
    int next;
    int ptr;
}wnd;

typedef struct{
    int is_ack;
    int seq_no;
    char data[1024];
}smsg;

typedef struct{
    int is_recved;
    int seq_no;
    char data[1024];
}rmsg;


typedef struct SM {
    int state;
    int alloc_pid;
    int udp_sfd;
    char des_A[16];
    int des_P;
    smsg sbuff[SBUFF_SIZE];
    rmsg rbuff[RBUFF_SIZE];
    wnd swnd;
    wnd rwnd;
} SM;

typedef struct SOCK_INFO{
    int sock_id;
    char ip_address[16];
    int port;
    int op;
    int err_no;
}SOCK_INFO;

int min(int a,int b);
int m_socket(int domain, int type, int protocol);
int m_bind(int sockfd, const struct sockaddr *src_addr,const struct sockaddr *dest_addr, socklen_t addrlen);
int m_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int m_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int m_close(int sockfd);
int Drop_Message(float p);