#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

#define MAX_HISTORY                 (16)      /*the max number of the comand histories*/
#define MAX_COMMAND_LENGTH          (128)     /*the max length of the comand*/
int main(int argc, char* argv[]) {
    if(argc != 2){
        printf("Invalid index");
        exit(-1);
    }
    char buffer[MAX_COMMAND_LENGTH];
    for (int i = 0; i < MAX_HISTORY; ++i) {
        memset(buffer, 0, 128 * sizeof(char ));
    }
    char res[MAX_COMMAND_LENGTH];
    int targetIndex = atoi(argv[1]);
    int state = history(res, targetIndex);
    if(state != 0){
        printf("Invalid index");
        exit(-1);
    }
    for (int i = 0; i < MAX_HISTORY; ++i) {
        state = history(buffer, i);
        if(state != 0)
            break;
        printf("%s\n",buffer);
    }
    printf("requested command: %s\n", res);
    exit(0);
}