#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radial_menu_mod::data0_archive {

bool ReadFile(const std::wstring& game_directory, const wchar_t* virtual_path,
    std::vector<std::uint8_t>& bytes, std::uint64_t max_size);
void Reset();

}  // namespace radial_menu_mod::data0_archive
