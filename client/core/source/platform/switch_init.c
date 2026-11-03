/*
    libnx initialization used by every NXKit app.

    This is a modified version of the wrapper shipped with borealis: besides the
    services borealis needs, it also mounts the SD card so that apps can keep
    their config / cache files under "sdmc:/switch/nxkit/".
*/

#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <switch.h>
#include <unistd.h>

static int nxlink_sock = -1;

uint32_t htonl(uint32_t hostlong) { return __builtin_bswap32(hostlong); }

uint16_t htons(uint16_t hostshort) { return __builtin_bswap16(hostshort); }

uint32_t ntohl(uint32_t netlong) { return __builtin_bswap32(netlong); }

uint16_t ntohs(uint16_t netshort) { return __builtin_bswap16(netshort); }

void userAppInit(void)
{
    printf("userAppInit\n");
    appletLockExit();
    SocketInitConfig cfg = *(socketGetDefaultInitConfig());

    cfg.bsd_service_type = BsdServiceType_System;

    AppletType at = appletGetAppletType();
    if (at == AppletType_Application || at == AppletType_SystemApplication)
    {
        cfg.num_bsd_sessions = 16;
        cfg.sb_efficiency    = 8;
        socketInitialize(&cfg);
    }
    else
    {
        cfg.num_bsd_sessions = 2;
        cfg.sb_efficiency    = 1;
        socketInitialize(&cfg);
    }

#ifdef DEBUG
    nxlink_sock = nxlinkStdio();
#endif

    romfsInit();
    plInitialize(PlServiceType_User);
    setsysInitialize();
    setInitialize();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);

    /* Enable "sdmc:/..." access for Config / DiskCache. */
    fsdevMountSdmc();

    appletUnlockExit();
}

void userAppExit(void)
{
    printf("userAppExit\n");

    fsdevUnmountAll();

    nifmExit();
    psmExit();
    setExit();
    setsysExit();
    plExit();

    romfsExit();

    if (nxlink_sock != -1)
        close(nxlink_sock);

    socketExit();
}
