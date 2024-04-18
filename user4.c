#include "msocket.h"

int main(){
    int sockid = m_socket(AF_INET,SOCK_MTP,IPPROTO_UDP);
    printf("sockid = %d\n",sockid);
    int port = 0;
    struct sockaddr_in remoteaddr,myaddr;
    remoteaddr.sin_family = AF_INET;
    remoteaddr.sin_port = htons(8002);
    inet_aton("127.0.0.1", &remoteaddr.sin_addr);

    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(8003);
    myaddr.sin_addr.s_addr = INADDR_ANY;
    m_bind(sockid,(struct sockaddr *)&myaddr,(struct sockaddr *)&remoteaddr,sizeof(myaddr));
    printf("bind done on port %d\n",ntohs(myaddr.sin_port));
    
    char buff[1024];
    int remotelen = sizeof(remoteaddr);
    for(int i=1;i<=20;i++){
        if(m_recvfrom(sockid,buff,1024,0,(struct sockaddr *)&remoteaddr,&remotelen) < 0){
            int error_no = errno;  
            printf("receive %d failed {%s} \n",i,strerror(error_no));
            sleep(5);
            i--;
        }
        else{
            printf("%s\n",buff);
        }
    }

    for(int i=1;i<=20;i++){
        sprintf(buff,"D: %02d",i);
        if(m_sendto(sockid,buff,1024,0,(struct sockaddr *)&remoteaddr, remotelen) < 0){
            int error_no = errno;
            printf("message %s failed {%s}\n",buff,strerror(error_no));
            sleep(5);
            i--;
        }
        else{
            printf("sent %s\n",buff);
        }
    }

    printf("press enter to exit\n");
    getchar();
    m_close(sockid);
    return 0;
}