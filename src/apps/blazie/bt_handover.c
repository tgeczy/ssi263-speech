/* bt_handover.c -- blazie_emu on a BT Speak or BT Braille hands over to the BT frontend (bt_handover.h). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "bt_handover.h"

#define PROBE_MS 5000

/* The device's own library and keyboard service, as the frontend uses them; any failure is "no". */
static const char PROBE[] =
    "import sys\n"
    "try:\n"
    "    from BTSpeak import kb_client\n"
    "    sys.exit(0 if kb_client.server_available() else 1)\n"
    "except Exception:\n"
    "    sys.exit(1)\n";

int bt_detect(void)
{
    const char *force = getenv("BLAZIE_BT_DETECT");
    struct timespec step = {0, 10 * 1000000L};
    int status, waited, fd;
    pid_t pid;
    if (force && *force)
        return strcmp(force, "0") != 0;
    pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
        }
        execlp("python3", "python3", "-c", PROBE, (char *)NULL);
        _exit(127);
    }
    for (waited = 0; waited < PROBE_MS; waited += 10) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (r < 0)
            return 0;
        nanosleep(&step, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 0;
}

int bt_frontend(char *out, size_t cap)
{
    char dir[PATH_MAX], worker[PATH_MAX + 16], *slash;
    ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 1);
    if (n <= 0)
        return 0;
    dir[n] = 0;
    if ((slash = strrchr(dir, '/')) == NULL)
        return 0;
    *slash = 0;
    snprintf(out, cap, "%s/blazie_emu_bt", dir);
    snprintf(worker, sizeof worker, "%s/blazie_bt", dir);
    return access(out, X_OK) == 0 && access(worker, X_OK) == 0;
}

void bt_hand_over(int no_bt, const char *setting, const char *unit, const char *firmware, const char *state_dir,
                  int rate)
{
    char exe[PATH_MAX], rate_text[16];
    const char *argv[10];
    int n = 0;
    if (getenv("BLAZIE_BT_BREAK")) {           /* the tests' control: --no-bt and the setting ignored, --unit lost */
        no_bt = 0;
        setting = "auto";
        unit = NULL;
    }
    /* native: blazie_emu uses the device's keyboard and display itself; off: the terminal only */
    if (no_bt || !strcmp(setting, "off") || !strcmp(setting, "0") || !strcmp(setting, "native") || !bt_detect())
        return;
    if (!bt_frontend(exe, sizeof exe)) {
        printf("BT Speak or BT Braille detected, but blazie_emu_bt is not installed beside blazie_emu: "
               "using the device's keyboard and display from blazie_emu itself.\n");
        fflush(stdout);
        return;
    }
    argv[n++] = exe;
    if (unit) {
        argv[n++] = "--unit";
        argv[n++] = unit;
    }
    if (firmware) {
        argv[n++] = "--firmware";
        argv[n++] = firmware;
    }
    if (state_dir) {
        argv[n++] = "--state-dir";
        argv[n++] = state_dir;
    }
    if (rate) {
        snprintf(rate_text, sizeof rate_text, "%d", rate);
        argv[n++] = "--rate";
        argv[n++] = rate_text;
    }
    argv[n] = NULL;
    printf("BT Speak or BT Braille detected: using its keyboard, speech and braille display.\n");
    fflush(stdout);
    execv(exe, (char *const *)argv);
    printf("Could not start %s (%s): running in the terminal instead.\n", exe, strerror(errno));
    fflush(stdout);
}
