/**
 * @file ring_buffer.h
 * @author Stanislav Matkov (https://github.com/matkovst)
 * @brief Магический круговой буфер
 * 
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#if defined(__linux__)
    #include <features.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <sys/utsname.h>
    #include <sys/sysinfo.h>
    #include <sys/resource.h>
    #include <linux/version.h>
#elif defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__) && defined(__MACH__)
    #error "No implementation for Apple"
#endif

#if defined(__has_include)
    #if __has_include(<valgrind/memcheck.h>)
        #include <valgrind/memcheck.h>
    #endif
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <sstream>
#include <type_traits>

#define RING_BUFFER_H_MIN(a, b) ((a) < (b) ? (a) : (b)) // Самописный макрос для предотвращения коллизий виндового min и std::min

std::string syscallFailureMessage(const char*);

static size_t getPageSize()
{
#if defined(__linux__)
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#elif defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<size_t>(info.dwPageSize);
#endif
}

static size_t getRamSize()
{
#if defined(__linux__)

    struct sysinfo si;
    if (sysinfo(&si) == 0)
        return static_cast<size_t>(si.totalram) * si.mem_unit; // в байтах
    
    // Альтернативный способ
    size_t pages = sysconf(_SC_PHYS_PAGES);
    size_t page_size = getPageSize();
    if (pages > 0 && page_size > 0)
        return pages * page_size; // в байтах

#elif defined(_WIN32)

    ULONGLONG kBytes = 0;

    if (GetPhysicallyInstalledSystemMemory(&kBytes))
    {
        const uint64_t bytes = static_cast<uint64_t>(kBytes) * 1024;
        if (bytes >= (std::pow<uint64_t>(2, sizeof(size_t)) - 1)) // Оперативы больше, чем адресов
            return static_cast<size_t>(-1);
        else
            return static_cast<size_t>(bytes);
    }
    else
    {
        const auto explain = syscallFailureMessage("Failed to get physical memory size");
        printf("%s\n", explain.c_str());
    }

#endif

    return 0;
}

static size_t getFileLimit()
{
#if defined(__linux__)

    struct rlimit limit;
    if (0 != getrlimit(RLIMIT_FSIZE, &limit))
    {
        perror("getrlimit call failed\n");
        return 0;
    }

    return (limit.rlim_cur == RLIM_INFINITY) ? static_cast<size_t>(-1) : static_cast<size_t>(limit.rlim_cur);

#elif defined(_WIN32)

    return getRamSize();

#endif
}

inline std::string syscallFailureMessage(const char* msg)
{
#if defined(__linux__)

    return std::string(msg) + ": " + std::strerror(errno);

#elif defined(_WIN32)

    struct ScopedLPVOID final
    {
        explicit ScopedLPVOID(LPVOID _ptr) : ptr(_ptr) { }
        ~ScopedLPVOID() {if (ptr) LocalFree(ptr);}

        LPVOID ptr {nullptr};
    };

    ScopedLPVOID errorBuffer16 {nullptr};
    DWORD errorCode = GetLastError();
    std::ostringstream log;

    if (nullptr != msg)
        log << msg << ": ";
    else
        log << "System error code: ";
    log << errorCode;

    if (DWORD bufferSize = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR) &errorBuffer16.ptr, 0, NULL))
    {
        // UTF-16 -> UTF-8
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)errorBuffer16.ptr, static_cast<int>(bufferSize), NULL, 0, NULL, NULL);
        if (0 == sizeNeeded)
            return log.str();

        std::string errorBuffer8(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)errorBuffer16.ptr, static_cast<int>(bufferSize), (LPSTR)errorBuffer8.data(), sizeNeeded, NULL, NULL);

        std::string errorSubBuffer8 = errorBuffer8.substr(0, errorBuffer8.size()-2); // Последние символы битые. Причина не ясна.
        log << ' ' << '(' << errorSubBuffer8 << ')';
    }

    return log.str();

#endif
}

#if defined(__linux__)

static utsname getLinuxSystem()
{
    static struct utsname buffer;
    if (0 != uname(&buffer))
        perror("uname call failed\n");

    return buffer;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)) && \
    (defined(__GLIBC__) && defined(__GLIBC_MINOR__) && (__GLIBC__ * 100 + __GLIBC_MINOR__) >= 227)

// У GLIBC свой memfd_create

#else // GLIBC < 2.27: memfd_create отсутствует - пишем свой

int memfd_create(const char* name, unsigned int flags)
{
    return syscall(__NR_memfd_create, name, flags);
}

#endif // #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))

#endif // #if defined(__linux__)

#if defined(_WIN32)

typedef PVOID(WINAPI *PFN_VirtualAlloc2)(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount
);

typedef PVOID(WINAPI *PFN_MapViewOfFile3)(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount
);

PFN_VirtualAlloc2 getVirtualAlloc2(void) {
    HMODULE hMod = GetModuleHandleA("kernelbase.dll");
    if (!hMod) return NULL;
    return (PFN_VirtualAlloc2)GetProcAddress(hMod, "VirtualAlloc2");
}

PFN_MapViewOfFile3 getMapViewOfFile3(void) {
    HMODULE hMod = GetModuleHandleA("kernelbase.dll");
    if (!hMod) return NULL;
    return (PFN_MapViewOfFile3)GetProcAddress(hMod, "MapViewOfFile3");
}

static size_t getGranularity()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<size_t>(info.dwAllocationGranularity);
}

typedef NTSTATUS(WINAPI* LPFN_RTLGETVERSION)(PRTL_OSVERSIONINFOEXW);
static std::string getWindowsSystem()
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (!hMod)
        return std::string();

    auto RtlGetVersion = (LPFN_RTLGETVERSION)GetProcAddress(hMod, "RtlGetVersion");
    if (!RtlGetVersion)
        return std::string();

    RTL_OSVERSIONINFOEXW osInfo = { 0 };
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);

    if (0 != RtlGetVersion(&osInfo))
        return std::string();

    std::string ver;
    if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000)
        ver = "Windows 11 ";
    else if (osInfo.dwMajorVersion == 10)
        ver = "Windows 10 ";
    else
        ver = "Windows ";

    ver += '(';
    ver += std::to_string(osInfo.dwMajorVersion);
    ver += '.';
    ver += std::to_string(osInfo.dwMinorVersion);
    ver += '.';
    ver += std::to_string(osInfo.dwBuildNumber);
    ver += ')';
    return ver;
}

#endif // #if defined(_WIN32)


/**
 * @brief Специальный аллокатор памяти под круговой буфер.
 *        Выделяет две соседние области виртуальной памяти, привязанные к одной физической области.
 *        left.data[0] == right.data[0]
 * 
 *            Left virtual pages   |   Right virtual pages
 *         ________________________|________________________
 *        |____|____|____|____|____|____|____|____|____|____|
 *                           \           /
 *                            \         /
 *                             \       /
 * 
 *                           Physical pages
 *                      ________________________
 *                     |____|____|____|____|____|
 */
