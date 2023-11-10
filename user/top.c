#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/spinlock.h"
#include "kernel/riscv.h"
#include <kernel/proc.h>
#include "user.h"

char * getState(enum procstate procstate){
    switch (procstate) {
        case 0:
            return "UNUSED";
        case 1:
            return "USED";
        case 2:
            return "SLEEPING";
        case 3:
            return "RUNNABLE";
        case 4:
            return "RUNNING";
        case 5:
            return "ZOMBIE";
    }
    return "";
}

int main() {
    struct top topstruct;
    top(&topstruct);
    printf("uptime: %d seconds\n", topstruct.uptime);
    printf("total: %d\n", topstruct.total_process);
    printf("running: %d\n", topstruct.running_process);
    printf("sleeping: %d\n", topstruct.sleeping_process);
    printf("        pid         name            ppid            state\n");
    for (int i = 0; i < topstruct.total_process; ++i) {
        printf("        %d         %s            %d            %s\n", topstruct.p_list[i].pid, topstruct.p_list[i].name,
               topstruct.p_list[i].ppid, getState(topstruct.p_list[i].state));
    }
    return 0;
}