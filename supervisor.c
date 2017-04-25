#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>

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

int scan_directory(char *dir);
void mark_for_supervision(char *dir, char *nm);
void check_on_process(struct process *p);
void launch_process(struct process *p);

int main(int argc, char **argv) {
	pid_t pid;

	if(argc != 2) {
		puts("Usage ./t <directory>");
		return -1;
	}

	fprintf(stdout, "Supervising directory <%s>\n", argv[1]);
	
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
	}
	
	return 0;
}

int scan_directory(char *dir) {
	DIR *d;
	struct dirent *ent;
	
	if(!(d = opendir(dir))) return 1;
	while(ent = readdir(d)) {
		if(ent->d_type == DT_DIR) {
			if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;
			
			fprintf(stdout, "Supervising program <%s>\n", ent->d_name);
		        mark_for_supervision(dir, ent->d_name);
		}
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
		dup2(fd1, 1);
		dup2(fd2, 2);
		
		close(fd1);
		close(fd2);
		
		execl(p->run, p->run, NULL);
		
		// TODO what if it cant be run?
		
		exit(0);
	}
}
