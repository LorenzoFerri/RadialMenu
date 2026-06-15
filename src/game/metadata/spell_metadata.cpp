#include "game/metadata/spell_metadata.h"

#include "core/common.h"
#include "game/messages/message_repository.h"
#include "game/params/param_repository.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace radial_menu_mod {

namespace {

constexpr std::uintptr_t kMagicParamOffset      = 0x478;
constexpr std::uintptr_t kMagicReqIntOffset     = 0x22;
constexpr std::uintptr_t kMagicReqFaithOffset   = 0x23;
// SoloParamRepository holder index 3 (EquipParamGoods), first res-cap pointer.
constexpr std::uintptr_t kGoodsParamOffset      = 0x160;
constexpr std::uintptr_t kGoodsIconIdOffset     = 0x30;
constexpr std::uintptr_t kGoodsTypeOffset       = 0x3E;
constexpr std::uint8_t   kGoodsTypeSorcery = 5;
constexpr std::uint8_t   kGoodsTypeIncantation = 16;
constexpr std::uint8_t   kGoodsTypeSpellTool = 17;
constexpr std::uint8_t   kGoodsTypeSpellBuff = 18;

std::mutex g_cache_mutex;
std::unordered_map<std::uint32_t, ResolvedSpellMetadata> g_metadata_cache;
ULONGLONG g_last_slow_spell_metadata_log_ms = 0;

void LogSlowSpellMetadata(std::uint32_t spell_id, ULONGLONG start_ms, const ResolvedSpellMetadata& metadata)
{
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now - start_ms;
    if (elapsed < 16) return;
    if (g_last_slow_spell_metadata_log_ms != 0 && now - g_last_slow_spell_metadata_log_ms < 2000) return;

    g_last_slow_spell_metadata_log_ms = now;
    Log("Timing: ResolveSpellMetadata(%u) took %llums (icon=%u).",
        spell_id,
        static_cast<unsigned long long>(elapsed),
        metadata.icon_id);
}

std::uint32_t ReadGoodsIconId(std::uintptr_t repo, std::uint32_t spell_id)
{
    const std::uint8_t* data = param_repository::FindRowData(repo, kGoodsParamOffset, spell_id);
    if (!data) return 0;

    const std::uint8_t goods_type = data[kGoodsTypeOffset];
    if (goods_type != kGoodsTypeSorcery &&
        goods_type != kGoodsTypeIncantation &&
        goods_type != kGoodsTypeSpellTool &&
        goods_type != kGoodsTypeSpellBuff) return 0;

    const auto icon_id = *reinterpret_cast<const std::uint16_t*>(data + kGoodsIconIdOffset);
    return icon_id != 0 ? static_cast<std::uint32_t>(icon_id) : 0;
}

struct RuntimeMagicMetadata { std::uint32_t icon_id = 0; SpellCategory category = SpellCategory::unknown; };

RuntimeMagicMetadata ReadRuntimeMagicMetadata(std::uint32_t spell_id)
{
    const auto repo = param_repository::ResolveSoloParamRepository();
    if (!repo) return {};
    const std::uint8_t* data = param_repository::FindRowData(repo, kMagicParamOffset, spell_id);
    if (data) {

        RuntimeMagicMetadata meta{};
        if (const std::uint32_t goods_icon_id = ReadGoodsIconId(repo, spell_id)) {
            meta.icon_id = goods_icon_id;
        }
        const auto faith = *reinterpret_cast<const std::uint8_t*>(data + kMagicReqFaithOffset);
        const auto intel = *reinterpret_cast<const std::uint8_t*>(data + kMagicReqIntOffset);
        meta.category = faith > 0 ? SpellCategory::incantation
                      : intel > 0 ? SpellCategory::sorcery
                      : SpellCategory::unknown;
        return meta;
    }
    return {};
}

}  // namespace

bool InitializeSpellMetadata()
{
    message_repository::Initialize();
    return true;
}

ResolvedSpellMetadata ResolveSpellMetadata(std::uint32_t spell_id)
{
    {
        std::lock_guard lock(g_cache_mutex);
        if (const auto it = g_metadata_cache.find(spell_id); it != g_metadata_cache.end()) {
            return it->second;
        }
    }

    const ULONGLONG start = GetTickCount64();
    const auto runtime = ReadRuntimeMagicMetadata(spell_id);
    std::string name = message_repository::LookupMagicName(spell_id);
    if (name.empty()) {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "Spell %u", spell_id);
        name = buf;
    }

    ResolvedSpellMetadata metadata{
        .name = std::move(name),
        .icon_id = runtime.icon_id,
        .category = runtime.category,
    };

    {
        std::lock_guard lock(g_cache_mutex);
        g_metadata_cache[spell_id] = metadata;
    }
    LogSlowSpellMetadata(spell_id, start, metadata);

    return metadata;
}

}  // namespace radial_menu_mod
