#ifndef _RESERVE_FD_H_
#define _RESERVE_FD_H_

#include <stdio.h>

FILE *reserve_fd_fopen(const char *filename, const char *mode);
int reserve_fd_fclose(FILE *stream);

#endif /* _RESERVE_FD_H_ */

