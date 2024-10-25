#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-include-cleaner"
//
// Created by kandu on 24.10.2024.
//
#include "24c32.h"
#include "ds1307.h"
#include "eeprom_storage.h"
#include <set>


static std::set<entry_t> entriesQueue;

//FIXME: Избыточное число операций чтения/записи в EEPROM
//FIXME: Страничное чтение/запись

size_t cache() {
    uint8_t buffer[eeprom::PAGE_SIZE];
    uint16_t current_address = 0x0000;
    size_t ctr = 0;
    while (current_address < eeprom::STORAGE_SIZE) {
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;
        if (!eeprom::read_random(page * eeprom::PAGE_SIZE, buffer)) {
            return 0;
        }
        const auto *current_element = reinterpret_cast<entry_t *>(buffer + offset_in_page);
        if (!current_element->deleted) {
            const std::time_t current_time = ds1307::time(nullptr);
            // Проверяем, что текущая запись актуальна по времени
            const uint16_t curr_min =
                    (std::localtime(&current_time)->tm_hour * 60) + std::localtime(&current_time)->tm_min;
            if (curr_min <= current_element->end) {
                ctr++;
                cache_push(*current_element);
            }
        }
        if (current_element->next_node == 0x0000) {
            break;
        }
        current_address = current_element->next_node;
    }
    return ctr;
}


std::optional<entry_t> get_node(uint16_t address) {
    entry_t entry{};
    if (!eeprom::read_random(address, {reinterpret_cast<uint8_t *>(&entry), sizeof(entry_t)})) {
        return std::nullopt;
    }
    if (entry.deleted) {
        return std::nullopt;
    }
    return entry;
}


bool insert_node(const entry_t &entry) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    for (size_t i = 0; i < eeprom::STORAGE_SIZE / eeprom::PAGE_SIZE; ++i) {
        if (!eeprom::read_random(i * eeprom::PAGE_SIZE, buffer)) {
            return false;
        }
        for (size_t j = 0; j < eeprom::PAGE_SIZE / sizeof(entry_t); ++j) {
            const size_t current_address = (i * eeprom::PAGE_SIZE) + j * sizeof(entry_t);
            auto *current_element = reinterpret_cast<entry_t *>(buffer + j * sizeof(entry_t));
            // Если запись удалена, вставляем на её место и сохраняем цепочку
            if (current_element->deleted) {
                // Создаем копию нового элемента
                entry_t new_entry = entry;
                // Сохраняем цепочку — если у удаленного элемента есть указатель на следующий элемент,
                // мы должны перенести этот указатель в новую запись
                new_entry.next_node = current_element->next_node;
                // Записываем новый элемент на место удаленного
                return eeprom::write_page(current_address,
                                          {reinterpret_cast<uint8_t *>(&new_entry), sizeof(entry_t)});
            }
            // Если нашли последнюю запись в цепочке, добавляем новую
            if (current_element->next_node == 0x0000) {
                size_t next_address = current_address + sizeof(entry_t);
                // Если следующий элемент не помещается в текущей странице, переходим на следующую страницу
                if (eeprom::PAGE_SIZE - next_address < sizeof(entry_t)) {
                    next_address = (i + 1) * eeprom::PAGE_SIZE;
                }
                // Указываем адрес следующего элемента в текущем элементе
                current_element->next_node = next_address;

                // Обновляем текущий элемент с новым указателем next_node
                if (!eeprom::write_page(current_address,
                                        {buffer + j * sizeof(entry_t), sizeof(entry_t)})) {
                    return false;
                }
                // Записываем новую запись на следующее место
                return eeprom::write_page(next_address,
                                          {reinterpret_cast<const uint8_t *>(&entry), sizeof(entry_t)});
            }
        }
    }
    return false;
}


std::vector<std::pair<uint16_t, entry_t>> get_all_nodes() {
    std::vector<std::pair<uint16_t, entry_t>> nodes;  // Для хранения всех считанных элементов
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    uint16_t current_address = 0x0000;

    while (current_address < eeprom::STORAGE_SIZE) {
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;
        // Читаем текущую страницу EEPROM
        if(!eeprom::read_random(page * eeprom::PAGE_SIZE, {buffer, eeprom::PAGE_SIZE})) {
            return {};
        }

        // Читаем элемент по текущему адресу
        auto *current_element = reinterpret_cast<entry_t *>(buffer + offset_in_page);
        // Проверяем, что элемент не удален
        if (!current_element->deleted) {
            // Добавляем элемент в список
            nodes.emplace_back(current_address, *current_element);
        }

        // Если next_node равен 0x0000, значит, это последний элемент
        if (current_element->next_node == 0x0000) {
            break;
        }

        // Переходим к следующему элементу по указателю next_node
        current_address = current_element->next_node;
    }

    return nodes;  // Возвращаем все найденные элементы
}

bool delete_node(uint16_t address) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    size_t const page = address / eeprom::PAGE_SIZE;
    size_t const offset = address % eeprom::PAGE_SIZE;
    if (!eeprom::read_random(page * eeprom::PAGE_SIZE, {buffer, eeprom::PAGE_SIZE})) {
        return false;
    }
    auto *current_element = reinterpret_cast<entry_t *>(buffer + offset);
    if (!current_element->deleted) {
        current_element->deleted = 1;
        if (!eeprom::write_page((page * eeprom::PAGE_SIZE) + offset,
                                {buffer + offset, sizeof(entry_t)}))
            return false;
    }
    return true;
}

bool is_cache_empty() {
    return entriesQueue.empty();
}

entry_t cache_top() {
    return *entriesQueue.begin();
}

void cache_pop() {
    entriesQueue.erase(cache_top());
}

void cache_push(const entry_t &entry) {
    entriesQueue.insert(entry);
    if (entriesQueue.size() >= CACHE_SIZE) {
        auto lastElement = std::prev(entriesQueue.end());
        entriesQueue.erase(lastElement);
    }
}

void cache_pop(const entry_t &entry) {
    entriesQueue.erase(entry);
}

#pragma clang diagnostic pop