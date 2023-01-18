#include <system_error>
#include <stdexcept>
#include <array>
#include <string_view>
#include <vector>
#include <algorithm>
#include <memory>
#include <optional>
#include <iterator>

#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>

#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <unistd.h>

/// AF_UNIX / local socket specific address
using sockaddr_un_t = struct sockaddr_un;
/// generic address, usually pointer argument
using sockaddr_t = struct sockaddr;
/// used as list for poll()
using pollfd_t = struct pollfd;
/// file descriptor
using Descriptor = int;

/// std::errc or int
/// Unwrap will either return the int, or throw the errc as a system_error
class SysReturn {
    static constexpr std::errc sNotAnError = std::errc();
public:
    constexpr explicit SysReturn(int value) noexcept : mCode(sNotAnError), mValue(value) {}
    constexpr explicit SysReturn(std::errc code) noexcept : mCode(code), mValue() {}
    constexpr bool IsError() const { return mCode != sNotAnError; }
    [[noreturn]] void ThrowCode() const { throw std::system_error(std::make_error_code(mCode)); }
    constexpr int Unwrap() const { if (IsError()) ThrowCode(); return mValue; }
    constexpr std::errc GetCode() const { return mCode; }
private:
    std::errc mCode;
    int mValue;
};

/// call a system function and wrap the errno or int result in a SysReturn
template <typename Fn, typename... Args>
[[nodiscard]] inline SysReturn SysCall(Fn&& func, Args&&... args) noexcept {
    const int result = static_cast<int>(func(std::forward<Args>(args)...));
    if (result != -1) return SysReturn(result);
    return SysReturn(std::errc(errno));
}

/// wrap a blocking syscall and return nullopt if it would block
template <typename Fn, typename... Args>
[[nodiscard]] inline std::optional<SysReturn> SysCallBlocking(Fn&& func, Args&&... args) noexcept {
    const int result = static_cast<int>(func(std::forward<Args>(args)...));
    if (result != -1) return std::optional<SysReturn>(result);
    const auto code = static_cast<std::errc>(errno);
    if (code == std::errc::operation_would_block || code == std::errc::resource_unavailable_try_again) {
        return std::nullopt;
    }
    return std::optional<SysReturn>(code);
}

namespace event {

enum class SockMode {
    Acceptor,
    Connector
};

/// bitmask for which events to return
using Mask = short;
inline constexpr Mask Readable = POLLIN; /// enable Readable events
inline constexpr Mask Priority = POLLPRI; /// enable Priority events
inline constexpr Mask Writable = POLLOUT; /// enable Writable events

class Result {
public:
    explicit Result(short events) : v(events) {}
    bool IsReadable() const { return (v & POLLIN) != 0; } /// without blocking, connector can call read or acceptor can call accept
    bool IsPriority() const { return (v & POLLPRI) != 0; } /// some exceptional condition, for tcp this is OOB data
    bool IsWritable() const { return (v & POLLOUT) != 0; } /// can call write without blocking
    bool IsErrored() const { return (v & POLLERR) != 0; } /// error to be checked with Socket::GetError(), or write pipe's target read pipe was closed
    bool IsClosed() const { return (v & POLLHUP) != 0; } /// socket closed, however for connector, subsequent reads must be called until returns 0
    bool IsInvalid() const { return (v & POLLNVAL) != 0; } /// not an open descriptor and shouldn't be polled
private:
    short v;
};
/// poll an acceptor and its connections
class Poller {
    static constexpr Mask mConnectorMask = Readable | Writable;
    static constexpr Mask mAcceptorMask = Readable; 
public:
    void Poll(int timeoutMs) {
        SysCall(::poll, mPollList.data(), mPollList.size(), timeoutMs).Unwrap();
    }
    /// @tparam TPred (Descriptor, event::Result, event::SockMode) -> void
    template <typename TPred>
    void Poll(int timeoutMs, TPred&& pred) {
        Poll(timeoutMs);
        for (const pollfd_t& elem : mPollList) {
            SockMode mode = (elem.events == mAcceptorMask) ? SockMode::Acceptor : SockMode::Connector;
            pred(elem.fd, Result(elem.revents), mode);
        }
    }
    void AddConnector(Descriptor descriptor) {
        mPollList.push_back({descriptor, mConnectorMask, 0});
    }
    void AddAcceptor(Descriptor descriptor) {
        mPollList.push_back({descriptor, mAcceptorMask, 0});
    }
    Result At(int idx) const {
        return Result(mPollList.at(idx).revents);
    }
    bool Remove(Descriptor descriptor) {
        auto it = std::find_if(mPollList.begin(), mPollList.end(), 
            [&](const pollfd_t& elem){ return elem.fd == descriptor; });
        if (it == mPollList.end()) return false;
        mPollList.erase(it);
        return true;
    }
    void Clear() { mPollList.clear(); }
    int GetSize() const { return static_cast<int>(mPollList.size()); }
private:
    std::vector<pollfd_t> mPollList{};
};

}

