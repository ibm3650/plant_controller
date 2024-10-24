//
// Created by kandu on 24.10.2024.
//
#include "eeprom_storage.h"
#include "24c32.h"
#include "ds1307.h"
#include <set>
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#define LOG_DEBUG(...)
#endif

#ifdef LOG_ERROR
#undef LOG_ERROR
#define LOG_ERROR(...)
#endif

#ifdef LOG_INFO
#undef LOG_INFO
#define LOG_INFO(...)
#endif

static std::set<entry_t> entriesQueue;


size_t cache() {
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    uint16_t current_address = 0x0000;
    size_t ctr = 0;
    while (current_address < eeprom::STORAGE_SIZE) {
        LOG_DEBUG("Reading EEPROM at address", current_address);
        // Определяем страницу и смещение в ней
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;

        // Читаем текущую страницу EEPROM
        eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Читаем элемент по текущему адресу
        auto *current_element = reinterpret_cast<entry_t *>(buffer + offset_in_page);

        // Проверяем, что элемент не удален
        if (!current_element->deleted) {
            const std::time_t current_time = ds1307::time(nullptr);
            const auto curr_min = (std::localtime(&current_time)->tm_hour * 60) + std::localtime(&current_time)->tm_min;
            //if (curr_min >= current_element->start || curr_min <= current_element->end) {
            if (curr_min <= current_element->end) {
                ctr++;
                cache_push(*current_element);
            }
        }

        // Если next_node равен 0x0000, значит, это последний элемент
        if (current_element->next_node == 0x0000) {
            break;
        }

        // Переходим к следующему элементу по указателю next_node
        current_address = current_element->next_node;
    }
    return ctr;
    // return nodes;  // Возвращаем все найденные элементы
}


entry_t get_node(uint16_t address) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    size_t const page = address / eeprom::PAGE_SIZE;
    size_t const offset = address % eeprom::PAGE_SIZE;

    // Ограничение на выход за пределы EEPROM
    if (address + sizeof(entry_t) > eeprom::STORAGE_SIZE) {
        LOG_ERROR("Address out of bounds");
        return {};
    }

    eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

    // Перебираем данные внутри страницы
    for (size_t j = offset / sizeof(entry_t); j < eeprom::PAGE_SIZE / sizeof(entry_t); ++j) {
        auto *current_element = reinterpret_cast<entry_t *>(buffer + j * sizeof(entry_t));

        // Проверяем, что запись не удалена
        if (!current_element->deleted) {
            LOG_DEBUG("Node found", current_element->start, current_element->end, current_element->transition_time);
            return *current_element;
        }
    }

    LOG_INFO("EEPROM is empty or all nodes are deleted");
    return {};
}

//TODO: Оптимизация числа записи и чтения
//TODO: Проверка пределов страницы
void insert_node(const entry_t &entry) {
    uint8_t buffer[eeprom::PAGE_SIZE];

    for (size_t i = 0; i < eeprom::STORAGE_SIZE / eeprom::PAGE_SIZE; ++i) {
        eeprom::read_random(i * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

        // Перебираем элементы внутри страницы
        for (size_t j = 0; j < eeprom::PAGE_SIZE / sizeof(entry_t); ++j) {
            const size_t current_address = i * eeprom::PAGE_SIZE + j * sizeof(entry_t);
            auto *current_element = reinterpret_cast<entry_t *>(buffer + j * sizeof(entry_t));

            // Если запись удалена, вставляем на её место и сохраняем цепочку
            if (current_element->deleted) {
                LOG_DEBUG("Inserting node into deleted slot", entry.start, entry.end, entry.transition_time);

                // Создаем копию нового элемента
                entry_t new_entry = entry;

                // Сохраняем цепочку — если у удаленного элемента есть указатель на следующий элемент,
                // мы должны перенести этот указатель в новую запись
                new_entry.next_node = current_element->next_node;

                // Записываем новый элемент на место удаленного
                eeprom::write_page(current_address, reinterpret_cast<const uint8_t *>(&new_entry), sizeof(entry_t));
                return;
            }

            // Если нашли последнюю запись в цепочке, добавляем новую
            if (current_element->next_node == 0x0000) {
                LOG_DEBUG("Inserting node into eeprom", entry.start, entry.end, entry.transition_time);

                const size_t next_address = current_address + sizeof(entry_t);

                // Указываем адрес следующего элемента в текущем элементе
                current_element->next_node = next_address;

                // Обновляем текущий элемент с новым указателем next_node
                eeprom::write_page(current_address, reinterpret_cast<const uint8_t *>(current_element),
                                   sizeof(entry_t));

                // Записываем новую запись на следующее место
                eeprom::write_page(next_address, reinterpret_cast<const uint8_t *>(&entry), sizeof(entry_t));
                return;
            }
        }
    }

    LOG_INFO("EEPROM is full, unable to insert node");
}


std::vector<std::pair<uint16_t, entry_t>> get_all_nodes() {
    std::vector<std::pair<uint16_t, entry_t>> nodes;  // Для хранения всех считанных элементов
    uint8_t buffer[eeprom::PAGE_SIZE];

    // Начинаем с первого элемента
    uint16_t current_address = 0x0000;

    while (current_address < eeprom::STORAGE_SIZE) {
        LOG_DEBUG("Reading EEPROM at address", current_address);
        // Определяем страницу и смещение в ней
        size_t const page = current_address / eeprom::PAGE_SIZE;
        size_t const offset_in_page = current_address % eeprom::PAGE_SIZE;

        // Читаем текущую страницу EEPROM
        eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

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

void delete_node(uint16_t address) {
    uint8_t buffer[eeprom::PAGE_SIZE];
    size_t const page = address / eeprom::PAGE_SIZE;
    size_t const offset = address % eeprom::PAGE_SIZE;

    // Чтение страницы
    eeprom::read_random(page * eeprom::PAGE_SIZE, buffer, eeprom::PAGE_SIZE);

    auto *current_element = reinterpret_cast<entry_t *>(buffer + offset);

    // Если элемент не удален, помечаем его как удаленный
    if (!current_element->deleted) {
        current_element->deleted = 1;
        LOG_DEBUG("Node deleted", address, current_element->start, current_element->end);
        eeprom::write_page((page * eeprom::PAGE_SIZE) + offset, reinterpret_cast<uint8_t *>(current_element),
                           sizeof(entry_t));
    } else {
        LOG_INFO("Node already deleted");
    }
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
