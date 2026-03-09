#include <algorithm>
#include <stdio.h>
#include <unordered_map>

#include "state.hpp"

namespace ung {
EXPORT void ung_load_profiler_push(const char* name)
{
#ifdef UNG_LOAD_PROFILING
    const auto i = state->prof_zones.size;
    state->prof_zones.push({
        .name = state->prof_strpool.insert(name),
        .parent_idx = state->prof_stack.size ? state->prof_stack.last() : UINT32_MAX,
    });
    state->prof_stack.push(i);
    // do timing last, so we don't time the allocations
    state->prof_zones[i].start_time = ung_get_time();
#else
    (void)name;
#endif
}

EXPORT void ung_load_profiler_pop(const char* name)
{
#ifdef UNG_LOAD_PROFILING
    auto& zone = state->prof_zones[state->prof_stack.last()];
    assert(!name || zone.name == name);
    zone.duration = ung_get_time() - zone.start_time;
    if (zone.parent_idx < UINT32_MAX) {
        state->prof_zones[zone.parent_idx].child_duration += zone.duration;
    }
    state->prof_stack.size--;
#else
    (void)name;
#endif
}

struct ZoneAux {
    u32 first_child = UINT32_MAX;
    u32 next_sibling = UINT32_MAX;
    u32 num_children = 0;
};

static void dump_speedscope(const Array<ZoneAux>& aux, u32 root, float total_dur)
{
    // dump load.speedscope.json
    // dedup frame names
    Vector<std::string_view> names = {};
    names.init(root);
    std::unordered_map<std::string_view, u32> name_idx_map;
    Vector<u32> name_idx = {};
    name_idx.init(root);

    for (u32 i = 0; i < state->prof_zones.size; ++i) {
        const auto it = name_idx_map.find(state->prof_zones[i].name);
        if (it == name_idx_map.end()) {
            name_idx_map.emplace(state->prof_zones[i].name, names.size);
            name_idx.push(names.size);
            names.push(state->prof_zones[i].name);
        } else {
            name_idx.push(it->second);
        }
    }

    const auto f = fopen("load.speedscope.json", "w");
    if (!f) {
        ung_panicf("Could not open 'load.speedscope.json'");
    }
    fprintf(f, "{\n");
    fprintf(f, "\"$schema\": \"https://www.speedscope.app/file-format-schema.json\",\n");
    fprintf(f, "\"shared\": {\n");
    fprintf(f, "\"frames\": [\n");
    for (u32 i = 0; i < names.size; ++i) {
        fprintf(f, "{\"name\": \"%.*s\"}", (int)names[i].size(), names[i].data());
        if (i < names.size - 1) {
            fprintf(f, ",\n");
        } else {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "]\n");
    fprintf(f, "},\n");
    fprintf(f, "\"profiles\": [\n");
    fprintf(f, "{\n");
    fprintf(f, "\"type\": \"evented\",\n");
    fprintf(f, "\"name\": \"load profile\",\n");
    fprintf(f, "\"unit\": \"seconds\",\n");
    fprintf(f, "\"startValue\": %f,\n", state->prof_zones[0].start_time);
    fprintf(f, "\"endValue\": %f,\n", state->prof_zones[0].start_time + total_dur);
    fprintf(f, "\"events\": [");

    auto speedscope_dump = [&](auto&& self, u32 node, bool trailing_comma) -> void {
        const auto& z = state->prof_zones[node];
        fprintf(f, "{\"type\": \"O\", \"frame\": %u, \"at\": %f},\n", name_idx[node], z.start_time);
        for (u32 c = aux[node].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
            self(self, c, true);
        }
        fprintf(f, "{\"type\": \"C\", \"frame\": %u, \"at\": %f}%s\n", name_idx[node],
            z.start_time + z.duration, trailing_comma ? "," : "");
    };

    for (u32 c = aux[root].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
        const auto last_child = aux[c].next_sibling == UINT32_MAX;
        speedscope_dump(speedscope_dump, c, !last_child);
    }

    fprintf(f, "]\n");
    fprintf(f, "}],\n");
    fprintf(f, "\"activeProfileIndex\": 0,\n");
    fprintf(f, "\"exporter\": \"ung\",\n");
    fprintf(f, "\"name\": \"load.speedscope.json\"\n");
    fprintf(f, "}");
    fclose(f);
}

static void sort_children(Array<ZoneAux>& aux, u32 root)
{
    // sort each list of children
    Vector<u32> children = {};
    children.init(root);

    for (u32 z = 0; z < root + 1; ++z) {
        // We could probably avoid the vector of children if we did our own sort and used our own
        // linked-list swap. this is much better, but too much hassle for me now.
        children.size = 0;
        for (u32 c = aux[z].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
            children.push(c);
        }
        if (children.size <= 1) {
            continue;
        }

        std::sort(children.data, children.data + children.size, [&](u32 a, u32 b) {
            return state->prof_zones[a].duration < state->prof_zones[b].duration;
        });

        // write it back
        aux[z].first_child = children[0];
        for (u32 c = 0; c < children.size - 1; ++c) {
            aux[children[c]].next_sibling = children[c + 1];
        }
        aux[children[children.size - 1]].next_sibling = UINT32_MAX;
    }
}

static void dump_full(Array<ZoneAux>& aux, u32 root, float total_dur)
{
    auto dump_node = [&](auto&& self, u32 node, u32 depth, float parent_dur) -> void {
        const auto& z = state->prof_zones[node];
        float self_dur = z.duration - z.child_duration;
        const float pct = z.duration / parent_dur * 100.0f;

        printf("%*s%.*s: self=%dms, total=%dms [%.1f%%]\n", (int)(depth * 2), "",
            (int)z.name.size(), z.name.data(), (int)(self_dur * 1000.0f),
            (int)(z.duration * 1000.0f), pct);

        for (u32 c = aux[node].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
            self(self, c, depth + 1, z.duration);
        }
    };

    for (u32 c = aux[root].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
        dump_node(dump_node, c, 0, total_dur);
    }
}

EXPORT void ung_load_profiler_dump()
{
    if (state->prof_zones.size == 0) {
        return;
    }

    Array<ZoneAux> aux = {};
    aux.init(state->prof_zones.size + 1);
    const auto root = state->prof_zones.size;

    for (u32 i = 0; i < state->prof_zones.size; ++i) {
        const auto parent
            = state->prof_zones[i].parent_idx < UINT32_MAX ? state->prof_zones[i].parent_idx : root;
        // Keep order, so speedscope export works
        if (aux[parent].first_child == UINT32_MAX) {
            aux[parent].first_child = i;
        } else {
            u32 last_sibling = aux[parent].first_child;
            while (aux[last_sibling].next_sibling != UINT32_MAX) {
                last_sibling = aux[last_sibling].next_sibling;
            }
            aux[last_sibling].next_sibling = i;
        }
        aux[parent].num_children++;
    }

    float total_dur = 0.0f;
    for (u32 c = aux[root].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
        total_dur += state->prof_zones[c].duration;
    }

    dump_speedscope(aux, root, total_dur);

    sort_children(aux, root);

    dump_full(aux, root, total_dur);

    u32 num_assets = 0;
    for (u32 c = aux[root].first_child; c != UINT32_MAX; c = aux[c].next_sibling) {
        num_assets++;
    }

    printf("load done: %dms, %u assets, %u zones\n", (int)(total_dur * 1000.0f), num_assets,
        state->prof_zones.size);
}
}