class RingAllocator
{
public:
    /**
     * @brief Конструктор.
     * 
     * @param size ненулевой размер буфера (в байтах), кратный размеру страницы
     */
    explicit RingAllocator(size_t size) : m_size(size)
    {
        if (0 == m_size)
            throw std::runtime_error("Empty buffer not allowed");

        if (m_size >= getFileLimit()) // Учёт ограничений процесса
        {
            throw std::runtime_error("Buffer size " + std::to_string(m_size) + 
                                     " exceeded " + std::to_string(getFileLimit()) + " bytes limit");
        }

        if (m_size % getPageSize() != 0)
        {
            std::ostringstream explain;
            explain << "Buffer size " << m_size << " must be multiple of page size " << getPageSize();
            throw std::runtime_error(explain.str());
        }

        if (m_size > getRamSize())
            throw std::runtime_error("Buffer size must not exceed RAM size");

    #if defined(__linux__)

        // Застолбить виртуальную область под будущее разбиение на 2 блока
        m_base = mmap(
            nullptr,                                        // Дать системе подобрать адрес, выровненный по размеру страницы
            m_size << 1,                                    // Объём под два будущих блока
            PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0   // Создать блок в приватной области памяти, без доступа. Доступ будет дан двум будущим блокам
        );
        if (MAP_FAILED == m_base)
        {
            throw std::runtime_error(
                syscallFailureMessage("Failed to reserve virtual space"));
        }

    #ifdef __MEMCHECK_H
        VALGRIND_MALLOCLIKE_BLOCK(m_base, m_size << 1, 0, 0);
    #endif

        // Узнать версию ядра.

        int major = 0;
        int minor = 0;
        int patch = 0;
        int matched = sscanf(getLinuxSystem().release, "%d.%d.%d", &major, &minor, &patch);
        const bool oldLinux = (matched >= 2 && (major > 3 || (major == 3 && minor >= 17)));

        // Создать временный файл для отображения в RAM

        int fileDesc = -1;
        char fileName[4096] = {};

        if (oldLinux) // Реализация через настоящий, временный файл (< Linux 3.17)
        {
            strcpy(fileName, "/tmp/ring_buffer_tempfile_XXXXXX");
            fileDesc = mkstemp(fileName); // Создать уникальный временный файл
        }
        else // Реализация через анонимный файл
        {
            strcpy(fileName, "ring_buffer_tempfile"); // Файл не хранится на диске, поэтому название не обязано быть уникальным https://man7.org/linux/man-pages/man2/memfd_create.2.html
            fileDesc = memfd_create(fileName, MFD_CLOEXEC); // Создать уникальный анонимный файл
        }

        if (-1 == fileDesc)
        {
            this->cleanup(fileDesc);
            throw std::runtime_error(
                syscallFailureMessage("Failed to create temp file"));
        }

        if (oldLinux)
        {
            if (0 != unlink(fileName)) // Файл будет удалён сам, после закрытия дескриптора
            {
                this->cleanup(fileDesc);
                throw std::runtime_error(
                    syscallFailureMessage("Failed to unlink temp file"));
            }
        }

        if (-1 == ftruncate(fileDesc, m_size))
        {
            this->cleanup(fileDesc);
            throw std::runtime_error(
                syscallFailureMessage("Failed to set temp file size"));
        }

        // Расположить виртуальные блоки по заданному адресу, 
        // привязать физическую область памяти

        m_leftPage = /* page-aligned */ mmap(
            m_base, m_size,                     // Адрес и размер блока
            PROT_READ | PROT_WRITE,             // Права доступа к блоку
            MAP_SHARED | MAP_FIXED, fileDesc, 0 // Привязать анонимный файл к блоку по заданному адресу
            );
        m_rightPage = /* page-aligned */ mmap(
            static_cast<char*>(m_base) + m_size, m_size,    // Адрес и размер блока
            PROT_READ | PROT_WRITE,                         // Права доступа к блоку
            MAP_SHARED | MAP_FIXED, fileDesc, 0             // Привязать анонимный файл к блоку по заданному адресу
            );
        if (MAP_FAILED == m_leftPage || MAP_FAILED == m_rightPage)
        {
            this->cleanup(fileDesc);
            throw std::runtime_error(syscallFailureMessage("Failed to map pages"));
        }

        // Дескриптор больше не нужен - закрыть
        if (close(fileDesc))
        {
            const auto explain = syscallFailureMessage("Failed to close temp file during construction");
            printf("%s\n", explain.c_str());
        }
        else
        {
            fileDesc = -1; // На всякий случай
        }

    #elif defined(_WIN32)

        PFN_VirtualAlloc2 pVirtualAlloc2 = getVirtualAlloc2();
        PFN_MapViewOfFile3 pMapViewOfFile3 = getMapViewOfFile3();
        if (nullptr == pVirtualAlloc2 || nullptr == pMapViewOfFile3)
        {
            throw std::runtime_error(syscallFailureMessage("Failed to get sys funcs"));
        }

        // Застолбить виртуальную область под будущее разбиение на 2 блока
        m_base = pVirtualAlloc2(
            NULL, // Текущий процесс
            NULL, // Дать системе подобрать адрес, выровненный по granularity
            m_size << 1, // Объем
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, // Тип: плейсхолдер
            NULL, 0);
        if (nullptr == m_base)
        {
            throw std::runtime_error(
                syscallFailureMessage("Failed to reserve virtual space"));
        }
        if (!VirtualFree(m_base, m_size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
        {
            throw std::runtime_error(
                syscallFailureMessage("Failed to free virtual space"));
        }

        // Создать временный файл для отображения в RAM

        struct ScopedHandle final
        {
            explicit ScopedHandle(HANDLE _han) : han(_han) { }
            ~ScopedHandle()
            {
                if (nullptr != han && INVALID_HANDLE_VALUE != han)
                {
                    if (CloseHandle(han))
                    {
                        han = INVALID_HANDLE_VALUE; // На всякий случай
                    }
                    else
                    {
                        const auto explain = syscallFailureMessage("Failed to close handle");
                        printf("%s\n", explain.c_str());
                    }
                };
            }

            HANDLE han {nullptr};
        };

        HANDLE fileDesc = CreateFileMappingW(
            INVALID_HANDLE_VALUE,           // Файл подкачки
            NULL,                           // Защита по умолчанию
            PAGE_READWRITE,                 // Доступ на чтение/запись
            0, static_cast<DWORD>(m_size),  // Размер
            NULL);
        ScopedHandle fileDescRaii {fileDesc};

        // Расположить виртуальные блоки по заданному адресу, 
        // привязать физическую область памяти

        m_leftPage = pMapViewOfFile3(
            fileDesc, NULL,                                     // Файл подкачки, текущий процесс
            m_base, 0, m_size,                                  // Адрес и размер блока
            MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0    // Заполнить подложку, отобразить блок в файл подкачки
            );
        m_rightPage = pMapViewOfFile3(
            fileDesc, NULL,                                                 // Файл подкачки, текущий процесс
            (void*)(reinterpret_cast<char*>(m_base) + m_size), 0, m_size,   // Адрес и размер блока
            MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0                // Заполнить подложку, отобразить блок в файл подкачки
            );

        if (nullptr == m_leftPage || nullptr == m_rightPage)
        {
            this->cleanup();
            throw std::runtime_error(syscallFailureMessage("Failed to map pages"));
        }

    #endif

        assert(0 != this->m_size);
        assert(nullptr != this->m_base);
        assert(nullptr != this->m_leftPage);
        assert(nullptr != this->m_rightPage);
    }

    virtual ~RingAllocator() {this->cleanup();}

    RingAllocator(const RingAllocator&) = delete;
    RingAllocator& operator=(const RingAllocator&) = delete;

    RingAllocator(RingAllocator&& other) noexcept
        : m_size(other.m_size), m_base(other.m_base)
        , m_leftPage(other.m_leftPage), m_rightPage(other.m_rightPage)
    {
        other.m_size        = 0;
        other.m_base        = nullptr;
        other.m_leftPage    = nullptr;
        other.m_rightPage   = nullptr;
    }
    RingAllocator& operator=(RingAllocator&& other) noexcept
    {
        if (this == &other)
            return *this;

        this->m_size        = other.m_size;
        this->m_base        = other.m_base;
        this->m_leftPage    = other.m_leftPage;
        this->m_rightPage   = other.m_rightPage;

        other.m_size        = 0;
        other.m_base        = nullptr;
        other.m_leftPage    = nullptr;
        other.m_rightPage   = nullptr;

        return *this;
    }

    /**
     * @brief Указатель на начало левого блока
     */
    void* left() const noexcept {return m_leftPage;}

    /**
     * @brief Указатель на начало правого блока
     */
    void* right() const noexcept {return m_rightPage;}

    /**
     * @brief Размер буфера (в байтах)
     */
    size_t size() const noexcept {return m_size;}

private:
    void cleanup(int fileDesc = -1) noexcept
    {
    #if defined(__linux__)

        if (m_base && m_size > 0)
        {
        #ifdef __MEMCHECK_H
            VALGRIND_FREELIKE_BLOCK(m_base, 0);
        #endif
            if (0 != munmap(m_base, m_size << 1))
            {
                const auto explain = syscallFailureMessage("Failed to ummap pages during cleaning up");
                printf("%s\n", explain.c_str());
            }
        }

        if (-1 != fileDesc)
        {
            if (0 != close(fileDesc))
            {
                const auto explain = syscallFailureMessage("Failed to close temp file during cleaning up");
                printf("%s\n", explain.c_str());
            }
        }

    #elif defined(_WIN32)

        if (m_leftPage)
        {
            if(!UnmapViewOfFile(m_leftPage))
            {
                const auto explain = syscallFailureMessage("Failed to ummap left block during cleaning up");
                printf("%s\n", explain.c_str());
            }
        }
        if (m_rightPage)
        {
            if (!UnmapViewOfFile(m_rightPage))
            {
                const auto explain = syscallFailureMessage("Failed to ummap right block during cleaning up");
                printf("%s\n", explain.c_str());
            }
        }

    #endif
    }

private:
    size_t m_size           {0};        // Размер буфера (в байтах), кратный размеру страницы
    void*  m_base           {nullptr};  // Подложка для виртуальных блоков
    void*  m_leftPage       {nullptr};  // Левый виртуальный блок
    void*  m_rightPage      {nullptr};  // Правый виртуальный блок
};

/**
 * @brief Однопоточный непрерывный закольцованный буфер с доступом к элементам и счетчиком записи.
 * 
 *        Не подвержен фрагментации, память выделяется один раз.
 *        Новые элементы перезаписывают старые.
 */
class RingBuffer
{
public:
#if INTPTR_MAX == INT32_MAX
    using idx_t = int32_t;
    using uidx_t = uint32_t;
#elif INTPTR_MAX == INT64_MAX
    using idx_t = int64_t;
    using uidx_t = uint64_t;
#else
    #error "Unsupported pointer size"
#endif

public:
    /**
     * @brief Конструктор.
     * 
     * @note Необходимое условие: capacity*elemSize % pageSize == 0.
     * 
     * @param capacity ненулевое количество элементов в буфере (в единицах)
     * @param elemSize ненулевой размер элемента (в байтах)
     */
    RingBuffer(size_t capacity, size_t elemSize)
        : m_allocator(capacity*elemSize), m_capacity(capacity), m_elemSize(elemSize)
    {
        m_head = m_allocator.left();
        if (nullptr == m_head) // На всякий случай
            throw std::runtime_error("Got NULL pointer after allocating buffer data");
    }

    virtual ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    RingBuffer(RingBuffer&& other) noexcept
        : m_allocator(std::move(other.m_allocator)), m_capacity(other.m_capacity), m_size(other.m_size)
        , m_elemSize(other.m_elemSize), m_headIdx(other.m_headIdx), m_head(other.m_head)
    {
        other.m_capacity    = 0;
        other.m_size        = 0;
        other.m_elemSize    = 0;
        other.m_headIdx     = 0;
        other.m_head        = nullptr;
    }
    RingBuffer& operator=(RingBuffer&& other) = default;

    /**
     * @brief Записать в буфер elemSize байт данных по указателю.
     * 
     * @param src указатель на elemSize байт данных
     * 
     * @note Временная сложность O(N = elemSize), пространственная O(1)
     */
    void put(const void* src)
    {
        if (nullptr == src)
            throw std::runtime_error("Putting NULL pointer not allowed");

        memcpy(m_head, src, m_elemSize);
        this->proceed();
    }

    /**
     * @brief TODO: Записать в буфер объект
     */
    template<typename T>
    void put2(const T& obj)
    {
        if (sizeof(obj) != m_elemSize)
        {
            std::ostringstream explain;
            explain << "Putting object with incorrect sizeof(" << sizeof(obj) << ')'
                    << " into buffer with expected element sizeof(" << m_elemSize << ')';
            throw std::runtime_error(explain.str());
        }

        // TODO
    }

    /**
     * @brief Указатель на элемент по номеру в очереди
     * 
     * @param idx номер в очереди (0 - первый, -1 / size-1 - последний)
     * @return T выходной тип указателя
     */
    template<typename T = void*>
#if __cplusplus >= 202002L
    requires std::is_pointer_v<T>
#endif
    T element(idx_t idx) const
    {
        static_assert(std::is_pointer<T>::value, "Template type T must be a pointer type");

        if (idx < 0)
        {
            if (idx < -static_cast<idx_t>(m_size))
            {
                throw std::out_of_range(
                    std::string("Trying to access out-of-range index (") + 
                    std::to_string(idx) + " < -" + std::to_string(m_size) + ')');
            }

            idx = (m_headIdx + idx) % m_capacity;
            return reinterpret_cast<T>(
                reinterpret_cast<uint8_t*>(m_allocator.left()) + idx*m_elemSize);
        }

        if (static_cast<uidx_t>(idx) >= m_size)
        {
            throw std::out_of_range(
                std::string("Trying to access out-of-range index (") + 
                std::to_string(idx) + " >= " + std::to_string(m_size) + ')');
        }

        idx = (m_headIdx - m_size + idx) % m_capacity;
        return reinterpret_cast<T>(
            reinterpret_cast<uint8_t*>(m_allocator.left()) + idx*m_elemSize);
    }

    /**
     * @brief Указатель на элемент по индексу расположения в памяти
     * 
     * @param idx номер в очереди (0 - первый, -1 / size-1 - последний)
     * @return T выходной тип указателя
     */
    template<typename T = void*>
#if __cplusplus >= 202002L
    requires std::is_pointer_v<T>
#endif
    T room(size_t idx) const
    {
        if (idx >= m_capacity)
        {
            throw std::out_of_range(
                std::string("Trying to access out-of-range index (") + 
                std::to_string(idx) + " >= " + std::to_string(m_capacity) + ')');
        }

        return reinterpret_cast<T>(
            reinterpret_cast<uint8_t*>(m_allocator.left()) + idx*m_elemSize);
    }

    /**
     * @brief Указатель на следующий свободный слот для записи элемента.
     * 
     *        Элемент записывает вызывающая сторона. После вызова метода указатель перейдет к следующему слоту, 
     *        в независимости от того, был ли элемент записан или нет.
     * 
     *        Метод полезен для конструирования объектов-вьюшек / заголовков над данными, 
     *        конструктор которых принимает указатель на ячейку памяти, где должны лежать данные.
     * 
     *        Пример: cv::Mat({16,16,16}, CV_32F, ring.seek());
     * 
     * @note Сложность O(1)
     */
    void* seek()
    {
        void* prevHead = m_head;
        this->proceed();
        return prevHead;
    }

    size_t       size()     const noexcept {return m_size;}
    size_t       capacity() const noexcept {return m_capacity;}
    size_t       elemSize() const noexcept {return m_elemSize;}
    const void*  left()     const noexcept {return m_allocator.left();}
    const void*  right()    const noexcept {return m_allocator.right();}
    const void*  data()     const noexcept {return this->left();}

private:
    void proceed()
    {
        m_headIdx = (m_headIdx + 1) % m_capacity;
        m_head = reinterpret_cast<uint8_t*>(m_allocator.left()) + m_headIdx*m_elemSize;
        m_size = RING_BUFFER_H_MIN(m_size + 1, m_capacity);
    }

private:
    RingAllocator m_allocator;
    size_t        m_capacity  {0};
    size_t        m_size      {0};
    size_t        m_elemSize  {0};
    size_t        m_headIdx   {0};
    void*         m_head      {nullptr};
};

#endif // RING_BUFFER_H
