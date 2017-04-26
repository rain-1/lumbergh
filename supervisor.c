#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/prctl.h>

#define MAXSUBPROCS 1024

struct process {
	int garbage;
	
	char *name;
	char *run;
	char *stdout;
	char *stderr;
	
	pid_t pid;
};

struct process children[MAXSUBPROCS];
int num_children = 0;

char *enabled_services[MAXSUBPROCS];
int num_enabled_services;

int disabled_services[MAXSUBPROCS];
int num_disabled_services;

int scan_directory(char *dir);
void mark_for_supervision(char *dir, char *nm);
void disable_process(int i);
void check_on_process(struct process *p);
void launch_process(struct process *p);

int main(int argc, char **argv) {
	pid_t pid;

	if(argc != 2) {
		printf("Usage %s <directory>", argv[0]);
		return -1;
	}

	fprintf(stdout, "Supervising directory <%s>\n", argv[1]);

	setsid();

 	if(scan_directory(argv[1])) {
		fprintf(stdout, "Could not scan directory.\n");
		return -1;
	}

	while(1) {
		for(int i = 0; i < num_children; i++) {
			if(!children[i].garbage)
				check_on_process(&children[i]);
		}
		
		sleep(1);
		scan_directory(argv[1]);
	}
	
	return 0;
}

int scan_directory(char *dir) {
	// The algorithm here for a rescan is based on 'diffing'
	// what services we currently have against what we see
	// in the enabled directory.
	//
	// To do that we start with an empty list of newly 'enabled'
	// services which will be added to, and a full list of
	// 'disabled' services which will be removed from.
	
	DIR *d;
	struct dirent *ent;

	num_enabled_services = 0;
	num_disabled_services = num_children;
	for(int i = 0; i < num_children; i++) {
		if(!children[i].garbage) {
			disabled_services[i] = i;
		}
		else {
			disabled_services[i] = -1;
		}
	}
	
	if(!(d = opendir(dir))) return 1;
	while(ent = readdir(d)) {
		if(ent->d_type == DT_DIR) {
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			// check if the directory is in our list
			// if yes remove it from disabled
			// if no add it to enabled

			int seen = 0;
			
			for (int i = 0; i < num_children; i++) {
				if (children[i].garbage)
					continue;
				
				if(!strncmp(children[i].name, ent->d_name, 1024)) {
					disabled_services[i] = -1;
					seen = 1;
					break;
				}
			}

			if(!seen) {
				enabled_services[num_enabled_services] = ent->d_name;
				num_enabled_services++;
			}
		}
	}

	// supervise all newly enabled services
	// take down all disabled services

	// mark disabled children as garbage
	for (int i = 0; i < num_disabled_services; i++) {
		if(disabled_services[i] != -1) {
			fprintf(stdout, "Disabling service <%s>\n", children[i].name);
			disable_process(i);
		}
		
	}

	// supervise new processes
	for (int i = 0; i < num_enabled_services; i++) {
		fprintf(stdout, "Enabling service <%s>\n", enabled_services[i]);
		mark_for_supervision(dir, enabled_services[i]);
	}
	
	closedir(d);
	
	return 0;
}

void mark_for_supervision_at_index(char *dir, char *nm, int idx) {
	char run_buf[1024];
	char stdout_buf[1024];
	char stderr_buf[1024];
	
	snprintf(run_buf, sizeof(run_buf), "%s/%s/%s", dir, nm, nm);
	snprintf(stdout_buf, sizeof(stdout_buf), "%s/%s/stdout.log", dir, nm);
	snprintf(stderr_buf, sizeof(stderr_buf), "%s/%s/stderr.log", dir, nm);

	children[idx].garbage = 0;
	children[idx].name = strdup(nm);
	children[idx].run = strdup(run_buf);
	children[idx].stdout = strdup(stdout_buf);
	children[idx].stderr = strdup(stderr_buf);
	children[idx].pid = 0;
}

void mark_for_supervision(char *dir, char *nm) {
	for(int i = 0; i < num_children; i++) {
		if(children[i].garbage) {
			mark_for_supervision_at_index(dir, nm, i);
			return;
		}
	}
	
        mark_for_supervision_at_index(dir, nm, num_children);
	num_children++;
}

void disable_process(int i) {
	children[i].garbage = 1;

	free(children[i].name);
	free(children[i].run);
	free(children[i].stdout);
	free(children[i].stderr);

	children[i].name = NULL;
	children[i].run = NULL;
	children[i].stdout = NULL;
	children[i].stderr = NULL;

	kill(-children[i].pid, SIGKILL);
	waitpid(children[i].pid, NULL, 0);
	
	children[i].pid = 0;
}

void check_on_process(struct process *p) {
	pid_t ret;
	int status;

	if(!p->pid) {
		launch_process(p);
		return;
	}

	ret = waitpid(p->pid, &status, WNOHANG);
	if(ret == p->pid) {
		if(WIFEXITED(status)) {
			fprintf(stdout, "<%s> exited with status %d\n", p->name, WEXITSTATUS(status));
			launch_process(p);
		}
		return;
	}

	if(kill(p->pid, 0)) {
		fprintf(stdout, "<%s> died!\n", p->name);
		launch_process(p);
		return;
	}
}

void launch_process(struct process *p) {
	pid_t pid;
	int fd1, fd2;
	
	fprintf(stdout, "launching process <%s>\n", p->name);
	
	fd1 = open(p->stdout, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
	fd2 = open(p->stderr, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
	
	if(fd1 == -1 || fd2 == -1) {
		fprintf(stdout, "Critical Error: Unable to log for program <%s>.\n", p->name);
		return;
	}
	
	pid = fork();
	if(pid == -1) {
		fprintf(stdout, "Critical Error: Unable to fork.\n");
		return;
	}
	else if(pid) {
		// parent
		p->pid = pid;
		fprintf(stdout, "<%s> started with pid %d.\n", p->name, pid);

		close(fd1);
		close(fd2);
		
		return;
	}
	else {
		setpgrp();
		
		dup2(fd1, 1);
		dup2(fd2, 2);
		
		close(fd1);
		close(fd2);
		
		execl(p->run, p->run, NULL);
		
		// TODO what if it cant be run?
		
		exit(0);
	}
}
