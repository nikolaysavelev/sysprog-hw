#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

static int
execute_cd(const struct expr *e) {
	if (e->cmd.arg_count == 1) {
		if (chdir(e->cmd.args[0]) == 0) {
			return 0;
		}
		else {
			if (errno == ENOENT) {
				fprintf(stderr, "cd: no such file or directory: %s\n", e->cmd.args[0]);
				return 1;
			}
			else {
				perror("chdir");
				exit(EXIT_FAILURE);
			}
		}
	}

	else if (e->cmd.arg_count > 1) {
		fprintf(stderr, "cd: too many arguments\n");
		return -1;
	}

	else if (e->cmd.arg_count == 0) {
		if (chdir(getenv("HOME")) == 0) {
			return 0;
		}
		else {
			perror("chdir");
			exit(EXIT_FAILURE);
		}
	}

	else {
		fprintf(stderr, "cd: missing arguments\n");
		return 0;
	}
	return 0;
}

void
execute_exit(const struct expr *e) {
	if (e->cmd.arg_count == 0) { 
		exit(0);
	}

	exit(atoi(e->cmd.args[0]));
}

static void
execute_command(const struct expr *e) {
/*
		// Отладка
		printf("execvp cmd: %s\n", cmd->exe);
		for (uint32_t i = 0; i < cmd->arg_count; ++i) {
    		printf("execvp args[%d]: %s\n", i, cmd->args[i]);
		}
		printf("execvp args size: %d\n", cmd->arg_count);
		//
*/

	if (!strcmp(e->cmd.exe, "exit")) {
		execute_exit(e);
		return;
	}

	char *temp[e->cmd.arg_count + 2];
	temp[0] = e->cmd.exe;

	for (uint32_t i = 0; i < e->cmd.arg_count; ++i) {
		temp[i + 1] = e->cmd.args[i];
	}
	temp[e->cmd.arg_count + 1] = NULL;

	execvp(e->cmd.exe, temp);
}

static int
execute_command_line(const struct command_line *line)
{
	int pipefd[2];
	int proc_wait = 1;
	int pid;
	int last_status = 0;
	int pipe_stdin = 0;
	const struct expr *e = line->head;

	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {
			if (e->next && e->next->type == EXPR_TYPE_PIPE) {
				if (pipe(pipefd) == -1) {
					perror("pipe");
					exit(EXIT_FAILURE);
				}
			}

			if (!strcmp(e->cmd.exe, "exit") && !e->next) {
				for (int i = 0; i < proc_wait; ++i) {
					int status;
					int wait_pid = wait(&status);
					if (wait_pid == pid) {
						last_status = WEXITSTATUS(status);
					}
				}

				if (e->cmd.arg_count == 0) {
					last_status = 0;
				} 
				else {
					last_status = atoi(e->cmd.args[0]);
				}

				if (e == line->head) {
					exit(last_status);
				}

				return last_status;
			} 
			
			else if (strcmp(e->cmd.exe, "cd") == 0) {
				execute_cd(e);
			}
			
			else {
				pid = fork();
				if (pid == -1) {
					printf("HERE 1");
					perror("FORK");
					exit(EXIT_FAILURE);
				}

				else if (pid == 0) {
					if (pipe_stdin != 0) {
						dup2(pipe_stdin, STDIN_FILENO);
						close(pipe_stdin);
						pipe_stdin = 0;
					}

					int outfd = STDOUT_FILENO;
					if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
						outfd = open(line->out_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
					}
					if (line->out_type == OUTPUT_TYPE_FILE_APPEND) { 
						outfd = open(line->out_file, O_APPEND | O_RDWR | O_APPEND, 0644);
					}
					if (e->next && e->next->type == EXPR_TYPE_PIPE) {
						close(pipefd[0]);
						outfd = pipefd[1];
					}
					if (outfd != STDOUT_FILENO) {
						dup2(outfd, STDOUT_FILENO);
						close(outfd);
					}
					execute_command(e);
				}

				if (pipe_stdin) {
					close(pipe_stdin);
				}

				if (e->next && e->next->type == EXPR_TYPE_PIPE) {
					pipe_stdin = pipefd[0]; 
					close(pipefd[1]);
				}

				proc_wait++;
			}
		} else if (e->type == EXPR_TYPE_PIPE) {
			// printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
			// printf("\tAND\n");
		} else if (e->type == EXPR_TYPE_OR) {
			// printf("\tOR\n");
		}
		else {
			assert(false);
		}
		e = e->next;
	}

	for (int i = 0; i < proc_wait; ++i) {
		int status;
		int wait_pid = wait(&status);
		if (wait_pid == pid) {
			last_status = WEXITSTATUS(status);
		}
	}

	return last_status;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	int last_status = 0;
	struct parser *p = parser_new();

	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			last_status = execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return last_status;
}
