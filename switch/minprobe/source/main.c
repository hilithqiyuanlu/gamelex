// Minimal C-only sysmodule used to separate NPDM/loader problems from the
// C++ runtime. It logs two lines and then stays resident.
#include <switch.h>
#include <stdio.h>
#include <sys/stat.h>

u32 __nx_applet_type = AppletType_None;

extern char* fake_heap_start;
extern char* fake_heap_end;
static char mini_heap[1024 * 1024];

void __libnx_initheap(void) {
    fake_heap_start = mini_heap;
    fake_heap_end = mini_heap + sizeof(mini_heap);
}

static void log_line(const char* text) {
    FILE* file = fopen("sdmc:/minprobe.log", "a");
    if (!file) return;
    fputs(text, file);
    fputc('\n', file);
    fclose(file);
    fsdevCommitDevice("sdmc");
}

void __appInit(void) {
    // Re-assert the heap before any service/stdio allocation.
    fake_heap_start = mini_heap;
    fake_heap_end = mini_heap + sizeof(mini_heap);
    if (R_FAILED(smInitialize()) || R_FAILED(fsInitialize()) || R_FAILED(fsdevMountSdmc()))
        svcExitProcess();
    mkdir("sdmc:/config", 0777);
    log_line("appInit");
}

void __appExit(void) {
    log_line("appExit");
    fsdevUnmountAll();
    fsExit();
    smExit();
}

int main(void) {
    log_line("main");
    while (true) {
        svcSleepThread(1000000000ULL);
    }
}
