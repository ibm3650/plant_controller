#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-non-private-member-variables-in-classes"
#pragma ide diagnostic ignored "misc-include-cleaner"
#pragma ide diagnostic ignored "modernize-use-trailing-return-type"
//
// Created by kandu on 24.10.2024.
//

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

constexpr auto CACHE_SIZE = 16U;

struct entry_t {
    uint16_t start: 11;
    uint16_t end: 11;
    uint8_t transition_time;
    uint16_t next_node: 12;
    uint8_t deleted: 1;

    bool operator<(const entry_t &other) const {
        if (start != other.start) {
            return start < other.start;
        }
        if (end != other.end) {
            return end < other.end;
        }
        return transition_time < other.transition_time;
    }
} __attribute__((packed));


size_t cache();

std::optional<entry_t> get_node(uint16_t address);

[[nodiscard]] bool insert_node(const entry_t &entry);

std::vector<std::pair<uint16_t, entry_t>> get_all_nodes();

[[nodiscard]] bool delete_node(uint16_t address);

bool is_cache_empty();

entry_t cache_top();

void cache_pop();

void cache_pop(const entry_t &entry);

void cache_push(const entry_t &entry);

#pragma clang diagnostic pop