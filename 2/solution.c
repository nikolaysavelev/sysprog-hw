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

struct pipes {
	bool is_input_pipe;
	bool is_output_pipe;
	int input_fd;
	int output_fd;
};

char**
add_cmd_name_to_args(const struct command *cmd)
{
    char **temp = malloc(sizeof(char*) * (cmd->arg_count + 2));
    temp[0] = cmd->exe;

    for (uint32_t i = 0; i < cmd->arg_count; ++i)
    {
        temp[i + 1] = strdup(cmd->args[i]);
    }
    temp[cmd->arg_count + 1] = NULL;
    
    return temp;
}

static void 
execute_cd(const struct command *cmd) { // TODO ADD EXIT CODE
	assert(cmd != NULL);
	assert(cmd->exe != NULL);

	if (strcmp(cmd->exe, "cd") == 0) {
		if (cmd->arg_count == 1) {
			if (chdir(cmd->args[0]) == 0) {
				printf("Changed directory to: %s\n", cmd->args[0]);
			}
			else {
				if (errno == ENOENT) {
					fprintf(stderr, "cd: no such file or directory: %s\n", cmd->args[0]);
				}
				else {
					perror("chdir");
					exit(EXIT_FAILURE);
				}
			}
		}

		else if (cmd->arg_count > 1) {
			fprintf(stderr, "cd: too many arguments\n");
		}

		else if (cmd->arg_count == 0) {
			if (chdir(getenv("HOME")) == 0) {
				printf("Change directory to home\n"); // А можно так? Или в точности должен быть баш?
			}
			else {
				perror("chdir");
				exit(EXIT_FAILURE);
			}
		}
		else {
			fprintf(stderr, "cd: missing arguments\n");
		}
	}
}

static void 
execute_exit(const struct command *cmd) { // TODO add exit code
	assert(cmd != NULL);
	assert(cmd->exe != NULL);

	if (strcmp(cmd->exe, "exit") == 0) {
		if (cmd->arg_count > 1) {
			fprintf(stderr, "exit: too many arguments\n");
		}
		else {
			exit(EXIT_SUCCESS);
		}
	}
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
			fprintf(stderr, "execute_command: Missing executable name\n");
			exit(EXIT_FAILURE);
		}

		// Отладка, убрать потом
		printf("execvp cmd: %s\n", cmd->exe);
		for (uint32_t i = 0; i < cmd->arg_count; ++i) {
    		printf("execvp args[%d]: %s\n", i, cmd->args[i]);
		}
		printf("execvp args size: %d\n", cmd->arg_count);
		//

		char** args = add_cmd_name_to_args(cmd);

		if (execvp(cmd->exe, args) == -1) {
			perror("execvp");
			exit(EXIT_FAILURE);
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
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

	assert(line != NULL);

	if (line->head != NULL && line->head->type == EXPR_TYPE_COMMAND && 
		strcmp(line->head->cmd.exe, "cd") == 0 && line->head->next == NULL) {
			execute_cd(&line->head->cmd);
			return;
	}

	if (line->head != NULL && line->head->type == EXPR_TYPE_COMMAND && 
		strcmp(line->head->cmd.exe, "exit") == 0 && line->head->next == NULL) {
			execute_exit(&line->head->cmd);
			return;
	}

/*
	printf("================================\n");
	printf("Command line:\n");
	printf("Is background: %d\n", (int)line->is_background);
	printf("Output: ");
	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		printf("stdout\n");
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		printf("new file - \"%s\"\n", line->out_file);
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		printf("append file - \"%s\"\n", line->out_file);
	} else {
		assert(false);
	}
	printf("Expressions:\n");
*/
	const struct expr *e = line->head;
	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {
			execute_command(&e->cmd);
		} 
		
		else if (e->type == EXPR_TYPE_PIPE) {
			int pipefd[2];

			if (pipe(pipefd) == -1) {
				perror("pipe");
				exit(EXIT_FAILURE);
			}

			pid_t ch_pid = fork();

			if (ch_pid == -1) {
				perror("fork");
				exit(EXIT_FAILURE);
			}
			else if (ch_pid == 0) {
				if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
					perror("dup2");
					exit(EXIT_FAILURE);
				}
				close(pipefd[0]);
				close(pipefd[1]);
				execute_command(&e->cmd);
				exit(EXIT_SUCCESS);
			}
			else {
				pid_t pid = fork();
				
				if (pid == 0) {
					if (dup2(pipefd[0], STDIN_FILENO) == -1) {
						perror("dup2");
						exit(EXIT_FAILURE);
					}
					close(pipefd[1]);
					close(pipefd[0]);
					execute_command(&e->cmd);
					exit(EXIT_SUCCESS);
				}
				else {
					int status;
					close(pipefd[0]);
					close (pipefd[1]);
					if (waitpid(ch_pid, &status, 0) == -1) {
						perror("waitpid");
						exit(EXIT_FAILURE);
					}
				}
			}
		} 

		else if (e->type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} 
		
		else if (e->type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} 
		
		else {
			assert(false);
			// fprintf(stderr, "command not found: %s\n", line->head->cmd.exe);
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
