#include "msocket.h"

int shm_sm_id;
SM* sm;
int shm_socki_id;
SOCK_INFO* s_info;
sem_t* mutex, *sem1, *sem2;

int send_cnt, recv_cnt;

void sigH(int sig){
    printf("handler called %d\n",sig);
    sem_unlink("mutex");
    sem_unlink("sem1");
    sem_unlink("sem2");
    for(int i=0;i<MAX_SM_ENTRIES;i++){
        if(sm[i].state==BOUND){
            close(sm[i].udp_sfd);
        }
    }
    shmdt(sm);
    shmctl(shm_sm_id, IPC_RMID, NULL);
    shmctl(shm_socki_id, IPC_RMID, NULL);
    int c_pid = getpid();
    kill(c_pid, sig); 
}

char add_header(int type,int seq_no, int rwnd_sz){
    char header = 0;
    header = header | (type << 7);
    header = header | (seq_no << 3);
    header = header | rwnd_sz;
    return header;
}

void strip_header(char header, int* type, int* seq_no, int* rwnd_sz){
    *type = (header & 0x80) >> 7;
    *seq_no = (header & 0x78) >> 3;
    *rwnd_sz = header & 0x07;
}

void print_binary(int num) {
    for(int i=3;i>=0;i--){
        printf("%d", (num & (1 << i)) ? 1 : 0);
    }
}

void print_header(char header){
    int type, seq_no, rwnd_sz;
    strip_header(header, &type, &seq_no, &rwnd_sz);
    if(type == M_TYPE){
        printf("M_TYPE ");
    }
    else if(type == A_TYPE){
        printf("A_TYPE ");
    }
    printf("Sequence Number: ");print_binary(seq_no);printf(" ==> %3d",seq_no);
    printf(" RWND Size: ");
    if(rwnd_sz == 7){
        printf("don't care\n");
        return;
    }
    print_binary(rwnd_sz);printf(" ==> %3d\n",rwnd_sz);
}

void print_swnd(int i){
    printf("SWND(~%d):B ",i);
    for(int j=sm[i].swnd.base;j<sm[i].swnd.next;j++){
        printf("%d ",j);
    }
    printf("N: ");
    for(int j=sm[i].swnd.next;j<sm[i].swnd.base+SWND;j++){
        printf("%d ",j);
    }
    printf("\n");
}

void print_rwnd(int i){
    printf("RWND(~%d):B ",i);
    for(int j=sm[i].rwnd.base;j<sm[i].rwnd.next;j++){
        printf("%d ",j);
    }
    printf("N: ");
    for(int j=sm[i].rwnd.next;j<sm[i].rwnd.base+RWND;j++){
        printf("%d ",j);
    }
    printf("\n");
}