/// owned socket file descriptor
class Socket {
    static constexpr Descriptor sInvalidSocket = -1;
public:
    /// open a new socket
    Socket(int domain, int type, int protocol) : mDescriptor(SysCall(::socket, domain, type, protocol).Unwrap()) {
        SetNonBlocking();
    }
    /// using file descriptor returned from system call
    explicit Socket(Descriptor descriptor) : mDescriptor(descriptor) {
        if (descriptor == sInvalidSocket) throw std::invalid_argument("invalid socket descriptor");
        SetNonBlocking(); // accepted from non-blocking socket still needs to be set
    }
    ~Socket() {
        // owns resource and must close it, descriptor will be set invalid if moved from
        // discard any errors thrown by close, most mean it never owned it or didn't exist
        if (mDescriptor != sInvalidSocket) (void)SysCall(::close, mDescriptor);
    }
    // manage descriptor like never null unique_ptr
    Socket(Socket&& other) noexcept :
        mDescriptor(other.mDescriptor),
        mIsReadable(other.mIsReadable),
        mIsWritable(other.mIsWritable),
        mIsNonBlocking(other.mIsNonBlocking) {
        other.mDescriptor = sInvalidSocket;
    }
    Socket& operator=(Socket&& rhs) noexcept {
        std::swap(mDescriptor, rhs.mDescriptor);
        mIsReadable = rhs.mIsReadable;
        mIsWritable = rhs.mIsWritable;
        mIsNonBlocking = rhs.mIsNonBlocking;
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    /// get underlying file descriptor
    Descriptor GetDescriptor() const { return mDescriptor; }
    /// get an error on the socket, indicated by errored poll event
    std::errc GetError() const {
        return static_cast<std::errc>(GetSockOpt<int>(SOL_SOCKET, SO_ERROR).first);
    }
    void SetBlocking() { mIsNonBlocking = false; SetStatusFlags(GetStatusFlags() & ~(O_NONBLOCK)); }
    void SetNonBlocking() { mIsNonBlocking = true; SetStatusFlags(GetStatusFlags() | O_NONBLOCK); }
    // only applies to non blocking, and set from Update (poll), always return true if blocking
    bool GetAndResetIsReadable() { const bool temp = mIsReadable; mIsReadable = false; return temp || !mIsNonBlocking; }
    bool GetAndResetIsWritable() { const bool temp = mIsWritable; mIsWritable = false; return temp || !mIsNonBlocking; }
    /// @return false if socket should close
    bool Update(event::Result res) {
        if (res.IsErrored()) {
            throw std::system_error(std::make_error_code(GetError()));
        }
        if (res.IsInvalid() || res.IsClosed()) {
            // TODO: technically could still have bytes waiting to be read on the close event
            return false;
        }
        if (res.IsReadable()) {
            mIsReadable = true;
        }
        if (res.IsWritable()) {
            mIsWritable = true;
        }
        return true;
    }

private:
    int GetStatusFlags() const { return SysCall(::fcntl, mDescriptor, F_GETFL, 0).Unwrap(); }
    void SetStatusFlags(int flags) { SysCall(::fcntl, mDescriptor, F_SETFL, flags).Unwrap(); }

    /// get or set socket option, most are ints, non default length is only for strings
    template <typename T>
    std::pair<T, socklen_t> GetSockOpt(int level, int optname, T inputValue = T(), socklen_t inputSize = sizeof(T)) const {
        T outValue = inputValue;
        socklen_t outSize = inputSize;
        SysCall(::getsockopt, mDescriptor, level, optname, &outValue, &outSize).Unwrap();
        return std::make_pair(outValue, outSize);
    }
    template <typename T>
    void SetSockOpt(int level, int optname, const T& inputValue, socklen_t inputSize = sizeof(T)) {
        SysCall(::setsockopt, level, optname, &inputValue, inputSize).Unwrap();
    }

    Descriptor mDescriptor;
    bool mIsReadable = false;
    bool mIsWritable = false;
    bool mIsNonBlocking = false;
};

/// address for unix sockets
class LocalAddress {
    static constexpr sa_family_t sFamily = AF_UNIX; // always AF_UNIX
    /// max returned by Size()
    static constexpr socklen_t sMaxSize = sizeof(sockaddr_un_t);
    /// offset of sun_path within the address object, sun_path is a char array
    static constexpr socklen_t sPathOffset = sMaxSize - sizeof(sockaddr_un_t::sun_path);
public:
    /// empty address
    LocalAddress() noexcept : mSize(sMaxSize) {}
    /// almost always bind before use
    explicit LocalAddress(std::string_view path)
        : mSize(sPathOffset + path.size() + 1) { // beginning of object + length of path + null terminator
        if (path.empty()) throw std::invalid_argument("path empty");
        if (mSize > sMaxSize) throw std::length_error("path too long");
        // copy and null terminate path
        std::strncpy(&mAddress.sun_path[0], path.data(), path.size());
        mAddress.sun_path[path.size()] = '\0';
        mAddress.sun_family = sFamily;
    }

