#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <filesystem>
#include <string>
#include <fstream>
#include "../externals/Streamline/include/sl.h"
#include "../externals/Streamline/include/sl_dlss.h"
namespace Engine {
bool VerifyDlssDll(const wchar_t* path);
// Optional, dynamically loaded SDK. Native D3D12 remains usable without the DLLs.
class DlssRuntime {
    HMODULE module_=nullptr;bool initialized_=false,supported_=false;
    Microsoft::WRL::ComPtr<ID3D12Device> hookDevice_;
    PFun_slShutdown* shutdown_=nullptr;PFun_slSetD3DDevice* device_=nullptr;
    PFun_slUpgradeInterface* upgrade_=nullptr;PFun_slGetNewFrameToken* token_=nullptr;
    PFun_slSetConstants* constants_=nullptr;PFun_slSetTagForFrame* tag_=nullptr;
    PFun_slEvaluateFeature* evaluate_=nullptr;PFun_slIsFeatureSupported* support_=nullptr;
    PFun_slGetFeatureFunction* feature_=nullptr;
    PFun_slDLSSSetOptions* options_=nullptr;PFun_slDLSSGetOptimalSettings* optimal_=nullptr;
    template<class T> bool load(T*& function,const char* name){function=reinterpret_cast<T*>(GetProcAddress(module_,name));return function!=nullptr;}
    std::string status_="SDK unavailable";uint32_t frame_=0;
public:
    static DlssRuntime& Get(){static DlssRuntime instance;return instance;}
    bool Supported()const{return supported_;}
    const std::string& Status()const{return status_;}
    void Initialize(){
        wchar_t executable[32768]{};auto length=GetModuleFileNameW(nullptr,executable,32768);
        auto folder=length&&length<32768?std::filesystem::path(executable).parent_path()/L"Streamline":std::filesystem::path{};
        // Release ships a self-contained runtime beside its EXE. Development
        // can still use the SDK installed in the repository.
        if(!std::filesystem::exists(folder/L"sl.interposer.dll"))folder=std::filesystem::absolute("externals/Streamline/bin");
        if(!std::filesystem::exists(folder/"sl.interposer.dll")){status_="SDK DLLs missing";return;}
        for(auto name:{L"sl.interposer.dll",L"sl.common.dll",L"sl.dlss.dll",L"nvngx_dlss.dll"})
            if(!VerifyDlssDll((folder/name).wstring().c_str())){status_="SDK signature verification failed: "+std::filesystem::path(name).string();return;}
        module_=LoadLibraryExW((folder/L"sl.interposer.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module_){status_="SDK load failed";return;}
        PFun_slInit* init=nullptr;
        if(!load(init,"slInit")||!load(shutdown_,"slShutdown")||!load(device_,"slSetD3DDevice")||!load(upgrade_,"slUpgradeInterface")||
           !load(token_,"slGetNewFrameToken")||!load(constants_,"slSetConstants")||!load(tag_,"slSetTagForFrame")||
           !load(evaluate_,"slEvaluateFeature")||!load(support_,"slIsFeatureSupported")||!load(feature_,"slGetFeatureFunction")){status_="SDK API mismatch";return;}
        std::wstring pluginPath=folder.wstring();const wchar_t* paths[]={pluginPath.c_str()};sl::Feature features[]={sl::kFeatureDLSS};
        sl::Preferences preferences{};preferences.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        preferences.featuresToLoad=features;preferences.numFeaturesToLoad=1;preferences.pathsToPlugins=paths;preferences.numPathsToPlugins=1;
        preferences.engine=sl::EngineType::eCustom;preferences.engineVersion="neo_Engine 1.0";
        preferences.projectId="a57817c3-85a6-4cbe-94b3-ffdbd561c623";preferences.showConsole=false;
        preferences.logMessageCallback=[](sl::LogType,const char* message){OutputDebugStringA(message);if(wcsstr(GetCommandLineW(),L"--dlss-smoke"))std::ofstream("tests/out/dlss-sdk.log",std::ios::app)<<message<<'\n';};
        auto result=init(preferences,sl::kSDKVersion);if(result!=sl::Result::eOk){status_="SDK init error "+std::to_string(uint32_t(result));return;}
        initialized_=true;
        status_="Waiting for D3D12 device";
    }
    void SetDevice(ID3D12Device* device,LUID luid){
        if(!initialized_)return;
        auto deviceResult=device_(device);if(deviceResult!=sl::Result::eOk){status_="SDK device error "+std::to_string(uint32_t(deviceResult));return;}
        void* function=nullptr;
        if(feature_(sl::kFeatureDLSS,"slDLSSSetOptions",function)!=sl::Result::eOk){status_="DLSS plugin unavailable";return;}options_=reinterpret_cast<PFun_slDLSSSetOptions*>(function);
        if(feature_(sl::kFeatureDLSS,"slDLSSGetOptimalSettings",function)!=sl::Result::eOk){status_="DLSS settings API unavailable";return;}optimal_=reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(function);
        sl::AdapterInfo adapter{};adapter.deviceLUID=reinterpret_cast<uint8_t*>(&luid);adapter.deviceLUIDSizeInBytes=sizeof(luid);
        auto result=support_(sl::kFeatureDLSS,adapter);supported_=result==sl::Result::eOk;
        status_=supported_?"DLSS SR available":"DLSS unsupported ("+std::to_string(uint32_t(result))+")";
        OutputDebugStringA(("[DLSS] "+status_+"\n").c_str());
    }
    void UpgradeSwapchain(Microsoft::WRL::ComPtr<IDXGISwapChain4>& swapchain){
        if(!initialized_)return;void* pointer=swapchain.Detach();auto result=upgrade_(&pointer);
        swapchain.Attach(static_cast<IDXGISwapChain4*>(pointer));if(result!=sl::Result::eOk){supported_=false;status_="Presentation hook failed";}
    }
    void UpgradeFactory(Microsoft::WRL::ComPtr<IDXGIFactory7>& factory){if(!initialized_)return;void* pointer=factory.Detach();upgrade_(&pointer);factory.Attach(static_cast<IDXGIFactory7*>(pointer));}
    HRESULT CreateQueue(ID3D12Device* device,const D3D12_COMMAND_QUEUE_DESC& description,ID3D12CommandQueue** queue){
        if(!initialized_)return device->CreateCommandQueue(&description,IID_PPV_ARGS(queue));
        hookDevice_=device;void* pointer=hookDevice_.Detach();upgrade_(&pointer);hookDevice_.Attach(static_cast<ID3D12Device*>(pointer));
        return hookDevice_->CreateCommandQueue(&description,IID_PPV_ARGS(queue));
    }
    bool QualitySize(UINT width,UINT height,UINT& inputWidth,UINT& inputHeight){
        if(!supported_)return false;sl::DLSSOptions options{};options.mode=sl::DLSSMode::eMaxQuality;options.outputWidth=width;options.outputHeight=height;
        sl::DLSSOptimalSettings settings{};if(optimal_(options,settings)!=sl::Result::eOk)return false;
        inputWidth=settings.optimalRenderWidth;inputHeight=settings.optimalRenderHeight;return inputWidth>0&&inputHeight>0;
    }
    bool Evaluate(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* output,const sl::Constants& camera,UINT width,UINT height){
        if(!supported_)return false;sl::ViewportHandle viewport(0);sl::DLSSOptions options{};options.mode=sl::DLSSMode::eMaxQuality;
        options.outputWidth=width;options.outputHeight=height;options.colorBuffersHDR=sl::Boolean::eTrue;options.useAutoExposure=sl::Boolean::eTrue;
        sl::FrameToken* frame=nullptr;uint32_t index=frame_++;
        auto result=options_(viewport,options);if(result==sl::Result::eOk)result=token_(frame,&index);
        if(result==sl::Result::eOk)result=constants_(camera,*frame,viewport);
        sl::Resource input(sl::ResourceType::eTex2d,color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sl::Resource d(sl::ResourceType::eTex2d,depth,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),mv(sl::ResourceType::eTex2d,motion,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sl::Resource out(sl::ResourceType::eTex2d,output,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto rd=color->GetDesc();sl::Extent inputExtent{0,0,UINT(rd.Width),rd.Height},outExtent{0,0,width,height};
        sl::ResourceTag tags[]={ {&input,sl::kBufferTypeScalingInputColor,sl::ResourceLifecycle::eOnlyValidNow,&inputExtent},
            {&d,sl::kBufferTypeDepth,sl::ResourceLifecycle::eOnlyValidNow,&inputExtent},{&mv,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eOnlyValidNow,&inputExtent},
            {&out,sl::kBufferTypeScalingOutputColor,sl::ResourceLifecycle::eOnlyValidNow,&outExtent} };
        if(result==sl::Result::eOk)result=tag_(*frame,viewport,tags,4,list);
        const sl::BaseStructure* inputs[]={&viewport};if(result==sl::Result::eOk)result=evaluate_(sl::kFeatureDLSS,*frame,inputs,1,list);
        if(result!=sl::Result::eOk){supported_=false;status_="DLSS evaluation failed ("+std::to_string(uint32_t(result))+")";OutputDebugStringA((status_+"\n").c_str());return false;}
        return true;
    }
    void Shutdown(){if(initialized_)shutdown_();initialized_=supported_=false;}
    void Unload(){hookDevice_.Reset();if(module_)FreeLibrary(module_);module_=nullptr;}
};
}
