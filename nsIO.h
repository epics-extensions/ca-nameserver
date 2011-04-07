#ifndef _NS_IO_H
#define _NS_IO_H
 
#include "epicsTime.h"

#define DEBUG 4
#define VERBOSE 3
#define INFO 2
#define WARNING 1
#define WARN 1
#define ERROR 0


int cd_home_dir(const char* const home_dir);
void increase_process_limits();
void create_restart_script();
void create_killer_script(pid_t parent_pid, const char* const home_dir,const char* const pvlist_file,
	const char* const log_file,const char* const fileName);
void setup_logging(char *log_file);
void print_env_vars(FILE *fd);
void print_env(FILE *fp, const char *var);

void set_log_level(int level);
void log_message(int level,const char* fmt,...);

#endif /* _NS_IO_H */

