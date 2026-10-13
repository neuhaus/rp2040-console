#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define SYSTEM_H
#define SIG_RET_TYPE void (*) (int)
#define Strcpy (void) strcpy
#define Strcat (void) strcat
#define Sprintf (void) sprintf
#ifdef RP2040_CONSOLE
void nh_srandom(unsigned int);
#define srandom nh_srandom
#endif
#define Vsprintf (void) vsprintf
#define Vprintf (void) vprintf
#define Vfprintf (void) vfprintf
