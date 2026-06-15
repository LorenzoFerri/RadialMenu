#include "render/vfs/data0_archive.h"

#include "core/common.h"
#include "render/vfs/path_utils.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace radial_menu_mod::data0_archive {
namespace {

constexpr std::size_t kRsaBlockSize = 256;
constexpr std::size_t kDecryptedBlockSize = 255;
constexpr std::size_t kEldenRingFileHeaderSize = 40;
constexpr std::uint64_t kPathHashPrime = 0x85;

constexpr std::array<std::uint8_t, 4> kPublicExponent = {0x47, 0x31, 0xFC, 0x21};
constexpr std::array<std::uint8_t, 256> kData0Modulus = {
    0xF5, 0x18, 0xEE, 0xDB, 0x08, 0x6B, 0xB9, 0x70, 0xD5, 0x41, 0x9A, 0x5F, 0xCA, 0x55, 0x44, 0x3D,
    0xE3, 0x71, 0x9B, 0xB5, 0xE0, 0x30, 0x77, 0x03, 0xC9, 0xA6, 0x91, 0x50, 0x8A, 0x57, 0x40, 0x4D,
    0x2A, 0x12, 0x8F, 0xBA, 0x63, 0x7E, 0x8B, 0x3F, 0x4C, 0x69, 0x16, 0xF2, 0xF4, 0x1F, 0x49, 0x0A,
    0xC4, 0x7C, 0x29, 0xB8, 0x84, 0xAC, 0x76, 0x0A, 0xE7, 0x30, 0x72, 0x7F, 0xCA, 0x3E, 0x12, 0xEB,
    0x53, 0x16, 0xCE, 0x13, 0xBC, 0xCB, 0x80, 0x50, 0x6F, 0x0F, 0xF3, 0x25, 0xE8, 0x61, 0x0D, 0x24,
    0x42, 0x79, 0xC1, 0x49, 0x98, 0x50, 0x73, 0xDC, 0x8D, 0x89, 0x0B, 0x91, 0x42, 0x8E, 0x82, 0xBE,
    0xF3, 0x6C, 0x5D, 0xF7, 0x13, 0x39, 0x5D, 0x77, 0x5F, 0xB1, 0xD0, 0x73, 0x46, 0x24, 0xA0, 0x86,
    0xE2, 0x07, 0x2F, 0x8A, 0xA4, 0xD9, 0x66, 0x7F, 0xD1, 0xFF, 0xE7, 0x2B, 0x95, 0x72, 0x81, 0xE5,
    0x97, 0x9F, 0xFA, 0x0B, 0x79, 0x80, 0x4B, 0x8D, 0x7D, 0x52, 0xF3, 0x04, 0x92, 0x01, 0x2C, 0xDA,
    0xEB, 0x82, 0x57, 0x8E, 0xDD, 0x1B, 0x3F, 0xF8, 0xBA, 0x9A, 0x95, 0x76, 0x48, 0xB6, 0x6A, 0x29,
    0x1C, 0x68, 0xEF, 0x1D, 0x9B, 0x21, 0x0A, 0xD1, 0xD7, 0x21, 0xCD, 0x7A, 0x44, 0x85, 0xDA, 0x30,
    0x61, 0x64, 0x88, 0x1C, 0x6E, 0xD3, 0x01, 0xC6, 0x30, 0xB2, 0xC2, 0x7F, 0xBB, 0xA7, 0xDE, 0x78,
    0xC2, 0x37, 0x1F, 0x8F, 0x10, 0x79, 0x55, 0x66, 0x99, 0xE2, 0x11, 0x9B, 0x2A, 0x18, 0xB5, 0x6E,
    0x16, 0x0F, 0x71, 0xC8, 0x6D, 0xE1, 0x79, 0xC6, 0x85, 0xC5, 0x13, 0x58, 0xED, 0xCD, 0x98, 0x95,
    0xAC, 0x97, 0x07, 0x1C, 0x8D, 0x0E, 0x24, 0xB2, 0xEB, 0x4B, 0xAB, 0x7B, 0x91, 0x8C, 0xB3, 0x0C,
    0xD0, 0x87, 0x93, 0x91, 0x03, 0xBA, 0x3E, 0xFF, 0x3F, 0x47, 0xB5, 0x0D, 0x16, 0xD7, 0x42, 0x5B,
};

struct ArchiveEntry {
    std::uint32_t padded_size = 0;
    std::uint64_t offset = 0;
};

std::mutex g_mutex;
std::wstring g_cached_game_directory;
std::vector<std::uint8_t> g_decrypted_header;
bool g_logged_header_failure = false;
bool g_logged_archive_read = false;

template <typename T>
bool ReadLe(const std::vector<std::uint8_t>& bytes, std::size_t offset, T& value)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}

