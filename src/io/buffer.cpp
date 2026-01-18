#include <io/buffer.h>

// 字符串缓冲区类，采用std::string对数据进行缓存
// append实现思路为直接调用string的append函数以实现追加字符串
// 关于read部分，提供两种接口，第一种为按行读取，第二种为读取缓冲区剩余所有内容，缓冲区在读取并返回字符串的同时会删除被读取的字符串
// 简单来讲，就是按行读取的时候，每读取一行就会从缓冲区删除当前行，而读取所有内容的时候就是返回整个缓冲区并清空缓冲区
// 但是由于按行读取中，每次读取都会从头开始删除字符，而目标程序又随时可能从缓冲区后方插入字符，
// 因此，不论是将字符串反转存储，每次按行读取时删除尾部字符，还是每次按行读取都删除头部字符串(对于std::string来说需要额外拷贝一次)，对于IO十分频繁且经常按行读取的程序来说，时间成本都很大。
// 对于这种情况，还有一种方法，即std::deque<char>，但是由于这种存储方式随机读写会有一定开销，且并非连续存储，因此我们不考虑这种实现方式。
// 此时，我们不妨考虑依旧使用原本的std::string存储缓冲区数据，而同时引入一个buffer_pointer变量，同时不在每次按行读取的时候都实在地从std::string头部删除字符串，
// 而是在每次按行读取的时候，都将这个pointer向后推进(如原始pointer指向的位置为0，现读取了一行长度为5的字符串，那么读取结束后pointer将指向index=5)，
// 这样的话，我们就可以避免每次按行读取时都需要重新拷贝std::string的内存开销。
// 而关于内存占用的问题，我们不妨设置一个最大缓冲区大小，当按行读取时发现当前缓冲区整体大小超过这个大小，再从pointer指向的位置开始截断整个std::string。
// 这样的话，在频繁按行读取和写入的场景中，这种方法会非常快(不妨考虑如下一个场景，内核部分不断向缓冲区追加内容，而日志输出模块不断地按行读取缓冲区所存储的内容，每次触发重分配的时候都只会有一小部分数据发生拷贝，这样的运行效率会很高)

namespace dl {

// 构造函数
Buffer::Buffer(uint64_t max_deleted_buffer_size) : MAX_DELETED_BUFFER_SIZE(max_deleted_buffer_size) {
}

// 向字符串末尾直接追加字符(为了避免重分配的拷贝时间可能阻塞主线程，这两个函数不添加截断被删除数据的逻辑)
void Buffer::append(const std::string &data) {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.append(data);
}
void Buffer::append(const char *data, uint64_t len) {
	if (data == nullptr || len == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.append(data, len);
}

// 读取缓冲区，read_line函数需要在读取完当前行后判断当前被标记为删除的字符串长度是否大于阈值，是的话则需要重分配
std::string Buffer::read_line() {
	std::lock_guard<std::mutex> lock(mutex_);
	// 检查缓冲区指针指向的位置是否越界
	if (buffer_ptr_ >= buffer_.size()) {
		return "";
	}
	// 从指针指向位置开始查找'\n'
	uint64_t newline_pos = buffer_.find('\n', buffer_ptr_);
	if (newline_pos == std::string::npos) { // 还没有\n就返回空白
		return "";
	}

	// 先存储结果(反正必须产生一次拷贝)
	std::string result = buffer_.substr(buffer_ptr_, newline_pos - buffer_ptr_ + 1);

	// 更新buffer_ptr_指向位置
	buffer_ptr_ = newline_pos + 1;

	// 重分配
	if (buffer_ptr_ >= MAX_DELETED_BUFFER_SIZE) { // 已读取部分大于阈值
		buffer_ = buffer_.substr(buffer_ptr_);
		buffer_ptr_ = 0;
	}

	return result;
}
std::string Buffer::read_all() {
	std::lock_guard<std::mutex> lock(mutex_);
	// 检查缓冲区指针指向的位置是否越界
	if (buffer_ptr_ >= buffer_.size()) {
		return "";
	}

	// 存储结果
	std::string result = buffer_.substr(buffer_ptr_);

	// 直接清除buffer_
	clear();

	return result;
}

void Buffer::clear() {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.clear();
	buffer_ptr_ = 0;
}

bool Buffer::empty() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return buffer_ptr_ >= buffer_.size();
}

}
