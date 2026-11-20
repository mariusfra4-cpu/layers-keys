#include "OrtRuntime.h"

#include <algorithm>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace lsplit
{

namespace
{
    bool gLoaded = false;
    bool gHasDml = false;
    // volontairement jamais détruit : détruire l'environnement pendant la fermeture du
    // programme (après le nettoyage interne d'onnxruntime) provoque un plantage.
    Ort::Env* gEnv = nullptr;
    std::string gVersion;

    using AppendDmlFn = OrtStatus* (ORT_API_CALL*) (OrtSessionOptions*, int);
    AppendDmlFn gAppendDml = nullptr;

   #if defined(_WIN32)
    std::wstring widen (const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
        std::wstring w ((size_t) n, L'\0');
        MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
        return w;
    }

    std::string lastErrorText()
    {
        const DWORD code = GetLastError();
        wchar_t* buf = nullptr;
        FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, code, 0, (LPWSTR) &buf, 0, nullptr);
        std::string out = "code " + std::to_string (code);
        if (buf != nullptr)
        {
            const int n = WideCharToMultiByte (CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            std::string s ((size_t) std::max (0, n - 1), '\0');
            WideCharToMultiByte (CP_UTF8, 0, buf, -1, s.data(), n, nullptr, nullptr);
            LocalFree (buf);
            out += " : " + s;
        }
        return out;
    }
   #endif
}

std::basic_string<ORTCHAR_T> toOrtPath (const std::string& utf8)
{
   #if defined(_WIN32)
    return widen (utf8);
   #else
    return utf8;
   #endif
}

std::mutex& OrtRuntime::mutex()
{
    static std::mutex m;
    return m;
}

bool OrtRuntime::isLoaded()
{
    std::lock_guard<std::mutex> lock (mutex());
    return gLoaded;
}

bool OrtRuntime::directMLAvailable()
{
    std::lock_guard<std::mutex> lock (mutex());
    return gHasDml;
}

std::string OrtRuntime::version()
{
    std::lock_guard<std::mutex> lock (mutex());
    return gVersion;
}

bool OrtRuntime::load (const std::string& dir, std::string& error)
{
    std::lock_guard<std::mutex> lock (mutex());
    if (gLoaded)
        return true;

    using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*) ();
    GetApiBaseFn getApiBase = nullptr;

   #if defined(_WIN32)
    std::string base = dir;
    if (! base.empty() && base.back() != '\\' && base.back() != '/')
        base += '\\';

    // DirectML.dll d'abord, par son chemin complet : onnxruntime.dll le trouvera déjà en
    // mémoire au lieu de prendre l'ancienne copie de System32.
    const std::wstring dmlPath = widen (base + "DirectML.dll");
    HMODULE dml = LoadLibraryExW (dmlPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    const std::wstring ortPath = widen (base + "onnxruntime.dll");
    HMODULE ort = LoadLibraryExW (ortPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (ort == nullptr)
    {
        error = "Impossible de charger onnxruntime.dll (" + base + ") — " + lastErrorText();
        return false;
    }
    getApiBase = reinterpret_cast<GetApiBaseFn> (GetProcAddress (ort, "OrtGetApiBase"));
    if (dml != nullptr)
        gAppendDml = reinterpret_cast<AppendDmlFn> (GetProcAddress (ort, "OrtSessionOptionsAppendExecutionProvider_DML"));
   #else
    std::string base = dir;
    if (! base.empty() && base.back() != '/')
        base += '/';
   #if defined(__APPLE__)
    const std::string libName = "libonnxruntime.dylib";
   #else
    const std::string libName = "libonnxruntime.so";
   #endif
    void* ort = dlopen ((base + libName).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (ort == nullptr)
    {
        const char* e = dlerror();
        error = "Impossible de charger " + base + libName + " — " + (e != nullptr ? e : "?");
        return false;
    }
    getApiBase = reinterpret_cast<GetApiBaseFn> (dlsym (ort, "OrtGetApiBase"));
   #endif

    if (getApiBase == nullptr)
    {
        error = "onnxruntime : fonction OrtGetApiBase introuvable.";
        return false;
    }

    const OrtApiBase* apiBase = getApiBase();
    const OrtApi* api = apiBase != nullptr ? apiBase->GetApi (ORT_API_VERSION) : nullptr;
    if (api == nullptr)
    {
        error = "onnxruntime : version incompatible (API " + std::to_string (ORT_API_VERSION) + " demandée).";
        return false;
    }

    Ort::InitApi (api);
    gVersion = apiBase->GetVersionString();

    try
    {
        gEnv = new Ort::Env (ORT_LOGGING_LEVEL_ERROR, "LayersSplit");
    }
    catch (const std::exception& e)
    {
        error = std::string ("onnxruntime : ") + e.what();
        return false;
    }

    gHasDml = (gAppendDml != nullptr);
    gLoaded = true;
    return true;
}

Ort::Env& OrtRuntime::env()
{
    return *gEnv;
}

bool OrtRuntime::appendDirectML (Ort::SessionOptions& options, std::string& error)
{
    if (gAppendDml == nullptr)
    {
        error = "DirectML indisponible";
        return false;
    }

    // Réglages exigés par DirectML.
    options.DisableMemPattern();
    options.SetExecutionMode (ORT_SEQUENTIAL);

    OrtStatus* st = gAppendDml (options, 0);
    if (st != nullptr)
    {
        error = Ort::GetApi().GetErrorMessage (st);
        Ort::GetApi().ReleaseStatus (st);
        return false;
    }
    return true;
}

} // namespace lsplit
