#if __has_include(<renderdoc_app.h>) && (defined(_WIN32) || defined(__linux__))
#define ENABLE_RENDERDOC
#include <renderdoc_app.h>
#endif

#ifdef _WIN32
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

#include <cassert>

class RenderDoc {
private:
#ifdef ENABLE_RENDERDOC
    RENDERDOC_API_1_6_0 *mApi = nullptr;
#endif
public:
    inline RenderDoc() {
#ifdef ENABLE_RENDERDOC
#ifdef _WIN32
        // At init, on windows
        if(HMODULE mod = GetModuleHandleA("renderdoc.dll"))
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
#else
        // At init, on linux/android.
        // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
        if(void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
#endif
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void **)&mApi);
            assert(ret == 1);
        }
#endif // #ifdef ENABLE_RENDERDOC
    }

    inline void startCapture() {
#ifdef ENABLE_RENDERDOC
        if(mApi)
            mApi->StartFrameCapture(nullptr, nullptr);
#endif
    }
    inline void endCapture() {
#ifdef ENABLE_RENDERDOC
        if(mApi)
            mApi->EndFrameCapture(nullptr, nullptr);
#endif
    }

    inline void *getApi() const {
#ifdef ENABLE_RENDERDOC
        return mApi;
#else
        return nullptr;
#endif
    }
};