    /// deletes the path on the filesystem, usually called before bind
    void Unlink() const {
        // TODO: keep important errors (permissions, logic, ...)
        (void)SysCall(::unlink, GetPath());
    }

    /// system calls with address
    const sockaddr_t* GetPtr() const { return reinterpret_cast<const sockaddr_t*>(&mAddress); }
    /// system calls with address out
    sockaddr_t* GetPtr() { return reinterpret_cast<sockaddr_t*>(&mAddress); }
    /// system calls with addrLen
    socklen_t GetSize() const { return mSize; }
    /// system calls with addrLen out
    socklen_t* GetSizePtr() {
        mSize = sMaxSize;
        return &mSize;
    }
    const char* GetPath() const { return &mAddress.sun_path[0]; }
    bool IsValid() const {
        return mAddress.sun_family == sFamily; // not used with wrong socket type
    }
private:
    socklen_t mSize;
    sockaddr_un_t mAddress{};
};

class LocalSocket : public Socket {
    static constexpr int sDomain = AF_UNIX, // unix domain socket
                         sType = SOCK_STREAM, // connection oriented, no message boundaries
                         sProtocol = 0; // auto selected
public:
    explicit LocalSocket(std::string_view path) : Socket(sDomain, sType, sProtocol), mAddress(path) {}
    LocalSocket(Descriptor descriptor, LocalAddress address) : Socket(descriptor), mAddress(address) {
        if (!mAddress.IsValid()) throw std::invalid_argument("invalid local socket address");
    }

protected:
    void UnlinkAddress() const { mAddress.Unlink(); }
    void Bind() const { SysCall(::bind, GetDescriptor(), mAddress.GetPtr(), mAddress.GetSize()).Unwrap(); }
    void Listen(int backlog) const { SysCall(::listen, GetDescriptor(), backlog).Unwrap(); }
    void Connect() const { SysCall(::connect, GetDescriptor(), mAddress.GetPtr(), mAddress.GetSize()).Unwrap(); }

private:
    LocalAddress mAddress;
};

/// connector manages a connection, can send/recv with
class LocalConnectorSocket : public LocalSocket {
public:
    /// open as outbound connector to path
    explicit LocalConnectorSocket(std::string_view path) : LocalSocket(path) {
        Connect();
    }
    /// open as inbound connector from accept
    LocalConnectorSocket(Descriptor descriptor, LocalAddress address) : LocalSocket(descriptor, address) {}
    /// send a byte buffer
    /// @tparam TBufIt iterator to contiguous memory
    /// @return number of bytes sent or nullopt if blocking
    template <typename TBufIt>
    std::optional<int> TrySend(TBufIt bufBegin, int bytesToSend) {
        if (!GetAndResetIsWritable()) return std::nullopt;
        constexpr int flags = 0;
        if (auto bytesSent = SysCallBlocking(::send, GetDescriptor(), &(*bufBegin), bytesToSend, flags)) {
            return (*bytesSent).Unwrap();
        }
        return std::nullopt;
    }
    /// receive a byte buffer
    /// @tparam TBufIt iterator to contiguous memory
    /// @return number of bytes written to buffer or nullopt if blocking
    template <typename TBufIt>
    std::optional<int> TryRecv(TBufIt bufBegin, int bufSize) {
        if (!GetAndResetIsReadable()) return std::nullopt;
        constexpr int flags = 0;
        if (auto bytesRecv = SysCallBlocking(::recv, GetDescriptor(), &(*bufBegin), bufSize, flags)) {
            return (*bytesRecv).Unwrap();
        }
        return std::nullopt;
    }
};

/// aka listener/passive socket, accepts connectors
class LocalAcceptorSocket : public LocalSocket {
public:
    /// open as acceptor on path, backlog is accept queue size
    LocalAcceptorSocket(std::string_view path, int backlog) : LocalSocket(path) {
        UnlinkAddress();
        Bind();
        Listen(backlog);
    }
    /// accept an inbound connector or nullopt if blocking
    std::optional<LocalConnectorSocket> Accept() {
        if (!GetAndResetIsReadable()) return std::nullopt;
        LocalAddress address;
        if (auto desc = SysCallBlocking(::accept, GetDescriptor(), address.GetPtr(), address.GetSizePtr())) {
            return LocalConnectorSocket((*desc).Unwrap(), address);
        }
        return std::nullopt;
    }
};

/// manage a single outbound connector
class BasicLocalClient {
public:
    void Open(std::string_view path) {
        if (IsOpen()) throw std::runtime_error("connection already open");
        mConnector = LocalConnectorSocket(path);
        mPoller.AddConnector(mConnector->GetDescriptor());
        assert(mPoller.GetSize() == 1);
    }
    void Close() {
        mConnector.reset();
        mPoller.Clear();
    }

