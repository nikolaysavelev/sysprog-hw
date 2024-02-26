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
execute_cd(const struct command *cmd) {
	assert(cmd != NULL);
	assert(cmd->exe != NULL);

	if (strcmp(cmd->exe, "cd") == 0) {
		if (cmd->arg_count == 1) {
			if (chdir(cmd->args[0]) == 0) {
				return 0;
			}
			else {
				if (errno == ENOENT) {
					fprintf(stderr, "cd: no such file or directory: %s\n", cmd->args[0]);
					return 1;
				}
				else {
					perror("chdir");
					exit(EXIT_FAILURE);
				}
			}
		}

		else if (cmd->arg_count > 1) {
			fprintf(stderr, "cd: too many arguments\n");
			return -1;
		}

		else if (cmd->arg_count == 0) {
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
	}
	return 0;
}

static int
execute_exit(const struct command *cmd) {
	assert(cmd != NULL);
	assert(cmd->exe != NULL);

	if (strcmp(cmd->exe, "exit") == 0) {
		if (cmd->arg_count > 1) {
			fprintf(stderr, "exit: too many arguments\n");
			return -1;
		}
		else {
			exit(EXIT_SUCCESS);
		}
	}
	return 0;
}

static void
execute_command(const struct command *cmd) {
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	else if (pid == 0) {
		if (cmd->exe == NULL) {
			exit(EXIT_FAILURE);
		}
/*
		// Отладка
		printf("execvp cmd: %s\n", cmd->exe);
		for (uint32_t i = 0; i < cmd->arg_count; ++i) {
    		printf("execvp args[%d]: %s\n", i, cmd->args[i]);
		}
		printf("execvp args size: %d\n", cmd->arg_count);
		//
*/
		char *temp[cmd->arg_count + 2];
		temp[0] = cmd->exe;

		for (uint32_t i = 0; i < cmd->arg_count; ++i)
		{
			temp[i + 1] = strdup(cmd->args[i]);
		}
		temp[cmd->arg_count + 1] = NULL;

		if (execvp(cmd->exe, temp) == -1) {
			perror("execvp");
			exit(EXIT_FAILURE);
		}
		else {
			exit(EXIT_SUCCESS);
		}
	}

	else {
		int status;
		if (waitpid(pid, &status, 0) == -1) {
			perror("waitpid");
			exit(EXIT_FAILURE);
		}
	}
}

static void
execute_command_line(const struct command_line *line)
{
	assert(line != NULL);
	const struct expr *e = line->head;
	int pipefd[2];
	int lastfd = -1;

	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {
			if (e->next && e->next->type == EXPR_TYPE_PIPE) {
				if (pipe(pipefd) == -1) {
					perror("pipe");
					exit(EXIT_FAILURE);
				}
			}

			if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "cd") == 0) {
				execute_cd(&e->cmd);
				return;
			}

			else if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "exit") == 0) {
				execute_exit(&e->cmd);
				return;
			}

			else {
				pid_t pid = fork();
				if (pid == -1) {
					perror("FORK");
					exit(EXIT_FAILURE);
				}

				else if (pid == 0) {
					if (lastfd != -1) {
						dup2(lastfd, STDIN_FILENO);
						close(lastfd);
					}

					int outfd = STDOUT_FILENO;
					if (e->next == NULL || e->next->type != EXPR_TYPE_PIPE) {
						if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
							outfd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
						}
						else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) { 
							outfd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
						}
						if (outfd == -1) {
							perror("open");
							exit(EXIT_FAILURE);
						}
						dup2(outfd, STDOUT_FILENO);
						if (outfd != STDOUT_FILENO) {
							close(outfd);
						}
					}
					execute_command(&e->cmd);
				}

				else {
					if (lastfd != -1) {
						close(lastfd);
					}
					if (e->next && e->next->type == EXPR_TYPE_PIPE) {
						lastfd = pipefd[0];
						close(pipefd[1]);
					}
					else {
						lastfd = -1;
					}
					int status;
					if (waitpid(pid, &status, 0) == -1) {
						perror("waitpid");
						exit(EXIT_FAILURE);
					}
				}	
			} 

			if (lastfd != -1) {
				close(lastfd);
			}	
		}
		e = e->next;
	}
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
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
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
