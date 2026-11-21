#include "ui/fileio.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace zs {

bool write_file_atomic(const std::string& path, const std::string& data, mode_t mode) {
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    fchmod(fd, mode);   // O_CREAT's mode is masked by umask; an old .tmp keeps its own
    size_t done = 0;
    while (done < data.size()) {
        ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            unlink(tmp.c_str());
            return false;
        }
        done += size_t(n);
    }
    if (fsync(fd) != 0) {
        close(fd);
        unlink(tmp.c_str());
        return false;
    }
    if (close(fd) != 0 || std::rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    // Make the rename itself durable.
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return true;
}

}  // namespace zs
