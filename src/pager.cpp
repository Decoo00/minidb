#include "pager.h"
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>



Pager::Pager(const std::string& path) 
        : fd_(::open(path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)), 
        page_count_(0) {

    if (fd_==-1) {
        throw std::runtime_error(std::string("Pager: failed to open '") + path + "': " + strerror(errno));
    }
    
    struct stat st;
    if (fstat(fd_, &st) == -1){
        int error = errno;
        ::close(fd_);
        throw std::runtime_error(std::string("Pager: failed to stat '") + path + "': " + strerror(error));
    }
    
    if (st.st_size % PAGE_SIZE != 0){
        ::close(fd_);
        throw std::runtime_error(std::string("Pager: '") + path + "' has invalid size " 
            + std::to_string(st.st_size) + " (not a multiple of " + std::to_string(PAGE_SIZE) + ")");
    }

    page_count_ = static_cast<page_id_t>(st.st_size / PAGE_SIZE);

}

Pager::~Pager() {
    ::close(fd_);
}

page_id_t Pager::allocate() {
    if (ftruncate(fd_, (page_count_ + 1) * PAGE_SIZE) == -1){
        throw std::runtime_error(std::string("Pager::allocate: failed to allocate: ") + strerror(errno));
    }
    return page_count_++;
}

void Pager::read(page_id_t id, char* buf) const {
    if (id >= page_count_) {
        throw std::runtime_error("Pager::read: page id " + std::to_string(id)
            + " out of range (page count: " + std::to_string(page_count_) + ")");
    }
    ssize_t res = pread(fd_, buf, PAGE_SIZE, id * PAGE_SIZE);
    read_count_++;

    if(res == -1){
        throw std::runtime_error(std::string("Pager::read: pread failed: ") + strerror(errno));
    } else if(res != static_cast<ssize_t>(PAGE_SIZE)){
        throw std::runtime_error("Pager::read: short read (" + std::to_string(res)
            + " of " + std::to_string(PAGE_SIZE) + " bytes)");
    }
}

void Pager::write(page_id_t id, const char* buf) {
    if (id >= page_count_) {
        throw std::runtime_error("Pager::write: page id " + std::to_string(id)
            + " out of range (page count: " + std::to_string(page_count_) + ")");
    }
    ssize_t res = pwrite(fd_, buf, PAGE_SIZE, id * PAGE_SIZE);
    write_count_++;

    if(res == -1){
        throw std::runtime_error(std::string("Pager::write: pwrite failed: ") + strerror(errno));
    } else if(res != static_cast<ssize_t>(PAGE_SIZE)){
        throw std::runtime_error("Pager::write: short write (" + std::to_string(res)
            + " of " + std::to_string(PAGE_SIZE) + " bytes)");
    }
}

void Pager::sync() {
    if (fsync(fd_) == -1) {
        throw std::runtime_error(std::string("Pager::sync: fsync failed: ") + strerror(errno));
    }
}

page_id_t Pager::page_count() const {
    return page_count_;
}

uint64_t Pager::read_count() const {
    return read_count_;
}


uint64_t Pager::write_count() const {
    return write_count_;
}