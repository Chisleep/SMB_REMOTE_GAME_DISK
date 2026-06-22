#pragma once

#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <atomic>
#include <ctime>

namespace rgh {

// 内存缓存项
struct CacheEntry {
    std::vector<uint8_t> data;   // 数据块
    uint64_t offset;             // 在文件中的偏移
    size_t size;                 // 数据大小
    std::time_t lastAccess;      // 最后访问时间
    int hitCount;                // 命中次数
};

// 文件级缓存索引: 文件路径 -> 数据块列表
// 使用 LRU 策略管理总内存使用量
class MemoryCache {
public:
    static MemoryCache& instance();

    // 初始化缓存，maxSizeMB 为最大内存使用量(MB)
    void init(size_t maxSizeMB);

    // 从缓存读取数据，返回实际命中字节数（可能部分命中）
    // 未命中部分由调用者从 SMB 读取
    int read(const std::string& filePath, uint64_t offset, void* buffer, uint32_t size);

    // 将从 SMB 读取的数据写入缓存
    void put(const std::string& filePath, uint64_t offset, const void* buffer, uint32_t size);

    // 使某个文件的缓存失效（文件被修改时调用）
    void invalidate(const std::string& filePath);

    // 清空所有缓存
    void clear();

    // 缓存统计
    struct Stats {
        std::atomic<uint64_t> totalReads{0};      // 总读取请求
        std::atomic<uint64_t> cacheHits{0};       // 缓存命中
        std::atomic<uint64_t> cacheMisses{0};     // 缓存未命中
        std::atomic<uint64_t> bytesServed{0};     // 从缓存服务的字节数
        std::atomic<uint64_t> bytesCached{0};     // 当前缓存字节数
        std::atomic<uint64_t> evictions{0};       // 淘汰次数
    };

    const Stats& stats() const { return stats_; }
    double hitRate() const;

    // 目录元数据缓存（文件列表）
    struct DirCacheEntry {
        std::vector<std::string> entries;  // 文件名列表
        std::time_t cachedAt;
    };

    bool getDirCache(const std::string& dirPath, std::vector<std::string>& entries, uint32_t ttlSec);
    void putDirCache(const std::string& dirPath, const std::vector<std::string>& entries);

private:
    MemoryCache() = default;

    // 缓存键: filePath + offset 对齐到块大小
    using BlockKey = std::pair<std::string, uint64_t>;
    struct BlockKeyHash {
        size_t operator()(const BlockKey& k) const {
            return std::hash<std::string>()(k.first) ^ (std::hash<uint64_t>()(k.second) << 1);
        }
    };

    static constexpr size_t BlockSize = 64 * 1024; // 64KB 块

    // LRU 链表 + 哈希表
    std::list<BlockKey> lruList_;
    std::unordered_map<BlockKey, std::pair<std::vector<uint8_t>, std::list<BlockKey>::iterator>, BlockKeyHash> blockMap_;

    // 目录缓存
    std::unordered_map<std::string, DirCacheEntry> dirCache_;

    std::mutex mutex_;
    size_t maxSizeBytes_ = 512 * 1024 * 1024; // 默认 512MB
    size_t currentSizeBytes_ = 0;
    Stats stats_;

    // 淘汰直到满足内存限制
    void evictIfNeeded();
    uint64_t blockAlign(uint64_t offset) const { return (offset / BlockSize) * BlockSize; }
};

} // namespace rgh
