#include <io/program.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <vector>

namespace dl {

Program::Program(const std::string& command)
    : command_(command),
      stdout_buffer_(std::make_unique<Buffer>()),
      stderr_buffer_(std::make_unique<Buffer>()) {
}

Program::~Program() {
    if (running_) {
        kill();
    }
    cleanup();
}

bool Program::run() {
    // Check if already running
    if (running_) {
        return false;
    }

    // Create pipes for stdin, stdout, stderr
    if (pipe(stdin_pipe_) == -1 ||
        pipe(stdout_pipe_) == -1 ||
        pipe(stderr_pipe_) == -1) {
        close_pipes();
        return false;
    }

    // Fork child process
    child_pid_ = fork();
    if (child_pid_ == -1) {
        close_pipes();
        return false;
    }

    if (child_pid_ == 0) {
        // Child process
        // Redirect stdin
        close(stdin_pipe_[1]);
        dup2(stdin_pipe_[0], STDIN_FILENO);
        close(stdin_pipe_[0]);

        // Redirect stdout
        close(stdout_pipe_[0]);
        dup2(stdout_pipe_[1], STDOUT_FILENO);
        close(stdout_pipe_[1]);

        // Redirect stderr
        close(stderr_pipe_[0]);
        dup2(stderr_pipe_[1], STDERR_FILENO);
        close(stderr_pipe_[1]);

        // Execute command using shell
        execl("/bin/sh", "sh", "-c", command_.c_str(), nullptr);
        
        // If execl fails, exit with error
        _exit(127);
    }

    // Parent process
    // Close unused pipe ends
    close(stdin_pipe_[0]);
    stdin_pipe_[0] = -1;
    close(stdout_pipe_[1]);
    stdout_pipe_[1] = -1;
    close(stderr_pipe_[1]);
    stderr_pipe_[1] = -1;

    // Set stdout and stderr pipes to non-blocking mode
    fcntl(stdout_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe_[0], F_SETFL, O_NONBLOCK);

    running_ = true;
    stop_reader_ = false;

    // Start reader thread
    reader_thread_ = std::thread(&Program::reader_thread_func, this);
    reader_thread_.detach();

    return true;
}

void Program::reader_thread_func() {
    constexpr size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    while (!stop_reader_) {
        bool has_data = false;

        // Read from stdout
        ssize_t bytes_read = read(stdout_pipe_[0], buffer, BUFFER_SIZE - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            stdout_buffer_->append(buffer, bytes_read);
            has_data = true;
        }

        // Read from stderr
        bytes_read = read(stderr_pipe_[0], buffer, BUFFER_SIZE - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            stderr_buffer_->append(buffer, bytes_read);
            has_data = true;
        }

        // Check if child process has exited
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == child_pid_) {
            // Child has exited, read remaining data
            while ((bytes_read = read(stdout_pipe_[0], buffer, BUFFER_SIZE - 1)) > 0) {
                stdout_buffer_->append(buffer, bytes_read);
            }
            while ((bytes_read = read(stderr_pipe_[0], buffer, BUFFER_SIZE - 1)) > 0) {
                stderr_buffer_->append(buffer, bytes_read);
            }

            if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code_ = -WTERMSIG(status);
            }
            running_ = false;
            break;
        }

        // Sleep briefly if no data to avoid busy waiting
        if (!has_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool Program::send_string(const std::string& data) {
    if (!running_ || stdin_pipe_[1] == -1) {
        return false;
    }

    ssize_t total_written = 0;
    ssize_t data_size = static_cast<ssize_t>(data.size());

    while (total_written < data_size) {
        ssize_t written = write(stdin_pipe_[1], data.c_str() + total_written,
                                data_size - total_written);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_written += written;
    }

    return true;
}

std::string Program::read_string(bool read_by_line, IOStreamType type) {
    Buffer* buffer = (type == IOStreamType::STDOUT)      ? stdout_buffer_.get()
                     : stderr_buffer_.get();

    if (read_by_line) {
        return buffer->read_line();
    }
    return buffer->read_all();
}

bool Program::stop() {
    if (!running_ || child_pid_ <= 0) {
        return false;
    }

    // Send SIGTERM for graceful termination
    if (::kill(child_pid_, SIGTERM) == -1) {
        return false;
    }

    return true;
}

bool Program::kill() {
    if (!running_ || child_pid_ <= 0) {
        return false;
    }

    // Send SIGKILL for forceful termination
    if (::kill(child_pid_, SIGKILL) == -1) {
        return false;
    }

    return true;
}

bool Program::is_running() const {
    return running_;
}

int Program::get_exit_code() const {
    return exit_code_;
}

void Program::close_pipes() {
    auto close_if_valid = [](int& fd) {
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    };

    close_if_valid(stdin_pipe_[0]);
    close_if_valid(stdin_pipe_[1]);
    close_if_valid(stdout_pipe_[0]);
    close_if_valid(stdout_pipe_[1]);
    close_if_valid(stderr_pipe_[0]);
    close_if_valid(stderr_pipe_[1]);
}

void Program::cleanup() {
    stop_reader_ = true;
    close_pipes();
}

}
