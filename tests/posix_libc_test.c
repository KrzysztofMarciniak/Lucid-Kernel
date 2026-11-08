/* Host-side tests for POSIX libc wrapper marshalling and errors. */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "recipes/syscall.h"

static uint32_t mock_result;
static uint32_t mock_call_count;
static uint32_t mock_number;
static uint32_t mock_arg1;
static uint32_t mock_arg2;
static uint32_t mock_arg3;
static uint32_t mock_arg4;

uint32_t lucid_user_syscall(uint32_t number,
                            uint32_t arg1,
                            uint32_t arg2,
                            uint32_t arg3,
                            uint32_t arg4)
{
    mock_call_count++;
    mock_number = number;
    mock_arg1 = arg1;
    mock_arg2 = arg2;
    mock_arg3 = arg3;
    mock_arg4 = arg4;
    return mock_result;
}

static int expect(int condition, int line)
{
    if (condition) return 0;
    return line;
}

#define CHECK(condition) do { \
    int failed_at = expect((condition), __LINE__); \
    if (failed_at) return failed_at; \
} while (0)

int main(void)
{
    mock_result = 0;
    CHECK(access("/system/bin/tcc", F_OK) == 0);
    CHECK(errno == 0);
    CHECK(mock_call_count == 1);
    CHECK(mock_number == SYS_FS_WRITE);

    mock_result = 0;
    CHECK(access("/system/bin", F_OK) == 0);
    CHECK(errno == 0);

    mock_result = 0;
    CHECK(access("users/root/welcome", F_OK) == 0);
    CHECK(mock_number == SYS_FS_WRITE);

    mock_result = (uint32_t)(-ENOENT);
    CHECK(access("/missing", F_OK) == -1);
    CHECK(errno == ENOENT);
    mock_result = 0;
    CHECK(access("/system/bin/tcc", F_OK) == 0);

    uint32_t calls_before = mock_call_count;
    CHECK(access("", F_OK) == -1);
    CHECK(errno == ENOENT);
    CHECK(access(0, F_OK) == -1);
    CHECK(errno == EINVAL);
    CHECK(access("/system/bin/tcc", W_OK) == -1);
    CHECK(errno == ENOTSUP);
    CHECK(mock_call_count == calls_before);

    CHECK(access("/system/bin/tcc", 8) == -1);
    CHECK(errno == EINVAL);
    CHECK(mock_call_count == calls_before);

    CHECK(access("relative", F_OK) == 0);
    CHECK(mock_number == SYS_FS_WRITE);
    calls_before = mock_call_count;

    char too_long[SYS_PATH_MAX + 1];
    for (uint32_t i = 0; i < SYS_PATH_MAX; i++) too_long[i] = 'a';
    too_long[SYS_PATH_MAX] = '\0';
    CHECK(access(too_long, F_OK) == -1);
    CHECK(errno == ENAMETOOLONG);
    CHECK(mock_call_count == calls_before);

    mock_result = 7;
    CHECK(open("input.c", O_RDONLY) == 7);
    CHECK(mock_number == SYS_FS_WRITE);
    CHECK(mock_arg2 > 4);

    char buffer[8];
    mock_result = 4;
    CHECK(read(7, buffer, sizeof(buffer)) == 4);
    CHECK(mock_number == SYS_FS_READ);
    CHECK(mock_arg4 == sizeof(buffer));

    mock_result = (uint32_t)(-EBADF);
    CHECK(write(7, buffer, sizeof(buffer)) == -1);
    CHECK(errno == EBADF);
    CHECK(mock_number == SYS_FS_WRITE);

    mock_result = 2;
    CHECK(lseek(7, 2, 0) == 2);
    CHECK(mock_number == SYS_FS_WRITE);

    mock_result = 0;
    CHECK(close(7) == 0);
    CHECK(mock_number == SYS_FS_WRITE);
    CHECK(unlink("output.o") == 0);
    CHECK(mock_number == SYS_FS_WRITE);

    mock_result = (uint32_t)(-ENOENT);
    CHECK(open("missing.c", O_RDONLY) == -1);
    CHECK(errno == ENOENT);

    char cwd[2];
    calls_before = mock_call_count;
    CHECK(getcwd(cwd, sizeof(cwd)) == cwd);
    CHECK(cwd[0] == '/' && cwd[1] == '\0');
    CHECK(mock_call_count == calls_before);
    CHECK(getcwd(cwd, 1) == 0);
    CHECK(errno == ERANGE);

    return 0;
}
