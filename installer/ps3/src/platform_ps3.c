/*
 * The PS3: the controller through PSL1GHT's io/pad (any connected pad), the system's callbacks for the PS button's
 * Quit Game, and the folders: discs in /dev_hdd0/PS2ISO, the payload and fonts beside EBOOT.BIN in the app's
 * USRDIR, and the app's data in USRDIR/data.
 */
#include "platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <io/pad.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>

#include "files.h"
#include "script.h"

#define DISCS_FOLDER "/dev_hdd0/PS2ISO"

/* src/lv2_printf.c: what the app prints, also kept in a file */
void lv2_log_open(const char *path);
void lv2_log_flush(void);

/* The main thread's stack: 1 MB, as FreeType's glyph rendering and the core's plan run on it, and PSL1GHT's 64 KB
   default is too little. */
SYS_PROCESS_PARAM(1001, 0x100000)

static volatile bool quitting;
static bool accept_is_cross = true;

static void system_event(u64 status, u64 parameter, void *context)
{
    (void)parameter;
    (void)context;
    if (status == SYSUTIL_EXIT_GAME) {
        quitting = true;
    }
}

bool platform_open(int argc, char **argv, Places *places)
{
    /* EBOOT.BIN's folder, from argv[0]: /dev_hdd0/game/<APPID>/USRDIR */
    char usrdir[400];
    snprintf(usrdir, sizeof usrdir, "%s", argc > 0 && argv[0] != NULL ? argv[0] : "/dev_hdd0/game/RACE00001/USRDIR/x");
    char *slash = strrchr(usrdir, '/');
    if (slash != NULL) {
        *slash = 0;
    }
    snprintf(places->discs, sizeof places->discs, "%s", DISCS_FOLDER);
    snprintf(places->payload, sizeof places->payload, "%s/payload", usrdir);
    snprintf(places->data, sizeof places->data, "%s/data", usrdir);
    snprintf(places->fonts, sizeof places->fonts, "%s/fonts", usrdir);
    snprintf(places->certificates, sizeof places->certificates, "%s/cacert.pem", usrdir);
    snprintf(places->packages, sizeof places->packages, "/dev_hdd0/packages");

    /* the TTY is not a terminal, so newlib would hold its lines in a buffer */
    setvbuf(stdout, NULL, _IONBF, 0);
    char log_path[600];
    snprintf(log_path, sizeof log_path, "%s/log.txt", usrdir);
    lv2_log_open(log_path);
    printf("builder: started from %s\n", usrdir);
    /* a test run (RPCS3): USRDIR/test-script.txt drives the builder, with screenshots in USRDIR/shots */
    char script_path[600];
    snprintf(script_path, sizeof script_path, "%s/test-script.txt", usrdir);
    size_t size;
    char *script = file_read_all(script_path, &size);
    if (script != NULL) {
        char shots[600];
        snprintf(shots, sizeof shots, "%s/shots", usrdir);
        file_make_directories(shots);
        script_start(script, shots, false);
        free(script);
    }

    /* slot 3: SDL keeps slot 0 for itself */
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT3, system_event, NULL);
    s32 enter = 1;
    if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN, &enter) == 0) {
        accept_is_cross = enter != 0;       /* 0: Circle confirms, 1: Cross */
    }
    return ioPadInit(7) == 0;
}

void platform_close(void)
{
    ioPadEnd();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT3);
    lv2_log_flush();
}

unsigned platform_pad(void)
{
    static unsigned held;
    sysUtilCheckCallback();
    padInfo info;
    if (ioPadGetInfo(&info) != 0) {
        return held;
    }
    unsigned now = 0;
    bool any = false;
    for (int port = 0; port < MAX_PADS; port++) {
        if (!info.status[port]) {
            continue;
        }
        padData data;
        if (ioPadGetData(port, &data) != 0 || data.len == 0) {
            /* no change on this pad since the last frame */
            continue;
        }
        any = true;
        now |= data.BTN_UP ? PAD_UP : 0;
        now |= data.BTN_DOWN ? PAD_DOWN : 0;
        now |= data.BTN_LEFT ? PAD_LEFT : 0;
        now |= data.BTN_RIGHT ? PAD_RIGHT : 0;
        now |= data.BTN_TRIANGLE ? PAD_TRIANGLE : 0;
        now |= data.BTN_START ? PAD_START : 0;
        bool cross = data.BTN_CROSS, circle = data.BTN_CIRCLE;
        now |= (accept_is_cross ? cross : circle) ? PAD_ACCEPT : 0;
        now |= (accept_is_cross ? circle : cross) ? PAD_BACK : 0;
    }
    if (any) {
        held = now;
    }
    return held;
}

bool platform_quitting(void)
{
    return quitting;
}

bool platform_accept_is_cross(void)
{
    return accept_is_cross;
}

bool platform_list_folder(const char *folder, void (*found)(void *context, const char *name), void *context)
{
    DIR *directory = opendir(folder);
    if (directory == NULL) {
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        found(context, entry->d_name);
    }
    closedir(directory);
    return true;
}
