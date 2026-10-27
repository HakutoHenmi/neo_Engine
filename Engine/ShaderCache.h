#pragma once
// HLSL のコンパイル結果 (DXBC) をファイルに残し、次回からはコンパイルせずに読む。
// キーはターゲット・エントリ・フラグと、シェーダー本体と #include した全ファイルの中身から作る。
// 中身が 1 文字でも変われば別のキーになるので、古い結果を使うことはない。
// 読む順: 配布物の Resources/ShaderCache → ユーザーごとの %LOCALAPPDATA%/LiquidTime/ShaderCache。
// --write-shader-cache を付けると、配布用に Resources/ShaderCache へ書き出す。
#include <d3dcommon.h>
#include <string>

namespace Engine::ShaderCache {
// D3DCompileFromFile (標準の include 処理) と同じ結果を返す
HRESULT CompileFromFile(const std::wstring& path, const char* entry, const char* target, UINT flags, ID3DBlob** blob, ID3DBlob** errors);
// D3DCompile と同じ結果を返す (include なしの文字列)
HRESULT Compile(const char* source, size_t size, const char* entry, const char* target, UINT flags, ID3DBlob** blob, ID3DBlob** errors);
}