bool StartsWith(const std::vector<std::uint8_t>& bytes, const char* magic)
{
    return bytes.size() >= 4 && std::memcmp(bytes.data(), magic, 4) == 0;
}

bool DecryptHeader(const std::vector<std::uint8_t>& encrypted, std::vector<std::uint8_t>& decrypted)
{
    if (encrypted.empty() || encrypted.size() % kRsaBlockSize != 0) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0) return false;

    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = static_cast<ULONG>(kData0Modulus.size() * 8);
    header.cbPublicExp = static_cast<ULONG>(kPublicExponent.size());
    header.cbModulus = static_cast<ULONG>(kData0Modulus.size());

    std::vector<std::uint8_t> blob(sizeof(header) + kPublicExponent.size() + kData0Modulus.size());
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), kPublicExponent.data(), kPublicExponent.size());
    std::memcpy(blob.data() + sizeof(header) + kPublicExponent.size(), kData0Modulus.data(), kData0Modulus.size());

    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key,
            blob.data(), static_cast<ULONG>(blob.size()), 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    decrypted.clear();
    decrypted.reserve((encrypted.size() / kRsaBlockSize) * kDecryptedBlockSize);
    std::array<std::uint8_t, kRsaBlockSize> output{};
    bool ok = true;
    for (std::size_t offset = 0; offset < encrypted.size(); offset += kRsaBlockSize) {
        ULONG written = 0;
        const NTSTATUS status = BCryptEncrypt(
            key,
            const_cast<PUCHAR>(encrypted.data() + offset),
            static_cast<ULONG>(kRsaBlockSize),
            nullptr,
            nullptr,
            0,
            output.data(),
            static_cast<ULONG>(output.size()),
            &written,
            BCRYPT_PAD_NONE);
        if (status != 0 || written != output.size()) {
            ok = false;
            break;
        }
        decrypted.insert(decrypted.end(), output.begin() + 1, output.end());
    }

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok && StartsWith(decrypted, "BHD5");
}

bool EnsureHeader(const std::wstring& game_directory)
{
    if (game_directory == g_cached_game_directory && StartsWith(g_decrypted_header, "BHD5")) return true;

    std::vector<std::uint8_t> encrypted;
    const std::wstring path = asset_reader::JoinDiskPath(game_directory, L"Data0.bhd");
    if (!asset_reader::ReadDiskFile(path, encrypted, 4ull * 1024ull * 1024ull) ||
        !DecryptHeader(encrypted, g_decrypted_header)) {
        g_decrypted_header.clear();
        if (!g_logged_header_failure) {
            Log("Asset reader: failed to decrypt Data0.bhd for direct icon reads.");
            g_logged_header_failure = true;
        }
        return false;
    }

    g_cached_game_directory = game_directory;
    g_logged_header_failure = false;
    return true;
}

