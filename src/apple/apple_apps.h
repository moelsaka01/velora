// Apple services (CLAUDE.md sec. 5 apple-services G1-G3): launch and surface Apple's own Windows apps. Off by default (Settings).
//
// Hard rules: Velora never asks for, reads, stores or transmits Apple Account credentials or tokens; sign-in happens only inside
// Apple's own apps. No network calls of its own, no scraping, no private APIs. Apps that are installed are launched as they are;
// missing ones get a Microsoft Store *search* link (ms-windows-store://search), never a product id Velora cannot verify. The
// App Store entry opens apps.apple.com in the default browser and is labelled browse-only. Owner: C.
#pragma once

#include <string>
#include <vector>

#include "apple/apple_options.h"
#include "apps/app_index.h"

namespace velora::apple {

enum class Product { Music, TV, Devices, ICloud, ITunes };

struct ProductInfo {
    Product product;
    const wchar_t* name;        // "Apple Music"
    const wchar_t* storeQuery;  // search text for the Microsoft Store
    const wchar_t* aumidPrefix; // package family prefix of the Store (MSIX) build, "" when unknown
    const wchar_t* exeName;     // classic installer's executable, "" when none
};
const std::vector<ProductInfo>& Products();

// Which product a Start-menu / AppsFolder entry is, matched by AppUserModelID prefix, executable name or exact display name.
// Nothing else matches (a "Pineapple Notes" app is not Apple software).
const ProductInfo* Classify(const apps::AppEntry& entry);

// `index` with an "Apple" group at the front: installed Apple apps (kept as they are, so their own icons and launch targets are
// used), "Get <name>" Store-search entries for the missing ones, and "App Store (web)". Without `options.enabled` the index
// is returned unchanged. Order inside the group is Music, TV, Devices, iCloud, iTunes, App Store (web); the rest follows.
std::vector<apps::AppEntry> WithAppleGroup(std::vector<apps::AppEntry> index, const Options& options);

// "Get Apple Music" entries carry this launch scheme; the web entry uses kAppStoreUrl.
inline constexpr wchar_t kStoreSearchPrefix[] = L"ms-windows-store://search/?query=";
inline constexpr wchar_t kAppStoreUrl[] = L"https://apps.apple.com/";
std::wstring StoreSearchUri(const std::wstring& query);  // percent-encodes the query

}  // namespace velora::apple
