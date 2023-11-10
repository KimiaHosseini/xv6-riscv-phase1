//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

#define MAX_HISTORY 16

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c) {
    if (c == BACKSPACE) {
        // if the user typed backspace, overwrite with a space.
        uartputc_sync('\b');
        uartputc_sync(' ');
        uartputc_sync('\b');
    } else {
        uartputc_sync(c);
    }
}

struct {
  struct spinlock lock;
  
  // input
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

struct {
    char bufferArr[MAX_HISTORY][INPUT_BUF_SIZE]; //holds the actual command strings
    uint lengthsArr[MAX_HISTORY]; //hold the length of each command string
    uint lastCommandIndex; //the index of last command entered to history
    int numOfCommandsInMem; //num of history commands in mem
    int currentHistory; //holds the current history view -> displacement from the last command index
} historyBufferArray;

int checkIndex(int index);

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

void shiftAndAdd(char arr[MAX_HISTORY][INPUT_BUF_SIZE], const char *newValue) {
    for (int i = 1; i < MAX_HISTORY; i++) {
        strncpy(arr[i-1], arr[i], INPUT_BUF_SIZE);
    }
    strncpy(arr[MAX_HISTORY - 1], newValue, INPUT_BUF_SIZE);
    historyBufferArray.lengthsArr[MAX_HISTORY - 1] = strlen(newValue);
    historyBufferArray.currentHistory = (int)historyBufferArray.lastCommandIndex;
}

void
clearCurrentLine(void){
    uint numToClear = cons.e - cons.r;
    for (uint i = 0; i < numToClear; i++) {
        consputc(BACKSPACE);
    }
    cons.e = cons.w;
}

void replaceHistoryCommand(uint index)
{
    if(index < 0 || index >= historyBufferArray.lastCommandIndex)
        return;

    clearCurrentLine();

    for (int i = 0; i < historyBufferArray.lengthsArr[index]; i++) {
        char c = historyBufferArray.bufferArr[index][i];
        consputc(c);
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
    }
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

    switch (c) {
        case C('P'):  // Print process list.
            procdump();
            break;
        case C('U'):  // Kill line.
            while (cons.e != cons.w &&
                   cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
        case C('H'): // Backspace
        case '\x7f': // Delete key
            if (cons.e != cons.w) {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
        case '\033':
            c = uartgetc();
            if (c == '[') {
                c = uartgetc();
                if (c == 'A') {
                    replaceHistoryCommand(historyBufferArray.currentHistory);
                    if(historyBufferArray.currentHistory != 0)
                        historyBufferArray.currentHistory--;
                    break;
                } else if (c == 'B') {
                    replaceHistoryCommand(historyBufferArray.currentHistory);
                    if(historyBufferArray.currentHistory != historyBufferArray.lastCommandIndex - 1)
                        historyBufferArray.currentHistory++;
                    break;
                }
            }
            break;
        default:
            if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
                char command[INPUT_BUF_SIZE];
                c = (c == '\r') ? '\n' : c;

                // echo back to the user.
                consputc(c);

                // store for consumption by consoleread().
                cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
                if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
                    // wake up consoleread() if a whole line (or end-of-file)
                    // has arrived.
                    consputc(1);

                    for(int i=cons.w, k=0; i < cons.e-1; i++, k++){
                        command[k] = cons.buf[i % INPUT_BUF_SIZE];
                    }
                    command[(cons.e-1-cons.w) % INPUT_BUF_SIZE] = '\0';

                    if(strncmp(command, "history", 7) != 0){
                        if(historyBufferArray.lastCommandIndex == MAX_HISTORY)
                            shiftAndAdd(historyBufferArray.bufferArr, command);
                        else{
                            uint index = historyBufferArray.lastCommandIndex;
                            strncpy(historyBufferArray.bufferArr[index],command, INPUT_BUF_SIZE);
                            historyBufferArray.lengthsArr[index] = strlen(command);
                            historyBufferArray.currentHistory = (int)historyBufferArray.lastCommandIndex;
                            historyBufferArray.lastCommandIndex = historyBufferArray.lastCommandIndex + 1;
                            if(historyBufferArray.numOfCommandsInMem < 16)
                                historyBufferArray.numOfCommandsInMem++;
                        }
                    }
                    cons.w = cons.e;
                    wakeup(&cons.r);
                }
            }
            break;
    }

    release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}

int sys_history(void){
    uint64 buffer;
    int historyId;
    argint(1, &historyId);
    if(!checkIndex(historyId)){
        return -1;
    }
    argaddr(0, &buffer);
    struct proc *p = myproc();
    copyout(p->pagetable, buffer, historyBufferArray.bufferArr[historyId], INPUT_BUF_SIZE);
    return 0;
}

int checkIndex(int index){
    if (index< 0 || index >= historyBufferArray.lastCommandIndex)
        return 0;
    return 1;
}