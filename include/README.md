ReShade API
===========

The ReShade API is a toolset that lets you interact with the resources and rendering commands of the application ReShade is loaded into via events. It [abstracts](#abstraction) away differences between the various underlying graphics API ReShade supports (Direct3D 9/10/11/12, OpenGL and Vulkan) to make it possible to write add-ons that work across a wide range of applications, regardless of the graphics API they use.

A ReShade add-on is simply a DLL that uses the header-only ReShade API to register events and do work in the callbacks. There are no further requirements, no functions need to be exported and no libraries need to be linked against. Simply add this include directory to your DLL project and include the [`reshade.hpp`](reshade.hpp) header to get started.

Here is a very basic code example of an add-on that registers a callback that gets executed every time a new frame is presented to the screen:

```cpp
#define RESHADE_ADDON_IMPL // Define this before including the ReShade header in exactly one source file
#include <reshade.hpp>

static void on_present(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain)
{
	// ...
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Call 'reshade::init_addon' before you call any other function of the ReShade API
        // This will look for the ReShade instance in the current process and initialize the API when found
        if (!reshade::init_addon())
            return FALSE;
        // This registers a callback for the 'present' event, which occurs every time a new frame is presented to the screen
        // The function signature has to match the type defined by 'reshade::addon_event_traits<reshade::addon_event::present>::decl'
        // For more details check the inline documentation for each event in 'reshade_events.hpp'
        reshade::register_event<reshade::addon_event::present>(on_present);
        break;
    case DLL_PROCESS_DETACH:
        // Before the add-on is unloaded, be sure to unregister any event callbacks that where previously registered
        reshade::unregister_event<reshade::addon_event::present>(on_present);
        break;
    }
    return TRUE;
}
```

For more complex examples, see also the built-in add-ons in [source/addon](../source/addon).

## Overlays

It is also supported to add an overlay, which can e.g. be used to display debug information or interact with the user in-application.
Overlays are created with the use of [Dear ImGui](https://github.com/ocornut/imgui/). Including the [`reshade.hpp`](reshade.hpp) header after `imgui.h` will automatically overwrite all Dear ImGui functions to use the instance created and managed by ReShade. This means all you have to do is include these two headers and use Dear ImGui as usual (without actually having to build its source code files, only the header files are needed):

```cpp
#include <imgui.h>
#include <reshade.hpp>

bool g_popup_window_visible = false;

static void draw_debug_overlay(reshade::api::effect_runtime *runtime, void *imgui_context)
{
    ImGui::TextUnformatted("Some text");

    if (ImGui::Button("Press me to open an additional popup window"))
        g_popup_window_visible = true;

    if (g_popup_window_visible)
    {
        ImGui::Begin("Popup", &g_popup_window_visible);
        ImGui::TextUnformatted("Some other text");
        ImGui::End();
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::init_addon())
            return FALSE;
        // This registers a new overlay with the specified name with ReShade.
        // It will be displayed as an additional window when the ReShade overlay is opened.
        // Its contents are defined by Dear ImGui commands issued in the specified callback function.
        reshade::register_overlay("Test", draw_debug_overlay);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_overlay("Test");
        break;
    }
    return TRUE;
}
```

## Abstraction

This graphics API abstraction is modeled after to the Vulkan API, so much of terminology used should be familiar to developers that have used Vulkan before.

Detailed documentation for all the classes and methods can also be found inside the headers (see [`reshade_api.hpp`](reshade_api.hpp) for the abstraction object classes and [`reshade_events.hpp`](reshade_events.hpp) for the list of available events and the callback function signatures they expect).

The base object everything else is created from is a `device`. This represents a logical rendering device that is typically mapped to a GPU (but may also be mapped to multiple GPUs). ReShade will call the `addon_event::init_device` event after the application created a device, which can e.g. be used to do some initialization work that only has to happen once. The `addon_event::destroy_device` event is called before this device is destroyed again, which can be used to perform clean up work.
```c++
using namespace reshade::api;

// Example callback function that can be registered via 'reshade::register_event<reshade::addon_event::init_device>(on_init_device)'.
static void on_init_device(device *device)
{
    // In case one wants to do something with the native graphics API object, rather than doing all work
    // through the ReShade API, can retrieve it as follows:
    if (device->get_api() == device_api::d3d11)
    {
        ID3D11Device *const d3d11_device = (ID3D11Device *)device->get_native_object();
        // ...
    }

    // But preferably things can be done through the graphics API abstraction, e.g. to create a new 800x600 texture in GPU memory:
    resource texture = {};
    const resource_desc desc(800, 600, 1, 1, format::r8g8b8a8_unorm, 1, memory_heap::gpu_only, resource_usage::shader_resource | resource_usage::render_target);
    if (!device->create_resource(desc, nullptr, resource_usage::undefined, &texture))
    {
        // Error handling ...
    }

    // ...
}
```

To submit rendering commands, an application has to record them into `command_list`s and then submit those to a `command_queue`. In some graphics APIs there is only a single implicit command list and queue, but modern ones like Direct3D 12 and Vulkan allow the creation of multiple ones for more efficient multi-threaded rendering. ReShade will call the `addon_event::init_command_list` and `addon_event::init_command_queue` after any such object was created by the application (including the implicit ones for older graphics APIs). Similarily, `addon_event::destroy_command_list` and `addon_event::destroy_command_queue` are called upon their destruction.\
ReShade will also pass the current command list object to every command event, like `addon_event::draw`, `addon_event::dispatch` and so on, which can be used to add additional commands to that command list or replace those by the application.
```c++
using namespace reshade::api;

// Example callback function that can be registered via 'reshade::register_event<reshade::addon_event::draw>(on_draw)'.
static bool on_draw(command_list *cmd_list, uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance)
{
    // Clear the current render targets to red every time a single triangle is drawn
    if (vertices == 3 && instances == 1)
    {
        const float clear_color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        cmd_list->clear_attachments(attachment_type::color, clear_color, 0, 0);
    }

    // Return 'true' to prevent this application command from actually being executed (e.g. because already having added a new command that should replace it via 'cmd_list->draw(...)' or similar).
    // Return 'false' to leave it unaffected.
    return false;
}
```

Showing results on the screen is done through a `swapchain` object. This is a collection of back buffers that the application can render into, which will eventually be presented to the screen. There may be multiple swap chains, if for example the application is rendering to multiple windows, or to the screen and to a VR headset. ReShade again will call the `addon_event::init_swapchain` event after such an object was created by the application (and `addon_event::destroy_swapchain` on destruction). In addition it will already call the `addon_event::create_swapchain` event before that happens, so an add-on may modify how the swap chain should be created. For example, to force the resolution to present at to a specific value, one can do the following:
```c++
using namespace reshade::api;

// Example callback function that can be registered via 'reshade::register_event<reshade::addon_event::create_swapchain>(on_create_swapchain)'.
static bool on_create_swapchain(resource_desc &buffer_desc, void *hwnd)
{
    // Change resolution to 1920x1080 if the application is trying to create a swap chain at 800x600.
    if (buffer_desc.texture.width == 800 &&
        buffer_desc.texture.height == 600)
    {
        buffer_desc.texture.width = 1920;
        buffer_desc.texture.height = 1080;
    }

    // Return 'true' for ReShade to overwrite the swap chain description of the application with the values set in this callback.
    // Return 'false' to leave it unaffected.
    return true;
}
```

ReShade associates an independent post-processing effect runtime with every swap chain (can be retrieved by calling `swapchain::get_effect_runtime()`). This is the runtime one usually controls via the ReShade overlay, but it can also be controlled programatically via the ReShade API using the methods of the `effect_runtime` object.

In contrast to these basic API abstraction objects, any buffers, textures, pipelines, etc. are referenced via handles. These are either be created by the application and passed to events (via `addon_event::init_...`) or can be created through the `device` object of the ReShade API (via `device::create_...(...)`).\
Buffers and textures are referenced via `resource` handles. Depth-stencil, render target, shader resource or unordered access views to such resources are referenced via `resource_view` handles. Sampler state objects via `sampler` handles. (Partial) pipeline state objects via `pipeline` handles. And so on.
