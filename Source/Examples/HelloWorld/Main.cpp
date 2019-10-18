#include "TeCoreApplication.h"
#include "RenderAPI/TeVideoMode.h"

#if TE_PLATFORM == TE_PLATFORM_WIN32
#include <windows.h>

int CALLBACK WinMain(
    _In_  HINSTANCE hInstance,
    _In_  HINSTANCE hPrevInstance,
    _In_  LPSTR lpCmdLine,
    _In_  int nCmdShow
)
#else
int main()
#endif
{
    te::START_UP_DESC desc;

    desc.RenderAPI = "TeGLRenderAPI";
    desc.Renderer = TE_RENDERER_MODULE;
    desc.Audio = TE_AUDIO_MODULE;

    desc.Importers = {
        "TeFreeImgImporter",
        "TeObjectImporter"
    };

    desc.WindowDesc.Mode = te::VideoMode(960, 480);

    te::CoreApplication::StartUp(desc);

    te::String startMessage = "Application start";
    TE_PRINT(startMessage);

    te::gCoreApplication().RunMainLoop();
    te::CoreApplication::ShutDown();

    return 0;
}
