#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "../externals/httplib/httplib.h"
#include "../externals/nlohmann/json.hpp"

#include "NetworkProfiler.h"
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace Engine {

bool NetworkProfiler::Initialize(int port) {
    if (isRunning_) return false;

    port_ = port;
    isRunning_ = true;
    server_ = std::make_unique<httplib::Server>();

    // Setup CORS
    auto set_cors_headers = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    };

    server_->Options(".*", [set_cors_headers](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
    });

    // Endpoint: Get Metrics
    server_->Get("/api/metrics", [this, set_cors_headers](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        set_cors_headers(res);
        
        ProfilerData currentData = GetData();

        nlohmann::json j;
        j["frameNumber"] = currentData.frameNumber;
        j["fps"] = currentData.fps;
        j["deltaTime"] = currentData.deltaTime;
        j["drawCalls"] = currentData.drawCalls;
        j["particleCount"] = currentData.particleCount;
        j["lightCount"] = currentData.lightCount;
        j["playerX"] = currentData.playerX;
        j["playerY"] = currentData.playerY;
        j["playerZ"] = currentData.playerZ;
        j["cpuLogicTimeMs"] = currentData.cpuLogicTimeMs;
        j["gpuRenderTimeMs"] = currentData.gpuRenderTimeMs;
        j["systemRamUsageMB"] = currentData.systemRamUsageMB;
        j["videoRamUsageMB"] = currentData.videoRamUsageMB;
        j["eventMarker"] = currentData.eventMarker;

        res.set_content(j.dump(), "application/json");
    });

    // Endpoint: Get Parameters
    server_->Get("/api/parameters", [this, set_cors_headers](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        set_cors_headers(res);
        std::string paramJson = "{}";
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (paramGetCb_) {
                paramJson = paramGetCb_();
            }
        }
        res.set_content(paramJson, "application/json");
    });

    // Endpoint: Update Parameter
    server_->Post("/api/parameters", [this, set_cors_headers](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string target = j.value("target", "");
            std::string prop = j.value("property", "");
            float val = j.value("value", 0.0f);

            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                if (paramUpdateCb_) {
                    paramUpdateCb_(target, prop, val);
                }
            }
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"status\":\"error\", \"message\":\"Invalid JSON\"}", "application/json");
        }
    });

    // Start server in background thread
    serverThread_ = std::make_unique<std::thread>(&NetworkProfiler::ServerThreadRun, this);

    return true;
}

void NetworkProfiler::ServerThreadRun() {
    std::cout << "[NetworkProfiler] Starting HTTP server on port " << port_ << std::endl;
    server_->listen("0.0.0.0", port_);
}

void NetworkProfiler::Shutdown() {
    if (!isRunning_) return;

    if (server_) {
        server_->stop();
    }
    
    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }

    server_.reset();
    serverThread_.reset();
    isRunning_ = false;
    std::cout << "[NetworkProfiler] Server stopped." << std::endl;
}

NetworkProfiler::NetworkProfiler() = default;

NetworkProfiler::~NetworkProfiler() {
    Shutdown();
}

void NetworkProfiler::CommitFrame() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_.frameNumber++;
}

} // namespace Engine
