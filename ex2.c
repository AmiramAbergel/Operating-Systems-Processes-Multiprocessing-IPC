/*
 * ex2.c
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <curl/curl.h>

#define HTTP_OK 200L
#define REQUEST_TIMEOUT_SECONDS 2L

#define URL_OK 0
#define URL_ERROR 1
#define URL_UNKNOWN 2

#define MAX_PROCESSES 1024

typedef struct {
		double sum;
		int amount, unknown;
} ResultStruct ;


void usage() {
	fprintf(stderr, "usage:\n\t./ex2 FILENAME NUMBER_OF_PROCESSES\n");
	exit(EXIT_FAILURE);
}

double check_url(const char *url) {
	CURL *curl;
	CURLcode res;
	double response_time = -1;

	curl = curl_easy_init();

	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT_SECONDS);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); /* do a HEAD request */

		res = curl_easy_perform(curl);
		if(res == CURLE_OK) {
			curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &response_time);
		}

		curl_easy_cleanup(curl);

	}

	return response_time;

}

void serial_checker(const char *filename) {
	
	ResultStruct results = {0};

	FILE *toplist_file;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	double res;

	toplist_file = fopen(filename, "r");

	if (toplist_file == NULL) {
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&line, &len, toplist_file)) != -1) {
		if (read == -1) {
			perror("unable to read line from file");
		}
		line[read-1] = '\0'; /* null-terminate the URL */
		if (-1 == (res = check_url(line))) {
			results.unknown++;
		} else {
			results.sum += res;
			results.amount++;
		}
	}

	free(line);
	fclose(toplist_file);

	printf("%.4f Average response time from %d sites, %d Unknown\n",
			results.sum / results.amount,
			results.amount,
			results.unknown);
}

/**
 * @define - handle single worker that run on child process
 */
void worker_checker(const char *filename, int pipe_write_fd, int worker_id, int workers_number) {
	/*
	 * TODO: this checker function should operate almost like serial_checker(), except:
	 * 1. Only processing a distinct subset of the lines (hint: think Modulo)
	 * 2. Writing the results back to the parent using the pipe_write_fd (i.e. and not to the screen)
	 */

	ResultStruct results = {0};

	double res;
	FILE *toplist_file;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int line_number = 0;

	toplist_file = fopen(filename, "r");

	if (toplist_file == NULL) {
		exit(EXIT_FAILURE);
	}

	// Looping over all the lines 
	while ((read = getline(&line, &len, toplist_file)) != -1) {

		if (read == -1) {
			perror("unable to read line from file");
		}

		line[read - 1] = '\0'; /* null-terminate the URL */
		if (line_number % workers_number == worker_id) {
			if (-1 == (res = check_url(line))) {
				results.unknown++;
			}
			else {
				results.sum += res;
				results.amount++;
			}
		}
		line_number++;
	}
	if (write(pipe_write_fd, &results, sizeof(ResultStruct)) == -1) {
		perror("Problems cant writing to the pipe");
		exit(EXIT_FAILURE);
	}

	if (close(pipe_write_fd) == -1) {
		perror("Problem cant closing the pipe");
		exit(EXIT_FAILURE);
	}

	free(line);
	fclose(toplist_file);
}

/**
 * Handle separate the work between process and merge the results
 */
void parallel_checker(const char *filename, int number_of_processes) {
	int worker_id;
	int pipefd[2];

	ResultStruct results = {0};
	ResultStruct results_buffer = {0};

	// initialize  pipe
	pipe(pipefd);

	// Start number_of_processes new workers
	for (worker_id = 0; worker_id  < number_of_processes; ++worker_id ) {

		pid_t x = fork();
		if (x == -1) {
			perror("Problem read fork");
			exit(EXIT_FAILURE);
		}
		if (x == 0) {
			if (close(pipefd[0] == -1)) {
				perror("Problem cant reading side of the pipe");
				exit(EXIT_FAILURE);
			}
			worker_checker(filename, pipefd[1], worker_id, number_of_processes);
			exit(EXIT_SUCCESS);
		}
	}
	
	pid_t wpid;
	int status = 0;
	while ((wpid = wait(&status)) > 0);

	for (worker_id = 0; worker_id  < number_of_processes; ++worker_id ) {
		//Sum the result
		close(pipefd[1]);
		if (read(pipefd[0], &results_buffer, sizeof(ResultStruct)) == -1) {
			perror("Problen to read from pipe");
			exit(EXIT_FAILURE);
		}

		results.sum += results_buffer.sum;
		results.amount += results_buffer.amount;
		results.unknown += results_buffer.unknown;
	}

	// print the total results
	printf("%.4f Average response time from %d sites, %d Unknown\n",
				results.sum / results.amount,
				results.amount,
				results.unknown);
}

int main(int argc, char **argv) {
	if (argc != 3) {
		usage();
	} else if (atoi(argv[2]) == 1) {
		serial_checker(argv[1]);
	} else parallel_checker(argv[1], atoi(argv[2]));

	return EXIT_SUCCESS;
}