    /// default timeout returns immediately
    void UpdateOnce(int timeoutMs = 0) {
        if (!IsOpen()) throw std::runtime_error("connection not open");
        mPoller.Poll(timeoutMs);

        if (!mConnector->Update(mPoller.At(0))) {
            Close();
        }
    }

    /// send a byte buffer, continuously updates until entire message is sent
    /// @tparam TBufIt iterator to contiguous memory
    /// @return false if the send fails (connection closed)
    template <typename TBufIt>
    bool Send(TBufIt bufBegin, int bufSize) {
        TBufIt msgIt = bufBegin;
        int bytesToSend = bufSize;
        int maxIter = 100;
        while (--maxIter && bytesToSend > 0) {
            if (!IsOpen()) return false;
            std::optional<int> bytesSent = mConnector->TrySend(msgIt, bytesToSend);
            if (!bytesSent) {
                // blocking, poll and try again
                UpdateOnce();
            } else if (*bytesSent <= 0) {
                // returning 0 is very unlikely given the small amount of data, something about filling up the internal buffer?
                // handle it the same as a would block error, and hope eventually it'll resolve itself
                UpdateOnce(20); // 20ms timeout to give the buffer time to be emptied
            } else if (*bytesSent > bytesToSend) {
                // probably guaranteed to not happen, but just in case
                throw std::runtime_error("bytes sent > bytes to send");
            } else {
                // SOCK_STREAM allows partial sends, but almost guaranteed to not happen on local sockets
                bytesToSend -= *bytesSent;
                msgIt += *bytesSent;
            }
        }
        if (maxIter == 0) {
            throw std::runtime_error("send stuck in infinite loop");
        }
        return true;
    }

    /// receive into byte buffer
    /// @tparam TBufIt iterator to contiguous memory
    /// @return number of bytes written to buffer, 0 indicating there is no message waiting
    template <typename TBufIt>
    int RecvOnce(TBufIt bufBegin, int bytesToRead) {
        std::optional<int> bytesRecv = mConnector->TryRecv(bufBegin, bytesToRead);
        // if the user is doing while(messageReceived) {  } to empty the message queue
        // then need to poll once before the next iteration, but only if there were bytes received
        if (bytesRecv && *bytesRecv > 0) UpdateOnce();
        return bytesRecv.value_or(0);
    }

    /// receive into byte buffer, continously updates until all bytes are read
    /// @tparam TBufIt iterator to contiguous memory
    /// @return true if bytesToRead bytes were written to buffer
    template <typename TBufIt>
    bool RecvAll(const TBufIt bufBegin, int bytesToRead) {
        int maxIter = 100;
        auto bufIt = bufBegin;
        while (--maxIter && bytesToRead > 0) {
            if (!IsOpen()) return false;
            std::optional<int> bytesRecv = mConnector->TryRecv(bufIt, bytesToRead);
            if (!bytesRecv || *bytesRecv == 0) {
                // try again
            } else if (*bytesRecv < 0 || *bytesRecv > bytesToRead) {
                // should not be possible
                throw std::length_error("bytesRecv");
            } else {
                // read some or all of the message
                bytesToRead -= *bytesRecv;
                bufIt += *bytesRecv;
            }
            // set readable for next bytes, or a future call to Recv
            UpdateOnce();
        }
        if (maxIter == 0) {
            throw std::runtime_error("recv stuck in infinite loop");
        }
        return true;
    }

    bool IsOpen() const { return mConnector.has_value(); }

private:
    std::optional<LocalConnectorSocket> mConnector{};
    event::Poller mPoller{}; // index 0 is connector if open
};