void send_msg(int i, char* buffer, int len){
    send_cnt++;
    printf("@@@@@ seding\n");
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(sm[i].des_A);
    addr.sin_port = htons(sm[i].des_P);
    print_header(buffer[0]);
    int val = sendto(sm[i].udp_sfd, buffer, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    printf("@@@@@ sent\n");
}

int recv_msg(int i, char* buffer, int len){
    recv_cnt++;
    printf("##### recving\n");
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(sm[i].des_A);
    addr.sin_port = htons(sm[i].des_P);
    socklen_t addr_len = sizeof(addr);
    int bytes_read = recvfrom(sm[i].udp_sfd, buffer, len, 0, (struct sockaddr *)&addr, &addr_len);
    print_header(buffer[0]);
    printf("##### recved\n");
    return bytes_read;
}

void* receiver(void* arg){
    fd_set readfds;
    struct timeval timeout;
    while(1){
        printf("-----------------------receiver threadup--------------------\n");
        sem_wait(mutex);
        FD_ZERO(&readfds);
        int max_fd = 0;
        for(int i=0;i<MAX_SM_ENTRIES;i++){
            if(sm[i].state != BOUND)continue;
            FD_SET(sm[i].udp_sfd, &readfds);
            if(sm[i].udp_sfd > max_fd)max_fd = sm[i].udp_sfd;
        }
        sem_post(mutex);
        timeout.tv_sec = T;
        timeout.tv_usec = 0;
        int r_val = select(max_fd+1, &readfds, NULL, NULL, &timeout);
        if(r_val == -1){
            perror("Select error");
            continue;
        }
        if(r_val == 0){// if time out send wnd size of all active sockets
            sem_wait(mutex);
            // printf("\t\t---------timeout in RRR\n");
            for(int i=0;i<MAX_SM_ENTRIES;i++){
                if(sm[i].state != BOUND)continue;
                printf("\t*************receive socket %d ***********\n",i);
                int last_sent = sm[i].rwnd.next-1;
                int rwnd_size = sm[i].rwnd.base + RWND - sm[i].rwnd.next;
                char header = add_header(A_TYPE,last_sent%16,rwnd_size);
                // print_header(add_header(A_TYPE,last_sent%16,rwnd_size));
                printf("\t r.b: %d and r.n %d: DUPACK ",sm[i].rwnd.base,sm[i].rwnd.next);
                send_msg(i, &header, 1);
            }
            // printf("\t\t-----------------  RRR\n");
            sem_post(mutex);
            continue;
        }
        else{//if data is available on some socket
            sem_wait(mutex);
            for(int i=0;i<MAX_SM_ENTRIES;i++){
                if(sm[i].state != BOUND)continue;
                printf("\t************receive socket %d ***********\n",i);
                print_rwnd(i);
                int fd = sm[i].udp_sfd;
                if(FD_ISSET(fd, &readfds)){
                    char buffer[1025];
                    int bytes_read = recv_msg(i, buffer, 1025);
                    if(bytes_read == -1){
                        perror("recvfrom error");
                        continue;
                    }
                    if(Drop_Message(DROP_PROBABILITY)){
                        printf("\t\tMessage Dropped\n");
                        // printf("\t\t");print_header(buffer[0]);
                        continue;
                    }
                    int type, seq_no, rwnd_sz;
                    strip_header(buffer[0], &type, &seq_no, &rwnd_sz);
                    if(type == M_TYPE){
                        /*
                            message received send ACK
                        */
                        int k = sm[i].rwnd.next;
                        for(;k<sm[i].rwnd.base+RWND;k++){
                            if(sm[i].rbuff[k%RBUFF_SIZE].seq_no%16==seq_no)break;
                        }
                        if(k==sm[i].rwnd.base+RWND || (sm[i].rbuff[k%RBUFF_SIZE].is_recved)){
                            printf("\t\tpacket not in rwnd/status:(%d) Msg killed\n",sm[i].rbuff[k%RBUFF_SIZE].is_recved);
                            // printf("\t\t\t");print_header(buffer[0]);   
                        }
                        else{
                            printf("\t\t*** status recv for msg %d\n",seq_no);
                            strncpy(sm[i].rbuff[k%RBUFF_SIZE].data, buffer+1, 1024);
                            sm[i].rbuff[k%RBUFF_SIZE].is_recved = 1;
                        }
                        while(sm[i].rwnd.next < sm[i].rwnd.base+RBUFF_SIZE && sm[i].rbuff[sm[i].rwnd.next%RBUFF_SIZE].is_recved){
                            sm[i].rwnd.next++;
                        }
                        int last_sent = sm[i].rwnd.next-1;
                        int rwnd_size = sm[i].rwnd.base+RBUFF_SIZE - sm[i].rwnd.next;
                        char header = add_header(A_TYPE,last_sent%16,rwnd_size);
                        send_msg(i, &header, 1);
                    }
                    else if(type == A_TYPE){
                        /*
                            ACK received update the swnd base 
                            seq_no  -1 and who to make ack = 1
                        */
                        int k = sm[i].swnd.base;
                        for(;k<sm[i].swnd.next;k++){
                            if(sm[i].sbuff[k%SBUFF_SIZE].seq_no%16==seq_no)break;
                        }
                        // print_header(buffer[0]);
                        if(k==sm[i].swnd.next){
                            printf("ACK of msg not in window [%d , %d)\n",sm[i].swnd.base,sm[i].swnd.next);
                        }
                        else{
                            printf("ACK of msg in window [%d , %d)\n",sm[i].swnd.base,sm[i].swnd.next);
                            int cur_sr = sm[i].swnd.base+SWND;
                            int new_sr = k+1+rwnd_sz;
                            int diff   = new_sr - cur_sr;
                            if(new_sr>cur_sr){
                                printf("\t===base %d update by %d\n",sm[i].swnd.base,diff);
                                sm[i].swnd.base += diff;
                            }
                        }
                        
                    }
                }
            }
            sem_post(mutex);
        }
    }
    pthread_exit(NULL);
}

void* sender(void* arg){  
    time_t last[25];
    memset(last,0,sizeof(last));
    while (1) {
        printf("-----------------------sender thread up-------\n");
        sleep(T/2);
        time_t cur_time;
        time(&cur_time);
        sem_wait(mutex);
        for(int i=0;i<25;i++){
            if(sm[i].state != BOUND)continue;
            printf("\t**************sender thread socket %d ***********\n",i);
            print_swnd(i);
            if(cur_time - last[i] >= T){//retransmit the data between [base,next)
                for(int j=sm[i].swnd.base;j<min(sm[i].swnd.next,sm[i].swnd.ptr);j++){
                    int k = j%SBUFF_SIZE;
                    char buffer[1025];
                    int rwnd_size = sm[i].rwnd.base + RWND - sm[i].rwnd.next;
                    buffer[0] = add_header(M_TYPE,sm[i].sbuff[k].seq_no%16,7);//since rwnd<=5 so 6,7 can be used for some communication
                    strcpy(buffer+1, sm[i].sbuff[k].data);
                    printf("\tRetransm: ");
                    send_msg(i, buffer,1025);
                    // print_header(buffer[0]);
                }
                time(&last[i]);
            }
            //send new packets in [next, base+SWND)
            for(int j=sm[i].swnd.next;j<min(sm[i].swnd.base+SWND,sm[i].swnd.ptr);j++){
                int k = j%SBUFF_SIZE;
                char buffer[1025];
                buffer[0] = add_header(M_TYPE,j%16,7);
                strcpy(buffer+1, sm[i].sbuff[k].data);
                printf("\tnew send: ");
                send_msg(i, buffer,1025);
                // print_header(buffer[0]);
                sm[i].swnd.next++;
            }
        }
        sem_post(mutex);
    }
    pthread_exit(NULL); 
}

void* Garbage(){
    printf("Garbage thread up\n");
    while(1){
        sleep(10);
        sem_wait(mutex);    
        for(int i=0;i<MAX_SM_ENTRIES;i++){
            if(sm[i].state==FREE){
                continue;
            }
            if(sm[i].alloc_pid==0){
                sm[i].state = FREE;
                close(sm[i].udp_sfd);
                continue;
            }
            if(kill(sm[i].alloc_pid, 0)==-1){
                sm[i].state = FREE;
                sm[i].alloc_pid = 0;
                printf("\t\t\tGarbage %d => %d\n",i,sm[i].udp_sfd);
                close(sm[i].udp_sfd);
                sm[i].udp_sfd = -1;
            }
        }
        sem_post(mutex);
    }
    pthread_exit(NULL);
}

int main() {
    printf("creating SM\n");
    signal(SIGINT, sigH);
    signal(SIGSEGV, sigH);
    mutex = sem_open("mutex", O_CREAT, 0666, 1);
    sem1 = sem_open("sem1", O_CREAT, 0666, 0);
    sem2 = sem_open("sem2", O_CREAT, 0666, 0);
    
    //---------------------------create shared memory--------------------------------------
    shm_sm_id = shmget(SHM_SM_KEY, MAX_SM_ENTRIES * sizeof(SM), IPC_CREAT | 0666);
    if (shm_sm_id == -1) {
        perror("Failed to create SM shared memory segment");
        return 1;
    }
    sm = (SM*)shmat(shm_sm_id, NULL, 0);
    printf("Created SM\n");
    shm_socki_id = shmget(SHM_SOCKI_KEY, MAX_SM_ENTRIES * sizeof(SOCK_INFO), IPC_CREAT | 0666);
    if (shm_socki_id == -1) {
        perror("Failed to create SOCK_INFO shared memory segment");
        return 1;
    }
    s_info = (SOCK_INFO*)shmat(shm_socki_id, NULL, 0);
    //---------------------------------------------------------------------------------------

    pthread_t senderThread, receiverThread, garbageThread;
    if (pthread_create(&senderThread, NULL, sender, NULL) != 0) {
        fprintf(stderr, "Failed to create sender thread\n");
        return 1;
    }
    if (pthread_create(&receiverThread, NULL, receiver, NULL) != 0) {
        fprintf(stderr, "Failed to create receiver thread\n");
        return 1;
    }
    if(pthread_create(&garbageThread,NULL,Garbage,NULL)!=0){
        fprintf(stderr, "Failed to create Garbage thread\n");
        return 1;
    }

    int close_cnt = 0;
    send_cnt = 0; recv_cnt = 0;
    while(1){
        /*
        if(close_cnt==2){//to populate the table    
            FILE* fd = fopen("table.txt","a");
            char buff[1024];
            memset(buff,0,1024);
            sprintf(buff,"send_cnt: %d ==== recv_cnt: %d === P: %f \n",send_cnt,recv_cnt,DROP_PROBABILITY);
            fprintf(fd,"%s",buff);
            break;
        }
        */
        sem_wait(sem1);
        printf("M acquired sem1\n");
        switch (s_info->op)
        {
            case CREATE_SOCKET:
                int r_val = socket(AF_INET, SOCK_DGRAM, 0);
                if (r_val == -1) {
                    s_info->err_no = errno;
                    continue;
                }
                s_info->sock_id = r_val;
                s_info->op = 0;
                printf("Socket created successfully\n");
                break;
            case BIND_SOCKET:
                struct sockaddr_in cur_addr;
                cur_addr.sin_family = AF_INET;
                // cur_addr.sin_addr.s_addr = inet_addr(s_info->ip_address);
                cur_addr.sin_addr.s_addr = INADDR_ANY;
                cur_addr.sin_port = htons(s_info->port);
                r_val = bind(s_info->sock_id, (struct sockaddr *)&cur_addr, sizeof(cur_addr));
                if (r_val == -1) {
                    s_info->err_no = errno;
                    continue;
                }
                printf("Socket bound to port %d\n", s_info->port);
                break;
            case CLOSE_SOCKET:
                r_val = close(s_info->sock_id);
                if (r_val == -1) {
                    s_info->err_no = errno;
                    continue;
                }
                close_cnt++;
                printf("Socket closed successfully\n");
                break;
            default:
                break;
        }
        sem_post(sem2);
    }
    return 0;
}