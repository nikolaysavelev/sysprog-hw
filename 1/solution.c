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

struct int_array
{
	int *numbers;
	size_t size;
};

struct my_context
{
	char *name;
	struct int_array *array;
	struct timespec start_time;
	struct timespec end_time;
	int context_switch_count;
	double elapsed_time;
};

static struct my_context *
my_context_new(const char *name, struct int_array *array)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->array = array;
	ctx->context_switch_count = 0;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

static double
get_elapsed_time(struct timespec start, struct timespec end)
{
	return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1.0e9;
}

/*
Пояснения к сортировке:
У нас есть предмет "Углубленный C", на котором мы реализовывали вручную mergesort для разных типов данных.
Компаратор, функции my_memcpy, merge и mergesort взяты оттуда.
*/

int int_gt_comparator(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

void my_memcpy(void *_dst, void *_src, size_t n)
{
	char *dst = (char *)_dst;
	char *src = (char *)_src;

	while (n)
	{
		*(dst++) = *(src++);
		n--;
	}
}

void merge(
	void *left_start, void *right_start,
	size_t left_size, size_t right_size,
	size_t element_size,
	int (*comparator)(const void *, const void *),
	void *result)
{
	size_t cur_left = 0, cur_right = 0, cur_result = 0;
	while (cur_left < left_size && cur_right < right_size)
	{
		if (comparator(left_start + cur_left * element_size, right_start + cur_right * element_size) <= 0)
		{
			my_memcpy(result + cur_result * element_size, left_start + cur_left * element_size, element_size);
			cur_left++;
		}
		else
		{
			my_memcpy(result + cur_result * element_size, right_start + cur_right * element_size, element_size);
			cur_right++;
		}
		cur_result++;
	}

	while (cur_left < left_size)
	{
		my_memcpy(result + cur_result * element_size, left_start + cur_left * element_size, element_size);
		cur_left++;
		cur_result++;
	}

	while (cur_right < right_size)
	{
		my_memcpy(result + cur_result * element_size, right_start + cur_right * element_size, element_size);
		cur_right++;
		cur_result++;
	}
}

int read_file(struct my_context *ctx, struct int_array *res)
{
	FILE *infile = fopen(ctx->name, "r");
	if (!infile)
	{
		printf("Error while opening file");
		return -1;
	}

	size_t cap = 15, size = 0;
	int *numbers = malloc(cap * sizeof(int));
	
	int number;
	while (fscanf(infile, "%d", &number) == 1) {
		numbers[size++] = number;

		if (size >= cap) {
			cap *= 2;
			int* temp = realloc(numbers, cap * sizeof(int));
			if (temp == NULL) {
				printf("Error allocating memory\n");
				free(numbers);
				fclose(infile);
				return -1;
			}
			numbers = temp;
		}
	}

	res->numbers = numbers;
	res->size = size;

	fclose(infile);

	return 0;
}

int write_file(int *array, size_t len)
{
	FILE *outfile = fopen("outfile.txt", "w");
	if (!outfile)
	{
		printf("Error while opening file");
		return -1;
	}

	for (size_t i = 0; i < len; i++)
	{
		fprintf(outfile, "%d ", array[i]);
	}

	fclose(outfile);

	return 0;
}

int mergesort(
	void *array,
	size_t elements,
	size_t element_size,
	int (*comparator)(const void *, const void *))
{

	if (elements <= 1)
	{
		return 0;
	}

	size_t middle = elements / 2;
	void *left = array;
	void *right = (char *)array + middle * element_size;

	int mergesort_res;
	mergesort_res = mergesort(left, middle, element_size, comparator);
	if (mergesort_res == -1)
	{
		return -1;
	}
	coro_yield();

	mergesort(right, elements - middle, element_size, comparator);
	if (mergesort_res == -1)
	{
		return -1;
	}
	coro_yield();

	void *temp = malloc(elements * element_size);
	if (!temp)
	{
		return -1;
	}
	merge(left, right, middle, elements - middle, element_size, comparator, temp);
	my_memcpy(array, temp, elements * element_size);
	free(temp);

	return 0;
}

static int
mergesort_file(struct my_context *ctx)
{
	clock_gettime(CLOCK_MONOTONIC, &(ctx->start_time));

	if (read_file(ctx, ctx->array) != 0)
	{
		printf("Error reading from file %s", ctx->name);
		return -1;
	}

	mergesort(ctx->array->numbers, ctx->array->size, sizeof(int), int_gt_comparator);

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

	clock_gettime(CLOCK_MONOTONIC, &(ctx->start_time));

	mergesort_file(ctx);

	clock_gettime(CLOCK_MONOTONIC, &(ctx->end_time));
	ctx->elapsed_time = get_elapsed_time(ctx->start_time, ctx->end_time);
	ctx->context_switch_count += coro_switch_count(this);
	printf("%s: yield\n", name);

	my_context_delete(ctx);

	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("Incorrect amount of input args!\n");
		return EXIT_FAILURE;
	}

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &(start));

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();

	int files_num = argc - 1;
	int files_offset = 1;

	struct int_array **integers = malloc(sizeof(struct int_array) * (argc - 1));
	/* Start several coroutines. */
	for (int i = 0; i < files_num; ++i)
	{
		struct int_array *array = malloc(sizeof(struct int_array));
		coro_new(coroutine_func_f, my_context_new(argv[i + files_offset], array));
		integers[i] = array;
	}

	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL)
	{
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished, code: %d, switched coro: %lld\n", coro_status(c), coro_switch_count(c));
		printf("==========\n");
		coro_delete(c);
	}

	int *result_array = malloc(0);
	int result_length = 0;

	for (int i = 0; i < files_num; ++i)
	{
		int *temp = malloc((result_length + integers[i]->size) * sizeof(int));

		merge(result_array, integers[i]->numbers, result_length, integers[i]->size, sizeof(int), int_gt_comparator, temp);
		free(integers[i]->numbers);
		result_length += integers[i]->size;
		free(integers[i]);
		free(result_array);
		result_array = temp;
	}

	if (write_file(result_array, result_length) != 0)
	{
		printf("Error writing to outfile");
		return -1;
	}

	free(result_array);
	free(integers);

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &(end));
	printf("Total time: %.6f sec\n", get_elapsed_time(start, end));

	return 0;
}