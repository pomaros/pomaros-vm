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
#include <aros/debug.h>
#include <string.h>

struct Library *SocketBase;
static void mark(const char *s) { while(*s)RawPutChar(*s++); }
/* A real stack and DHCP client must configure vn0. No synthetic address,
 * hardcoded NAT subnet, or raw transport result satisfies this check. */
int test_network_stack(void)
{
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
    CloseLibrary(SocketBase);SocketBase=NULL;
    mark(okay?"[VM] AROSTCP DHCP address PASS\n":"[VM] AROSTCP DHCP address FAIL\n");
    return okay;
}
