#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define INPUT_PIPE "/tmp/input_pipe"

int main() {
    int fd = open(INPUT_PIPE, O_WRONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char line[256];

    printf("Patient Page. Write commands:\n");

    while (fgets(line, sizeof(line), stdin)) {
        write(fd, line, strlen(line));
    }

    close(fd);
    return 0;
}
