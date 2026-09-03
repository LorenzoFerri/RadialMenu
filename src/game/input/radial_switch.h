#pragma once

namespace radial_menu_mod::radial_switch {

bool Initialize();
void SampleFrame();
void PrepareGameplayReturn();
bool IsRadialActive();
void QueueSelectionFeedback(bool is_item);

}  // namespace radial_menu_mod::radial_switch
