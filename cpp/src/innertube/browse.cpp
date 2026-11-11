#include "browse.hpp"

#include <algorithm>

#include "error.hpp"

namespace ytgrab::innertube::browse {
namespace {

/// Recursively collect every value stored under `key`.
void find_all_by_key(const json& value, const std::string& key, std::vector<const json*>& out) {
    if (value.is_object()) {
        auto it = value.find(key);
        if (it != value.end()) out.push_back(&*it);
        for (auto child = value.begin(); child != value.end(); ++child) {
            find_all_by_key(*child, key, out);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) find_all_by_key(item, key, out);
    }
}

std::optional<std::string> find_continuation(const json& value) {
    std::vector<const json*> found;
    find_all_by_key(value, "continuationCommand", found);
    for (const json* c : found) {
        if (!c->is_object()) continue;
        auto token = c->find("token");
        if (token != c->end() && token->is_string() && !token->get<std::string>().empty()) {
            return token->get<std::string>();
        }
    }
    found.clear();
    find_all_by_key(value, "nextContinuationData", found);
    for (const json* n : found) {
        if (!n->is_object()) continue;
        auto token = n->find("continuation");
        if (token != n->end() && token->is_string() && !token->get<std::string>().empty()) {
            return token->get<std::string>();
        }
    }
    return std::nullopt;
}

std::string title_of(const json& renderer) {
    auto title = renderer.find("title");
    if (title == renderer.end() || !title->is_object()) return "";
    if (title->contains("runs") && (*title)["runs"].is_array()) {
        std::string out;
        for (const auto& run : (*title)["runs"]) {
            if (run.is_object() && run.contains("text") && run["text"].is_string()) {
                out += run["text"].get<std::string>();
            }
        }
        return out;
    }
    if (title->contains("simpleText") && (*title)["simpleText"].is_string()) {
        return (*title)["simpleText"].get<std::string>();
    }
    return "";
}

std::vector<PlaylistItem> collect_items(const json& raw) {
    std::vector<const json*> renderers;
    find_all_by_key(raw, "playlistVideoRenderer", renderers);

    std::vector<PlaylistItem> out;
    for (const json* r : renderers) {
        if (!r->is_object()) continue;
        auto id = r->find("videoId");
        if (id == r->end() || !id->is_string()) continue;

        PlaylistItem item;
        item.video_id = id->get<std::string>();
        item.title = title_of(*r);

        auto index = r->find("index");
        if (index != r->end() && index->is_object() && index->contains("simpleText") &&
            (*index)["simpleText"].is_string()) {
            try {
                item.index = std::stoull((*index)["simpleText"].get<std::string>());
            } catch (...) {
            }
        }
        auto length = r->find("lengthSeconds");
        if (length != r->end() && length->is_string()) {
            try {
                item.duration_secs = std::stoull(length->get<std::string>());
            } catch (...) {
            }
        }
        out.push_back(std::move(item));
    }
    return out;
}

}  // namespace

std::vector<PlaylistItem> playlist(InnerTube& it, const std::string& playlist_id) {
    std::vector<PlaylistItem> items;
    std::optional<std::string> continuation;
    std::vector<std::string> seen_tokens;

    while (true) {
        json body = it.context_object();
        if (continuation) {
            body["continuation"] = *continuation;
        } else {
            body["browseId"] = "VL" + playlist_id;
        }

        json raw = it.post("browse", body);
        auto page = collect_items(raw);
        items.insert(items.end(), page.begin(), page.end());

        auto token = find_continuation(raw);
        if (token && std::find(seen_tokens.begin(), seen_tokens.end(), *token) ==
                         seen_tokens.end()) {
            seen_tokens.push_back(*token);
            continuation = *token;
        } else {
            break;
        }
        if (items.size() > 10000) break;
    }
    return items;
}

}  // namespace ytgrab::innertube::browse
