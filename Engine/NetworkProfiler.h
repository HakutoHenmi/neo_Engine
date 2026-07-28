#pragma once
#include <cstdint>
#include <string>
#include <thread>
#include <memory>
#include <mutex>
#include <functional>
#include <vector>

namespace httplib {
    class Server;
}

namespace Engine {

struct ProfilerData {
    uint32_t frameNumber;
    float fps;
    float deltaTime;
    uint32_t drawCalls;
    uint32_t particleCount;
    uint32_t lightCount;
    float playerX;
    float playerY;
    float playerZ;
    float cpuLogicTimeMs;
    float gpuRenderTimeMs;
    float systemRamUsageMB;
    float videoRamUsageMB;
    std::string eventMarker;
};

struct AssetInfo {
    std::string name;
    std::string type;
    float sizeMB;
    std::string details;
    int refCount;
    std::string status;
};

class NetworkProfiler {
public:
    static NetworkProfiler& GetInstance() {
        static NetworkProfiler instance;
        return instance;
    }

    bool Initialize(int port = 8080);
    void Shutdown();
    
    // Called at the end of every frame to update internal metrics
    void CommitFrame();

    // Data Setters
    void SetFPS(float fps) { std::lock_guard<std::mutex> lock(dataMutex_); data_.fps = fps; }
    void SetDeltaTime(float dt) { std::lock_guard<std::mutex> lock(dataMutex_); data_.deltaTime = dt; }
    void SetDrawCalls(uint32_t count) { std::lock_guard<std::mutex> lock(dataMutex_); data_.drawCalls = count; }
    void SetParticleCount(uint32_t count) { std::lock_guard<std::mutex> lock(dataMutex_); data_.particleCount = count; }
    void SetLightCount(uint32_t count) { std::lock_guard<std::mutex> lock(dataMutex_); data_.lightCount = count; }
    void SetPlayerPos(float x, float y, float z) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_.playerX = x; data_.playerY = y; data_.playerZ = z;
    }
    void SetCpuLogicTime(float ms) { std::lock_guard<std::mutex> lock(dataMutex_); data_.cpuLogicTimeMs = ms; }
    void SetGpuRenderTime(float ms) { std::lock_guard<std::mutex> lock(dataMutex_); data_.gpuRenderTimeMs = ms; }
    void SetSystemRamUsage(float mb) { std::lock_guard<std::mutex> lock(dataMutex_); data_.systemRamUsageMB = mb; }
    void SetVideoRamUsage(float mb) { std::lock_guard<std::mutex> lock(dataMutex_); data_.videoRamUsageMB = mb; }
    void SetEventMarker(const std::string& marker) { std::lock_guard<std::mutex> lock(dataMutex_); data_.eventMarker = marker; }

    ProfilerData GetData() { 
        std::lock_guard<std::mutex> lock(dataMutex_);
        return data_; 
    }

    // Asset tracking
    void RegisterAsset(const std::string& name, const std::string& type, float sizeMB, const std::string& details) {
        std::lock_guard<std::mutex> lock(assetMutex_);
        AssetInfo info;
        info.name = name;
        info.type = type;
        info.sizeMB = sizeMB;
        info.details = details;
        info.refCount = 1;
        info.status = "loaded";
        registeredAssets_.push_back(info);
    }

    std::vector<AssetInfo> GetAssets() {
        std::lock_guard<std::mutex> lock(assetMutex_);
        return registeredAssets_;
    }

    using ParameterUpdateCallback = std::function<void(const std::string& target, const std::string& property, float value)>;
    using ParameterGetCallback = std::function<std::string()>; // Returns JSON string of current parameters

    void SetParameterUpdateCallback(ParameterUpdateCallback cb) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        paramUpdateCb_ = cb;
    }

    void SetParameterGetCallback(ParameterGetCallback cb) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        paramGetCb_ = cb;
    }

private:
    NetworkProfiler();
    ~NetworkProfiler();

    void ServerThreadRun();

    ProfilerData data_ = {};
    std::mutex dataMutex_;

    std::vector<AssetInfo> registeredAssets_;
    std::mutex assetMutex_;

    ParameterUpdateCallback paramUpdateCb_;
    ParameterGetCallback paramGetCb_;

    std::unique_ptr<httplib::Server> server_;
    std::unique_ptr<std::thread> serverThread_;
    int port_ = 8080;
    bool isRunning_ = false;
};

} // namespace Engine
