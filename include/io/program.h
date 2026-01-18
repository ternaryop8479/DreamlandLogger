#ifndef DREAMLAND_LOGGER_INCLUDE_IO_PROGRAM_H
#define DREAMLAND_LOGGER_INCLUDE_IO_PROGRAM_H

#include <atomic>
#include <io/buffer.h>
#include <memory>
#include <string>
#include <thread>

namespace dl {

// Program class for managing child process I/O
class Program {
public:
	// IO stream type enumeration
	enum class IOStreamType {
		STDOUT, // Standard output stream
		STDERR // Standard error stream
	};

	// Constructor with command string
	explicit Program(const std::string &command);
	// Destructor
	~Program();

	// Disable copy and move operations
	Program(const Program &) = delete;
	Program &operator=(const Program &) = delete;
	Program(Program &&) = delete;
	Program &operator=(Program &&) = delete;

	// Start the program, return false if already running or failed to start
	bool run();
	// Send string to program's stdin
	bool send_string(const std::string &data);
	// Read string from program's output
	// @param read_by_line: if true, read one line; otherwise read all available
	// @param type: specify which stream to read from
	std::string read_string(bool read_by_line = false,
		IOStreamType type = IOStreamType::STDOUT);

	// Gracefully stop the program (SIGTERM)
	bool stop();
	// Forcefully kill the program (SIGKILL)
	bool kill();

	// Check if program is currently running
	bool is_running() const;
	// Get the exit code of the program (-1 if still running or not started)
	int get_exit_code() const;

private:
	// Reader thread function for capturing child process output
	void reader_thread_func();
	// Close all pipe file descriptors
	void close_pipes();
	// Cleanup resources after process termination
	void cleanup();

	std::string command_;
	pid_t child_pid_ = -1;
	std::atomic<bool> running_ { false };
	std::atomic<bool> stop_reader_ { false };
	int exit_code_ = -1;

	// Pipe file descriptors: [0] for read, [1] for write
	int stdin_pipe_[2] = { -1, -1 };
	int stdout_pipe_[2] = { -1, -1 };
	int stderr_pipe_[2] = { -1, -1 };

	// Buffers for stdout and stderr
	std::unique_ptr<Buffer> stdout_buffer_;
	std::unique_ptr<Buffer> stderr_buffer_;

	// Reader thread for capturing output
	std::thread reader_thread_;
};

}

#endif
