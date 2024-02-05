#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <time.h>

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct int_array {
	int *numbers;
    size_t size;
};

struct my_context {
	char *name;
	struct int_array *array;
	struct timespec start_time; 
    struct timespec end_time;
	double elapsed_time;
};

static struct my_context *
my_context_new(const char *name, struct int_array *array)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->array = array;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

static double 
get_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1.0e9;
}

int
read_file(struct my_context *ctx, struct int_array *res) {
	FILE *infile = fopen(ctx->name, "r");
	if (!infile) {
		printf("Error while opening file");
		return -1;
	}

	fseek(infile, 0, SEEK_END);
	long infile_size = ftell(infile);
	fseek(infile, 0, SEEK_SET);

	size_t size = infile_size / sizeof(int);
	printf("size in read_file: %zu", size);
	int *numbers = (int*)malloc(infile_size);
	if (numbers == NULL) {
		printf("Error: memory allocation failed\n");
        fclose(infile);
        return -1;
    }

	size_t elements_read = fread(numbers, sizeof(int), size, infile);
	if (elements_read != size) {
        printf("Error: failed to read all elements from file\n");
        fclose(infile);
		free(numbers);
        return -1;
    }

	res->numbers = numbers;
	res->size = size;

	fclose(infile);

	return 0;
}

int
write_file(int *array, size_t len) {
	FILE *outfile = fopen("outfile.txt", "w");
	if (!outfile) {
		printf("Error while opening file");
		return -1;
	}

	for (size_t i = 0; i < len; i++) {
		fprintf(outfile, "%d ", array[i]);
	}

	fclose(outfile);

	return 0;
}

static void 
quicksort(int *array, int low_idx, int high_idx) {
    if (low_idx < high_idx) {
        int ref_value = array[high_idx];
        int i = low_idx - 1;

        for (int j = low_idx; j < high_idx; j++) {
            if (array[j] < ref_value) {
                i++;

                int temp = array[i];
                array[i] = array[j];
                array[j] = temp;
            }
        }

        int temp = array[i + 1];
        array[i + 1] = array[high_idx];
        array[high_idx] = temp;

        int partitionIndex = i + 1;

        quicksort(array, low_idx, partitionIndex - 1);
        quicksort(array, partitionIndex + 1, high_idx);
    }
}

static int
quicksort_file(struct my_context *ctx) {
	clock_gettime(CLOCK_MONOTONIC, &(ctx->start_time));
	
	if (read_file(ctx, ctx->array) != 0) {
		printf("Error reading from file %s", ctx->name);
		return -1;
	}
	quicksort(ctx->array->numbers, 0, ctx->array->size - 1);

	clock_gettime(CLOCK_MONOTONIC, &(ctx->end_time));
    ctx->elapsed_time = get_elapsed_time(ctx->start_time, ctx->end_time);

	printf("Sorted %s in %.6f sec\n", ctx->name, ctx->elapsed_time);

	return 0;
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	struct coro *this = coro_this();
	struct my_context *ctx = context;
	char *name = ctx->name;

	printf("Started coroutine %s\n", name);
	clock_gettime(CLOCK_MONOTONIC, &(ctx->start_time));
	
	quicksort_file(ctx);

	clock_gettime(CLOCK_MONOTONIC, &(ctx->end_time));
	ctx->elapsed_time = get_elapsed_time(ctx->start_time, ctx->end_time);

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();

	my_context_delete(ctx);
	return 0;
}

void my_memcpy(void *_dst, void *_src, size_t n) {
  char *dst = (char *)_dst;
  char *src = (char *)_src;
  while (n) {
    *(dst++) = *(src++);
    n--;
  }
}

int int_gt_comparator(const void *a, const void *b) {
  return *(int *)a - *(int *)b;
}

void merge(
	void *left_start, void *right_start,
	size_t left_size, size_t right_size,
	size_t element_size,
	int (*comparator)(const void *, const void *),
	void *result)
{
	size_t cur_left = 0, cur_right = 0, cur_result = 0;
	while (cur_left < left_size && cur_right < right_size) {
		if (comparator(left_start + cur_left * element_size, right_start + cur_right * element_size) <= 0) {
			my_memcpy(result + cur_result * element_size, left_start + cur_left * element_size, element_size);
			cur_left++;
		} else {
			my_memcpy(result + cur_result * element_size, right_start + cur_right * element_size, element_size);
			cur_right++;
		}
		cur_result++;
	}

	while (cur_left < left_size) {
		my_memcpy(result + cur_result * element_size, left_start + cur_left * element_size, element_size);
		cur_left++;
		cur_result++;
	}

	while (cur_right < right_size) {
		my_memcpy(result + cur_result * element_size, right_start + cur_right * element_size, element_size);
		cur_right++;
		cur_result++;
	}
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
        printf("Incorrect amount of input args!\n");
        return EXIT_FAILURE;
    }

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	//struct my_context coroData[argc - 1];

	int files_num = argc - 2;
	int files_offset = 2;

	struct int_array **integers = malloc(sizeof(struct int_array) * (argc - 1));
	/* Start several coroutines. */
	for (int i = 0; i < files_num; ++i) {
		struct int_array *array = malloc(sizeof(struct int_array));
        coro_new(coroutine_func_f, my_context_new(argv[i + files_offset], array));
	}

	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	
	int *result_array = malloc(0);
	int result_length = 0;

	for (int i = 0; i < files_num; ++i) {
		int *temp = malloc((result_length + integers[i]->size) * sizeof(int));

		merge(result_array, integers[i]->numbers, result_length, integers[i]->size, sizeof(int), int_gt_comparator, temp);
		free(integers[i]->numbers);
		result_length += integers[i]->size;
		free(integers[i]);
		free(result_array);
		result_array = temp;
	}

	if (write_file(result_array, result_length) != 0) {
    	printf("Error writing to outfile");
    	return -1;
  	}

	free(result_array);
	free(integers);

    return 0;
}