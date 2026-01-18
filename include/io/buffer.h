#ifndef DREAMLAND_LOGGER_INCLUDE_IO_BUFFER_H
#define DREAMLAND_LOGGER_INCLUDE_IO_BUFFER_H

#include <mutex>
#include <string>

namespace dl {

// Thread-safe IO buffer with lazy compaction
class Buffer {
public:
	// Maximum deleted buffer size threshold to prevent frequent reallocation
	const uint64_t MAX_DELETED_BUFFER_SIZE = 4096;

	// Constructors
	Buffer() = default;
	Buffer(uint64_t max_buffer_size);
	// Constructors that be banned.
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	Buffer(Buffer &&) = delete;
	Buffer &operator=(Buffer &&) = delete;

	// Append data to the end of buffer
	void append(const std::string &data);
	void append(const char *data, uint64_t len);

	// Read all unread content and move read position to end
	std::string read_all();
	// Read one line ending with '\n', return empty if no complete line
	std::string read_line();

	// Clear the buffer and reset read position
	void clear();
	// Check if buffer has unread data
	bool empty() const;

private:
	std::string buffer_;
	uint64_t buffer_ptr_ = 0;
	mutable std::mutex mutex_;
};

}

#endif