std::uint64_t HashVirtualPath(const wchar_t* virtual_path)
{
    std::wstring path = asset_reader::NormalizePath(virtual_path);
    constexpr const wchar_t* prefix = L"data0:/";
    if (path.rfind(prefix, 0) == 0) path.erase(0, 6);
    if (path.empty() || path.front() != L'/') path.insert(path.begin(), L'/');

    std::uint64_t hash = 0;
    for (wchar_t ch : path) {
        const wchar_t lower = ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
        hash = hash * kPathHashPrime + static_cast<std::uint64_t>(lower);
    }
    return hash;
}

bool FindEntry(std::uint64_t wanted_hash, ArchiveEntry& entry)
{
    std::int32_t bucket_count = 0;
    std::int32_t buckets_offset = 0;
    if (!ReadLe(g_decrypted_header, 16, bucket_count) ||
        !ReadLe(g_decrypted_header, 20, buckets_offset) ||
        bucket_count <= 0 || bucket_count > 100000 || buckets_offset < 0) {
        return false;
    }

    for (std::int32_t bucket = 0; bucket < bucket_count; ++bucket) {
        const std::size_t bucket_offset = static_cast<std::size_t>(buckets_offset) +
            static_cast<std::size_t>(bucket) * 8;
        std::int32_t file_count = 0;
        std::int32_t files_offset = 0;
        if (!ReadLe(g_decrypted_header, bucket_offset, file_count) ||
            !ReadLe(g_decrypted_header, bucket_offset + 4, files_offset) ||
            file_count < 0 || file_count > 100000 || files_offset < 0) {
            return false;
        }

        for (std::int32_t file = 0; file < file_count; ++file) {
            const std::size_t header_offset = static_cast<std::size_t>(files_offset) +
                static_cast<std::size_t>(file) * kEldenRingFileHeaderSize;
            std::uint64_t hash = 0;
            if (!ReadLe(g_decrypted_header, header_offset, hash)) return false;
            if (hash != wanted_hash) continue;

            std::int32_t padded_size = 0;
            std::int64_t file_offset = 0;
            if (!ReadLe(g_decrypted_header, header_offset + 8, padded_size) ||
                !ReadLe(g_decrypted_header, header_offset + 16, file_offset) ||
                padded_size <= 0 || file_offset < 0) {
                return false;
            }
            entry.padded_size = static_cast<std::uint32_t>(padded_size);
            entry.offset = static_cast<std::uint64_t>(file_offset);
            return true;
        }
    }
    return false;
}

bool ReadArchiveRange(const std::wstring& path, const ArchiveEntry& entry, std::vector<std::uint8_t>& bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(entry.offset);
    bool ok = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;
    bytes.resize(entry.padded_size);

    std::size_t total = 0;
    while (ok && total < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - total, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        ok = ::ReadFile(file, bytes.data() + total, requested, &read, nullptr) != FALSE && read != 0;
        total += read;
    }
    CloseHandle(file);

    if (!ok || total != bytes.size()) {
        bytes.clear();
        return false;
    }
    return true;
}

}  // namespace

bool ReadFile(const std::wstring& game_directory, const wchar_t* virtual_path,
    std::vector<std::uint8_t>& bytes, std::uint64_t max_size)
{
    std::lock_guard lock(g_mutex);
    bytes.clear();
    if (game_directory.empty() || !virtual_path || !EnsureHeader(game_directory)) return false;

    ArchiveEntry entry{};
    if (!FindEntry(HashVirtualPath(virtual_path), entry) || entry.padded_size > max_size) return false;

    const std::wstring data_path = asset_reader::JoinDiskPath(game_directory, L"Data0.bdt");
    if (!ReadArchiveRange(data_path, entry, bytes)) return false;

    if (!g_logged_archive_read) {
        Log("Asset reader: icon assets resolved directly from Data0 archive.");
        g_logged_archive_read = true;
    }
    return true;
}

void Reset()
{
    std::lock_guard lock(g_mutex);
    g_cached_game_directory.clear();
    g_decrypted_header.clear();
    g_logged_header_failure = false;
    g_logged_archive_read = false;
}

}  // namespace radial_menu_mod::data0_archive
