#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <regex.h>
#include <fcntl.h>

#define PORT 8082
#define BUFFER_SIZE 2048
#define PAGE_NOT_FOUND "404.html"
#define HTTP_GET_REGEX "GET ([^ ]+) HTTP/1.1"
#define HTTP_404_RESPONSE "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n404 Not Found"
#define HTTP_200_RESPONSE "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))

void error(const char *msg) {
  perror(msg);
  exit(1);
}

char* copy_string(const char *source) {
  char *target = (char*)malloc(strlen(source) * sizeof(char) + 1);

  for(int i = 0; i < strlen(source); i++) {
    target[i] = source[i];
  }

  target[strlen(source)] = '\0';

  return target;
}

int is_hex(char x) {
	return (x >= '0' && x <= '9')	||
		(x >= 'a' && x <= 'f')	||
		(x >= 'A' && x <= 'F');
}

int url_decode(const char *url, char *output) {
	char *res;
	const char *end = url + strlen(url);
	int c;

	for(res = output; url <= end; res++) {
		c = *url++;
		if (c == '+') c = ' ';
		else if (c == '%' && (!is_hex(*url++) || !is_hex(*url++) || !sscanf(url - 2, "%2x", &c)))
			return -1;

		if(output) *res = c;
	}

	return res - output;
}

char* get_page_for(const char *path) {
  char *routes[] = {"/index", "/path1", "/path2"};
  char *pages[] = {"index.html", "path1.html", "path2.html"};

  for(int i = 0; i < ARRAY_LENGTH(routes); i++) {
    if(strcmp(path, routes[i]) == 0) {
      return pages[i];
    }
  }

  return NULL;
}

void build_http_response(const char *path, char *response, size_t *response_len) {
  const char *page = get_page_for(path);
  ssize_t bytes_read;
  int page_fd;
  const char* header_content;
  if(page == NULL) {
    header_content = HTTP_404_RESPONSE;
    page_fd = open(PAGE_NOT_FOUND , O_RDONLY);
  } else {
    page_fd = open(page, O_RDONLY);
    header_content = HTTP_200_RESPONSE;
  }

  snprintf(response, BUFFER_SIZE, header_content);
  *response_len = strlen(response);
  // copy page content to response
  while((bytes_read = read(page_fd, response + *response_len, BUFFER_SIZE - *response_len)) > 0) {
    *response_len += bytes_read;
  }

  close(page_fd);
}

void *handle_request(void *arg) {
  printf("Handling client request\n");
  int client_fd = *((int *)arg);
  char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));

  ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
  if(bytes_received > 0) {
    // regex tests if req is HTTP GET
    regex_t http_get_regex;
    regcomp(&http_get_regex, HTTP_GET_REGEX, REG_EXTENDED);

    // 2 matches because regexec() match is greedy (so matches the whole matching string and the group selected)
    regmatch_t matches[2];
    if(regexec(&http_get_regex, buffer, 2, matches, 0) == 0) {
      // extracting path from request
      buffer[matches[1].rm_eo] = '\0';
      const char *url_encoded_path_name = buffer + matches[1].rm_so;
      char url_path[strlen(url_encoded_path_name) + 1];
      if(url_decode(url_encoded_path_name, url_path) < 0) {
        error("url decode failed");
      };

      char *response = (char *)malloc(BUFFER_SIZE * sizeof(char));
      size_t response_len = sizeof(response);
      build_http_response(url_path, response, &response_len);
      send(client_fd, response, response_len, 0);
      free(response);
    }

    regfree(&http_get_regex);
  } else {
    printf("No bytes received for request");
  }

  close(client_fd);
  free(arg);
  free(buffer);

  return NULL;
}

void handle_parallel_requests(int *client_fd) {
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, handle_request, (void *)client_fd);
  pthread_detach(thread_id);
}

int main(int argc, char *argv[]) {
  int server_file_descriptor;
  struct sockaddr_in server_addr;

  // create server socket
  if((server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    error("socket failed");
  }

  // configure socket and then bind to the port
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if(bind(server_file_descriptor, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
      error("bind failed");
  }

  // listen for connections
  if(listen(server_file_descriptor, 1) < 0) {
    error("listen failed");
  }

  printf("Listening on port %d\n", PORT);
  while(1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int *client_fd = malloc(sizeof(int));

    if((*client_fd =
      accept(server_file_descriptor, (struct sockaddr *)&client_addr, 
        &client_addr_len)) < 0) {
        perror("accept failed");
        continue;
    }

    handle_parallel_requests(client_fd);
  }

  close(server_file_descriptor);
  return 0;
}
