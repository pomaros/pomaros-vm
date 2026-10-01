// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <dos/dostags.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <aros/debug.h>
#include <string.h>

struct Library *SocketBase;
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
/* Explicit host endpoint and per-run challenge: four IPv4 bytes, two port
 * bytes in network order, 32 random bytes. No public service is contacted. */
static int test_socket_exchange(void)
{
    UBYTE request[39],reply[33];
    BPTR file=Open((CONST_STRPTR)"SYS:NETECHO.REQ",MODE_OLDFILE);
    if(!file)return 1; /* optional; runner separately requires the marker */
    LONG count=Read(file,request,sizeof(request));Close(file);
    if(count!=38)return 0;
    struct sockaddr_in peer={0};
    peer.sin_len=sizeof(peer);peer.sin_family=AF_INET;
    memcpy(&peer.sin_addr,request,4);memcpy(&peer.sin_port,request+4,2);
    LONG fd=socket(AF_INET,SOCK_DGRAM,0);int okay=0;
    if(fd<0)return 0;
    /* Connected UDP also filters replies by source address and port. */
    if(connect(fd,(struct sockaddr *)&peer,sizeof(peer)))goto done;
    for(unsigned attempt=0;attempt<3 && !okay;++attempt) {
        LONG sent=send(fd,request+6,32,0);
        bug("[VM] UDP send=%ld errno=%ld\n",(long)sent,(long)Errno());
        if(sent!=32)break;
        fd_set reads;FD_ZERO(&reads);FD_SET(fd,&reads);
        struct timeval timeout={0};timeout.tv_sec=2;
        if(WaitSelect(fd+1,&reads,NULL,NULL,&timeout,NULL)>0 &&
           recv(fd,reply,sizeof(reply),0)==32 && !memcmp(reply,request+6,32))
            okay=1;
    }
done:
    CloseSocket(fd);
    mark(okay?"[VM] AROSTCP host UDP exchange PASS\n":"[VM] AROSTCP host UDP exchange FAIL\n");
    return okay;
}
/* Fresh stack, minimal NetDB and no seeded hosts entry: exercise the public
 * resolver using the nameservers delivered by DHCP, not a fixed IP fallback. */
static int test_dns(void)
{
    BPTR request=Lock((CONST_STRPTR)"SYS:DNS.REQ",ACCESS_READ);
    if(!request)return 1;
    UnLock(request);
    for(unsigned attempt=0;attempt<3;++attempt) {
        struct hostent *host=gethostbyname("example.com");
        if(host && host->h_addrtype==AF_INET && host->h_length==4 &&
           host->h_addr_list && host->h_addr_list[0]) {
            const UBYTE *ip=(APTR)host->h_addr_list[0];
            if(ip[0] && ip[0]!=127 && ip[0]<224) {
                bug("[VM] DNS example.com IPv4=%u.%u.%u.%u\n",ip[0],ip[1],ip[2],ip[3]);
                mark("[VM] AROSTCP DHCP DNS lookup PASS\n");return 1;
            }
        }
        Delay(50);
    }
    mark("FAIL: AROSTCP DHCP DNS lookup\n");return 0;
}
/* A real stack and DHCP client must configure vn0. No synthetic address,
 * hardcoded NAT subnet, or raw transport result satisfies this check. */
int test_network_stack(void)
{
    #ifndef VM_NETWORK_DESKTOP
    BPTR lock=Lock((CONST_STRPTR)"SYS:System/Network/AROSTCP/C/AROSTCP",ACCESS_READ);
    if(!lock)return -1;
    UnLock(lock);
    mark("[VM] AROSTCP startup begin\n");
    /* The headless profile has DOS but no Shell. Start the existing executable
     * as a normal CLI process, with its program directory and owned seglist. */
    BPTR segment=LoadSeg((CONST_STRPTR)"SYS:System/Network/AROSTCP/C/AROSTCP");
    BPTR home=Lock((CONST_STRPTR)"SYS:System/Network/AROSTCP/C",ACCESS_READ);
    if(!segment || !home || !CreateNewProcTags(NP_Seglist,segment,
            NP_FreeSeglist,TRUE,NP_Cli,TRUE,NP_HomeDir,home,
            NP_Name,(IPTR)"AROSTCP",NP_Arguments,(IPTR)"\n",
            NP_StackSize,262144,TAG_DONE)) {
        bug("[VM] AROSTCP process creation error=%ld\n",(long)IoErr());
        if(segment)UnLoadSeg(segment);
        if(home)UnLock(home);
        return 0;
    }
    #endif
    for(unsigned tick=0;tick<500 && !SocketBase;++tick) {
        SocketBase=OpenLibrary((CONST_STRPTR)"bsdsocket.library",0);
        if(!SocketBase)Delay(1);
    }
    if(!SocketBase) { mark("[VM] AROSTCP library unavailable FAIL\n");return 0; }
    mark("[VM] AROSTCP socket library PASS\n");
    LONG fd=socket(AF_INET,SOCK_DGRAM,0);int okay=0;
    if(fd<0)goto done;
    for(unsigned tick=0;tick<750;++tick) {
        struct ifreq req={0};memcpy(req.ifr_name,"vn0",4);
        if(!IoctlSocket(fd,SIOCGIFADDR,(APTR)&req)) {
            const UBYTE *ip=(APTR)&((struct sockaddr_in *)&req.ifr_addr)->sin_addr;
            if(req.ifr_addr.sa_family==AF_INET && ip[0] && ip[0]!=127 &&
               !(ip[0]==169 && ip[1]==254)) {
                bug("[VM] AROSTCP vn0 IPv4=%u.%u.%u.%u\n",ip[0],ip[1],ip[2],ip[3]);
                okay=1;break;
            }
        }
        Delay(1);
    }
    CloseSocket(fd);
done:
    mark(okay?"[VM] AROSTCP DHCP address PASS\n":"[VM] AROSTCP DHCP address FAIL\n");
    if(okay)okay=test_socket_exchange();
    if(okay)okay=test_dns();
    CloseLibrary(SocketBase);SocketBase=NULL;
    return okay;
}

#ifdef VM_NETWORK_DESKTOP
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
int main(void)
{
    /* Observe the stack started by Startup-Sequence; do not start it here. */
    int okay=test_network_stack();
    mark(okay==1?"[VM] desktop network autostart PASS\n":"FAIL: desktop network autostart\n");
    return okay==1?0:20;
}
#endif
