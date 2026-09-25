#pragma once

// Keys in the existing "lilka" Preferences namespace. Keep each under NVS's
// 15-character key limit and write kImageKey last as the complete-record marker.
namespace scummvm_recent {
constexpr char kManifestKey[] = "sv_manifest";
constexpr char kTitleKey[] = "sv_title";
constexpr char kImageKey[] = "sv_image";
} // namespace scummvm_recent
