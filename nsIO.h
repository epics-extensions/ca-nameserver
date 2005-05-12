#ifndef _NS_IO_H
#define _NS_IO_H
 
#include <stdio.h>

int cd_home_dir(const char* const home_dir);
void increase_process_limits();
void create_killer_script();
void create_restart_script(const char* const home_dir,const char* const pvlist_file,
	const char* const log_file,const char* const fileName);
void setup_logging(char *log_file);
void print_env_vars(FILE *fp);
void print_env(FILE *fp, const char *var);

#endif /* _NS_IO_H */
