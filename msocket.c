#include "msocket.h"

SM* m_sm;
SOCK_INFO* m_sinfo;
sem_t* m_mutex;
sem_t *m_sem1, *m_sem2;

int min(int a, int b) {
    return (a < b) ? a : b;
}

void m_link(){
    static int cnt = 0;
    if(cnt)return;
    printf("shared memory linking in msocket\n");
    cnt++;
    int shm_sm_id = shmget(SHM_SM_KEY, MAX_SM_ENTRIES * sizeof(SM), 0);
    if (shm_sm_id == -1) {
        perror("Failed to Load SM");
        return;
    }
    m_sm = (SM*)shmat(shm_sm_id, NULL, 0);
    int shm_socki_id = shmget(SHM_SOCKI_KEY, MAX_SM_ENTRIES * sizeof(SOCK_INFO), 0);
    if (shm_socki_id == -1) {
        perror("Failed to Load SOCK_INFO");
        return;
    }
    m_sinfo = (SOCK_INFO*)shmat(shm_socki_id, NULL, 0);
    m_mutex = sem_open("mutex", 0);
    m_sem1 = sem_open("sem1", 0);
    m_sem2 = sem_open("sem2", 0);
}

void m_unlink(){
    shmdt(m_sm);
    shmdt(m_sinfo);
    sem_close(m_mutex);
    sem_close(m_sem1);
    sem_close(m_sem2);
}

int Drop_Message(float p){
    // srand(1);
    int pool = 1000;
    float r = rand()%pool + 1;
    r = r/pool;
    if(r<p){
        return 1;
    }
    return 0;
}

int m_socket(int domain, int type, int protocol) {
    m_link();
    printf("Creating socket\n");
    if (type != SOCK_MTP) {
        errno = EINVAL;
        return -1;
    }
    sem_wait(m_mutex);
    int i;
    for (i = 0; i < MAX_SM_ENTRIES; i++) {
        if (m_sm[i].state == FREE) {
            break;
        }
    }
    if (i == MAX_SM_ENTRIES) {
        errno = ENOBUFS;
        sem_post(m_mutex);
        return -1;
    }
    m_sinfo->op = CREATE_SOCKET;
    m_sinfo->sock_id = -1;
    m_sinfo->err_no = 0;
    sem_post(m_sem1);
    sem_wait(m_sem2);
    if(m_sinfo->sock_id==-1){
        errno = m_sinfo->err_no;
        sem_post(m_mutex);
        return -1;
    }
    m_sm[i].state = ALLOCATED;
    m_sm[i].alloc_pid = getpid();
    m_sm[i].udp_sfd = m_sinfo->sock_id;
    m_sm[i].swnd.base = 1;
    m_sm[i].swnd.next = 1;
    m_sm[i].swnd.ptr = 1;
    for(int j=0; j<SBUFF_SIZE; j++){
        memset(m_sm[i].sbuff[j%SBUFF_SIZE].data, 0, 1024);
        m_sm[i].sbuff[j%SBUFF_SIZE].seq_no = -1;
        m_sm[i].sbuff[j%SBUFF_SIZE].is_ack = 0;
    }
    m_sm[i].rwnd.base = 1;
    m_sm[i].rwnd.next = 1;
    m_sm[i].rwnd.ptr = 1;
    for(int j=1; j<=RBUFF_SIZE; j++){
        memset(m_sm[i].rbuff[j%RBUFF_SIZE].data, 0, 1024);
        m_sm[i].rbuff[j%RBUFF_SIZE].is_recved = 0;
        m_sm[i].rbuff[j%RBUFF_SIZE].seq_no = j;
    }
    sem_post(m_mutex);
    printf("socket created with id %d=>%d\n",i,m_sm[i].udp_sfd);    
    return i;
}

int m_bind(int sockfd, const struct sockaddr *src_addr,const struct sockaddr *dest_addr, socklen_t addrlen) {
    int id = sockfd;
    sem_wait(m_mutex);
    m_sinfo->op = BIND_SOCKET;
    m_sinfo->sock_id = m_sm[id].udp_sfd;
    m_sinfo->port = ntohs(((struct sockaddr_in *)src_addr)->sin_port);
    m_sinfo->err_no = 0;
    sem_post(m_sem1);
    sem_wait(m_sem2);
    if(m_sinfo->sock_id==-1){
        errno = m_sinfo->err_no;
        sem_post(m_mutex);
        return -1;
    }
    m_sm[id].state = BOUND;
    strcpy(m_sm[id].des_A, inet_ntoa(((struct sockaddr_in *)dest_addr)->sin_addr));
    m_sm[id].des_P = ntohs(((struct sockaddr_in *)dest_addr)->sin_port);
    printf("m_bind: bound to port %d\n",m_sinfo->port);
    sem_post(m_mutex);
}

int m_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
    int id = sockfd;
    sem_wait(m_mutex);
    if(m_sm[id].state != BOUND){
        errno = EINVAL;
        sem_post(m_mutex);
        return -1;
    }
    if(strcmp(m_sm[id].des_A, inet_ntoa(((struct sockaddr_in *)dest_addr)->sin_addr))
    || m_sm[id].des_P != ntohs(((struct sockaddr_in *)dest_addr)->sin_port)){
        errno = ENOTCONN;
        sem_post(m_mutex);
        return -1;
    }
    int base = m_sm[id].swnd.base;
    int wptr = m_sm[id].swnd.ptr;
    if(wptr-base == SBUFF_SIZE){
        errno = ENOBUFS;
        sem_post(m_mutex);
        return -1;
    }
    wptr = wptr%SBUFF_SIZE;
    printf(" copied to send buffer %d\n",wptr);
    strncpy(m_sm[id].sbuff[wptr].data, buf, len);
    m_sm[id].sbuff[wptr].seq_no = m_sm[id].swnd.ptr;
    m_sm[id].sbuff[wptr].is_ack = 0;
    m_sm[id].swnd.ptr++;
    sem_post(m_mutex);
    return len;
}

int m_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    int id = sockfd;
    sem_wait(m_mutex);
    if(m_sm[id].state != BOUND){
        errno = EINVAL;
        sem_post(m_mutex);
        return -1;
    }
    int ptr = m_sm[id].rwnd.base;
    int next = m_sm[id].rwnd.next;
    int rptr = ptr%RBUFF_SIZE;
    if(ptr == next){
        printf("next = %d ptr = %d\n",next,ptr);
        sem_post(m_mutex);
        errno = ENOMSG;
        return -1;
    }
    strncpy(buf, m_sm[id].rbuff[rptr].data, len);
    printf("cleared buffer at %d\n",ptr);
    m_sm[id].rbuff[rptr].seq_no += RBUFF_SIZE;
    m_sm[id].rbuff[rptr].is_recved = 0;
    m_sm[id].rwnd.base++;
    sem_post(m_mutex);
    return min(len,1024);
}

int m_close(int sockfd) {
    sem_wait(m_mutex);
    m_sinfo->op = CLOSE_SOCKET;
    m_sinfo->sock_id = m_sm[sockfd].udp_sfd;
    m_sinfo->err_no = 0;
    sem_post(m_sem1);
    sem_wait(m_sem2);
    if(m_sinfo->sock_id==-1){
        errno = m_sinfo->err_no;
        return -1;
    }
    m_sm[sockfd].state = FREE;
    sem_post(m_mutex);
    return 0;
}