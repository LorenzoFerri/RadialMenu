#include "game/input/native_input.h"

#include "game/input/radial_camera.h"
#include "game/input/radial_switch.h"
#include "render/ui/config_editor.h"

namespace radial_menu_mod::native_input {
namespace {

bool IsRadialOrEditorActive()
{
    return radial_switch::IsRadialActive() || config_editor::IsOpen();
}

}  // namespace

bool Initialize()
{
    radial_switch::Initialize();
    radial_camera::Initialize(&IsRadialOrEditorActive);
    return true;
}

void SampleFrame()
{
    radial_switch::SampleFrame();
    radial_camera::SampleFrame();
}

}  // namespace radial_menu_mod::native_input
