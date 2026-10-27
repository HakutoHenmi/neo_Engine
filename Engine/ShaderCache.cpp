#include "ShaderCache.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace Engine::ShaderCache {
namespace {

namespace fs = std::filesystem;

// 128 ビットのキー (FNV-1a を 2 つの初期値で回す)
struct Key {
	uint64_t a = 14695981039346656037ull, b = 0x84222325cbf29ce4ull;
	void Add(const void* data, size_t size) {
		auto* p = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) { a = (a ^ p[i]) * 1099511628211ull; b = (b ^ p[i]) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull; }
	}
	void Add(const std::string& text) { Add(text.data(), text.size()); const char end = 0; Add(&end, 1); }
	std::wstring Name() const { wchar_t s[40]; swprintf_s(s, L"%016llx%016llx.cso", (unsigned long long)a, (unsigned long long)b); return s; }
};

bool ReadFile(const fs::path& path, std::string& out) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}

// 本体と #include "..." を辿った全ファイルの中身をキーに加える (標準の include 処理と同じく、取り込む側のフォルダから探す)
void AddSourceTree(Key& key, const fs::path& path, std::set<fs::path>& visited) {
	std::error_code ec;
	fs::path normal = fs::weakly_canonical(path, ec);
	if (ec) normal = path;
	if (!visited.insert(normal).second) return;
	std::string text;
	if (!ReadFile(normal, text)) { key.Add("missing:" + normal.filename().string()); return; }
	key.Add(normal.filename().string());
	key.Add(text);
	for (size_t at = text.find("#include"); at != std::string::npos; at = text.find("#include", at + 8)) {
		const size_t open = text.find_first_of("\"<", at + 8), line = text.find('\n', at);
		if (open == std::string::npos || (line != std::string::npos && open > line)) continue;
		const size_t close = text.find_first_of("\">", open + 1);
		if (close == std::string::npos || (line != std::string::npos && close > line)) continue;
		AddSourceTree(key, normal.parent_path() / text.substr(open + 1, close - open - 1), visited);
	}
}

bool WriteMode() {
	static const bool write = wcsstr(GetCommandLineW(), L"--write-shader-cache") != nullptr;
	return write;
}
fs::path ShippedDir() { return fs::path(L"Resources") / L"ShaderCache"; }
fs::path UserDir() {
	wchar_t buffer[MAX_PATH]{};
	const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	return n > 0 && n < MAX_PATH ? fs::path(buffer) / L"LiquidTime" / L"ShaderCache" : fs::path{};
}

// DXBC の形と、ヘッダーに書かれた全体の大きさを確かめてから使う (途中で切れたファイルは捨てる)
bool LoadBlob(const fs::path& path, ID3DBlob** blob) {
	std::string bytes;
	if (path.empty() || !ReadFile(path, bytes) || bytes.size() < 32 || bytes.compare(0, 4, "DXBC") != 0) return false;
	uint32_t total = 0; memcpy(&total, bytes.data() + 24, 4);
	if (total != bytes.size() || FAILED(D3DCreateBlob(bytes.size(), blob))) return false;
	memcpy((*blob)->GetBufferPointer(), bytes.data(), bytes.size());
	return true;
}
void StoreBlob(const fs::path& dir, const std::wstring& name, ID3DBlob* blob) {
	if (dir.empty()) return;
	std::error_code ec;
	fs::create_directories(dir, ec);
	// 別名で書いてから置き換える (書き込み途中のファイルを読ませない)
	const fs::path target = dir / name, temp = dir / (name + L".tmp");
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file) return;
		file.write(static_cast<const char*>(blob->GetBufferPointer()), std::streamsize(blob->GetBufferSize()));
		if (!file) return;
	}
	if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) fs::remove(temp, ec);
}

template <class CompileFn>
HRESULT Cached(const Key& key, ID3DBlob** blob, ID3DBlob** errors, CompileFn compile) {
	const std::wstring name = key.Name();
	if (errors) *errors = nullptr;
	if (LoadBlob(ShippedDir() / name, blob)) return S_OK;
	if (!WriteMode() && !UserDir().empty() && LoadBlob(UserDir() / name, blob)) return S_OK;
	const HRESULT hr = compile();
	if (SUCCEEDED(hr) && *blob) StoreBlob(WriteMode() ? ShippedDir() : UserDir(), name, *blob);
	return hr;
}

Key BaseKey(const char* kind, const char* entry, const char* target, UINT flags) {
	Key key;
	key.Add(std::string("neo-shader-cache-v1:") + kind);
	key.Add(entry ? entry : "");
	key.Add(target ? target : "");
	key.Add(&flags, sizeof(flags));
	return key;
}

} // namespace

HRESULT CompileFromFile(const std::wstring& path, const char* entry, const char* target, UINT flags, ID3DBlob** blob, ID3DBlob** errors) {
	Key key = BaseKey("file", entry, target, flags);
	std::set<fs::path> visited;
	AddSourceTree(key, path, visited);
	return Cached(key, blob, errors, [&] { return D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, flags, 0, blob, errors); });
}

HRESULT Compile(const char* source, size_t size, const char* entry, const char* target, UINT flags, ID3DBlob** blob, ID3DBlob** errors) {
	Key key = BaseKey("source", entry, target, flags);
	key.Add(source, size);
	return Cached(key, blob, errors, [&] { return D3DCompile(source, size, nullptr, nullptr, nullptr, entry, target, flags, 0, blob, errors); });
}

} // namespace Engine::ShaderCache
