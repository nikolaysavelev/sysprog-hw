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

static void 
execute_cd(const struct command *cmd) {
	assert(cmd != NULL);
	assert(cmd->exe != NULL);

	if (strcmp(cmd->exe, "cd") == 0) {
		if (cmd->arg_count == 1) { // 1 argument, valid
			if (chdir(cmd->args[0]) == 0) {
				printf("Changed directory to: %s\n", cmd->args[0]);
			}
			else {
				if (errno == ENOENT) { // no such file or dir
					fprintf(stderr, "cd: no such file or directory: %s\n", cmd->args[0]);
				}
				else {
					perror("chdir");
				}
			}
		}
		else if (cmd->arg_count > 1) { // > 1 args, invalid
			fprintf(stderr, "cd: too many arguments\n");
		}
		else if (cmd->arg_count == 0) { // switch to home dir
			if (chdir(getenv("HOME")) == 0) {
				printf("Changed directory to home\n");
			} 
			else {
				perror("chdir");
			}
		}
		else {
			fprintf(stderr, "cd: missing arguments\n");
		}
	}
}

static void 
execute_exit(const struct command *cmd) {
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
		if (execvp(cmd->exe, cmd->args) == -1) {
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
			printf("\tCommand: %s", e->cmd.exe);
			execute_command(&e->cmd);
			printf("\n");
		} else if (e->type == EXPR_TYPE_PIPE) {
			printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} else if (e->type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} else {
			assert(false);
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
